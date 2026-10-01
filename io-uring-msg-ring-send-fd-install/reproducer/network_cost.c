// SPDX-License-Identifier: GPL-2.0-or-later
/* Synthetic loopback TCP handoff + echo, not a reproduction of liburing proxy. */
#define REGISTRATION_MAIN registration_only_main
#include "registration_cost.c"
#include <arpa/inet.h>
#include <netinet/tcp.h>
#include <sys/socket.h>

#define PAYLOAD 64U
#define RX_TAG UINT64_C(0x5701000000000000)
#define TX_TAG UINT64_C(0x5801000000000000)

static uint64_t cpu_ns(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &t)) die("process CPU clock");
    return (uint64_t)t.tv_sec * 1000000000U + t.tv_nsec;
}

static int tcp_socket(void)
{
    int fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0), one = 1;
    struct timeval tv = {.tv_sec = 3};
    if (fd < 0 || setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)) ||
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv)) ||
        setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv))) die("TCP socket");
    return fd;
}

static void files_update(struct ring *r, const int *fds, unsigned n)
{
    struct io_uring_files_update u = {.fds = (uintptr_t)fds};
    int ret = syscall(__NR_io_uring_register, r->fd, IORING_REGISTER_FILES_UPDATE, &u, n);
    if (ret < 0) die("source update");
    if ((unsigned)ret != n) bad("short source update");
}

static void transfer_all(int fd, unsigned char *buf, unsigned n, bool sending)
{
    while (n) {
        ssize_t ret = sending ? send(fd, buf, n, MSG_NOSIGNAL) : recv(fd, buf, n, 0);
        if (ret < 0 && errno == EINTR) continue;
        if (ret <= 0) die(sending ? "client send" : "client receive");
        buf += ret;
        n -= ret;
    }
}

/* All clients provide data before the target receives. Handle stream short
 * completions explicitly; one payload is not assumed to equal one TCP packet. */
static void fixed_io(struct ring *r, unsigned base, unsigned count,
                     unsigned char buffers[QD][PAYLOAD], bool sending)
{
    unsigned offsets[QD] = {0};
    unsigned remaining = count;
    while (remaining) {
        unsigned tail = reserve(r, remaining), submitted = 0;
        bool seen[QD] = {0};
        for (unsigned i = 0; i < count; i++) {
            if (offsets[i] == PAYLOAD) continue;
            unsigned index = tail++ & *r->sq_mask;
            struct io_uring_sqe *sqe = &r->sqes[index];
            memset(sqe, 0, sizeof(*sqe));
            sqe->opcode = sending ? IORING_OP_SEND : IORING_OP_RECV;
            sqe->flags = IOSQE_FIXED_FILE;
            sqe->fd = (int)transfer_slots[base + i];
            sqe->addr = (uintptr_t)(buffers[i] + offsets[i]);
            sqe->len = PAYLOAD - offsets[i];
            sqe->msg_flags = sending ? MSG_NOSIGNAL : 0;
            sqe->user_data = tag(sending ? TX_TAG : RX_TAG, base + i);
            r->sq_array[index] = index;
            submitted++;
        }
        if (submitted != remaining) bad("network submit inventory");
        store(r->sq_tail, tail);
        enter_wait(r, submitted);
        unsigned head = load(r->cq_head), end = load(r->cq_tail);
        if (end - head != submitted) bad("network CQE inventory");
        for (unsigned j = 0; j < submitted; j++) {
            struct io_uring_cqe *c = &r->cqes[(head + j) & *r->cq_mask];
            unsigned i = decode(c->user_data, sending ? TX_TAG : RX_TAG, base, count);
            if (seen[i] || offsets[i] == PAYLOAD || c->flags || c->res <= 0 ||
                (unsigned)c->res > PAYLOAD - offsets[i]) bad("network CQE result");
            seen[i] = true;
            offsets[i] += c->res;
            if (offsets[i] == PAYLOAD) remaining--;
        }
        store(r->cq_head, end);
    }
}

struct net_result {
    uint64_t connect_ns, fill_ns, io_ns, cleanup_ns, interval_ns, cpu_ns;
};

int main(int argc, char **argv)
{
    if (argc != 7) {
        fprintf(stderr, "usage: %s --timing|--smoke|--counts fresh|reuse slots requests gap_us output.tsv\n", argv[0]);
        return 2;
    }
    bool timing = !strcmp(argv[1], "--timing");
    observing = !strcmp(argv[1], "--counts");
    bool smoke = !strcmp(argv[1], "--smoke");
    bool fresh = !strcmp(argv[2], "fresh");
    if ((!timing && !observing && !smoke) || (!fresh && strcmp(argv[2], "reuse"))) return 2;
    unsigned v[3];
    for (unsigned i = 0; i < 3; i++) {
        char *end;
        unsigned long n = strtoul(argv[3 + i], &end, 10);
        if (*end || n > 50000) return 2;
        v[i] = n;
    }
    slots = used = v[0];
    unsigned requests = v[1], gap = v[2];
    if ((slots != 4 && slots != 64 && slots != 256) ||
        (requests != 1 && requests != 16) || gap != 50000) return 2;
    distinct_source = true;
    pin_cpu();
    unsigned warmups = timing ? 3 : 0;
    unsigned total = timing ? 63 : (observing && !fresh ? 4 : 1);
    struct net_result *records = calloc(total, sizeof(*records));
    int *clients = malloc(used * sizeof(int)), *servers = malloc(used * sizeof(int));
    int *empty = malloc(used * sizeof(int));
    uint64_t *bitmap = calloc((slots + 63) / 64, sizeof(uint64_t));
    installed = malloc(used * sizeof(unsigned));
    transfer_slots = malloc(used * sizeof(unsigned));
    spans = calloc(used, sizeof(*spans));
    if (!records || !clients || !servers || !empty || !bitmap || !installed || !transfer_slots || !spans)
        die("network storage");
    for (unsigned i = 0; i < used; i++) empty[i] = -1;
    FILE *out = fopen(argv[6], "wx");
    if (!out) die("exclusive network output");
    uint64_t setup0 = now_ns();
    int listener = tcp_socket();
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    socklen_t addrlen = sizeof(addr);
    if (bind(listener, (struct sockaddr *)&addr, sizeof(addr)) || listen(listener, 8) ||
        getsockname(listener, (struct sockaddr *)&addr, &addrlen)) die("loopback listener");
    struct ring src, dst;
    if (!fresh) {
        ring_init(&src); register_sparse(&src, slots);
        ring_init(&dst); register_sparse(&dst, slots);
    }
    uint64_t once_setup = now_ns() - setup0;
    for (unsigned cycle = 0; cycle < total; cycle++) {
        uint64_t c0 = cpu_ns(), t0 = now_ns();
        if (fresh) {
            ring_init(&src); register_sparse(&src, slots);
            ring_init(&dst); register_sparse(&dst, slots);
        }
        mark(20, 1, cycle);
        for (unsigned i = 0; i < used; i++) {
            clients[i] = tcp_socket();
            if (connect(clients[i], (struct sockaddr *)&addr, sizeof(addr))) die("loopback connect");
            servers[i] = accept4(listener, NULL, NULL, SOCK_CLOEXEC);
            int one = 1;
            if (servers[i] < 0 || setsockopt(servers[i], IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one)))
                die("loopback accept");
        }
        files_update(&src, servers, used);
        for (unsigned i = 0; i < used; i++) if (close(servers[i])) die("close original accepted FD");
        uint64_t t1 = now_ns();
        mark(31, 1, cycle);
        fill_target(&src, &dst, bitmap);
        mark(40, 1, cycle);
        uint64_t t2 = now_ns();
        collect_slots(bitmap);
        files_update(&src, empty, used);
        mark(80, 1, cycle);
        for (unsigned request = 0; request < requests; request++) {
            for (unsigned base = 0; base < used; base += QD) {
                unsigned count = chunk_count(used - base);
                unsigned char payloads[QD][PAYLOAD], received[QD][PAYLOAD] = {{0}};
                for (unsigned i = 0; i < count; i++) {
                    for (unsigned j = 0; j < PAYLOAD; j++)
                        payloads[i][j] = (unsigned char)(cycle * 7 + request * 11 + (base + i) * 13 + j);
                    transfer_all(clients[base + i], payloads[i], PAYLOAD, true);
                }
                fixed_io(&dst, base, count, received, false);
                for (unsigned i = 0; i < count; i++)
                    if (memcmp(payloads[i], received[i], PAYLOAD)) bad("target TCP payload identity");
                fixed_io(&dst, base, count, received, true);
                for (unsigned i = 0; i < count; i++) {
                    transfer_all(clients[base + i], received[i], PAYLOAD, false);
                    if (memcmp(payloads[i], received[i], PAYLOAD)) bad("client TCP echo identity");
                }
            }
        }
        mark(81, 1, cycle);
        uint64_t t3 = now_ns();
        clear_target(&dst, empty);
        for (unsigned i = 0; i < used; i++) {
            unsigned char b;
            if (recv(clients[i], &b, 1, 0) != 0) bad("target close did not reach EOF");
            if (close(clients[i])) die("client close");
        }
        if (fresh) {
            unregister_files(&dst); ring_destroy(&dst);
            unregister_files(&src); ring_destroy(&src);
        }
        uint64_t t4 = now_ns(), c1 = cpu_ns();
        records[cycle] = (struct net_result){t1-t0, t2-t1, t3-t2, t4-t3, t4-t0, c1-c0};
        mark(90, 1, cycle);
        pause_us(gap);
    }
    uint64_t end0 = now_ns();
    if (!fresh) {
        unregister_files(&dst); ring_destroy(&dst);
        unregister_files(&src); ring_destroy(&src);
    }
    if (close(listener)) die("listener close");
    uint64_t once_cleanup = now_ns() - end0;
    mark(99, 0, total);
    fprintf(out, "trial\tround\treplicate\twarmup\tmode\tslots\tinstalls\trequests\tbytes_per_request\tgap_us\tconnect_ns\tfill_ns\tio_ns\tcleanup_ns\tinterval_ns\tcpu_ns\tonce_setup_ns\tonce_cleanup_ns\tsemantic_pass\tcpu\ttiming_evidence\n");
    for (unsigned i = 0; i < total; i++) {
        bool warm = i < warmups;
        fprintf(out, "%u\t%d\t%u\t%u\t%s\t%u\t%u\t%u\t%u\t%u\t%"PRIu64"\t%"PRIu64"\t%"PRIu64"\t%"PRIu64"\t%"PRIu64"\t%"PRIu64"\t%"PRIu64"\t%"PRIu64"\t1\t%u\t%u\n",
                i, warm ? -1 : (int)((i-warmups)/4), warm ? i : (i-warmups)%4,
                warm, argv[2], slots, used, requests, PAYLOAD, gap,
                records[i].connect_ns, records[i].fill_ns, records[i].io_ns,
                records[i].cleanup_ns, records[i].interval_ns, records[i].cpu_ns,
                once_setup, once_cleanup, CPU, timing);
    }
    if (fclose(out)) die("network result close");
    free(records); free(clients); free(servers); free(empty); free(bitmap);
    free(installed); free(transfer_slots); free(spans);
    return 0;
}
