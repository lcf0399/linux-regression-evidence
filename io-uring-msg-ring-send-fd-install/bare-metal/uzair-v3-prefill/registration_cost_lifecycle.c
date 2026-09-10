// SPDX-License-Identifier: GPL-2.0-or-later
/* A new auxiliary identity. The original F0 binary and source are unchanged. */
#define main original_f0_standalone_main
#include "../../reproducer/io_uring_msg_ring_f0_standalone.c"
#undef main

__attribute__((noinline)) void life_marker(unsigned event, unsigned ring_id,
                                         unsigned cycle, unsigned slots,
                                         unsigned used)
{
    asm volatile("" : : "r"(event), "r"(ring_id), "r"(cycle), "r"(slots), "r"(used) : "memory");
}

static bool observing;
static unsigned slots, used;

static void mark(unsigned event, unsigned ring_id, unsigned cycle)
{
    if (observing) life_marker(event, ring_id, cycle, slots, used);
}

static void pause_us(unsigned us)
{
    struct timespec delay = {.tv_sec = us / 1000000U,
                            .tv_nsec = (us % 1000000U) * 1000UL};
    while (nanosleep(&delay, &delay))
        if (errno != EINTR) die("nanosleep");
}

static void fill_target(struct ring *src, struct ring *dst, uint64_t *bitmap)
{
    memset(bitmap, 0, ((slots + 63U) / 64U) * sizeof(*bitmap));
    for (uint64_t base = 0; base < used; base += QD) {
        int source_results[QD] = {0}, target_results[QD] = {0};
        bool source_seen[QD] = {0}, target_seen[QD] = {0};
        unsigned tail = reserve(src, QD);
        for (unsigned i = 0; i < QD; i++, tail++) {
            unsigned index = tail & *src->sq_mask;
            struct io_uring_sqe *sqe = &src->sqes[index];
            memset(sqe, 0, sizeof(*sqe));
            sqe->opcode = IORING_OP_MSG_RING;
            sqe->fd = dst->fd;
            sqe->addr = IORING_MSG_SEND_FD;
            sqe->off = tag(DST_TAG, base + i);
            sqe->file_index = IORING_FILE_INDEX_ALLOC;
            sqe->user_data = tag(SRC_TAG, base + i);
            src->sq_array[index] = index;
        }
        store(src->sq_tail, tail);
        enter_wait(src, QD);
        drain(src, SRC_TAG, base, slots, source_results, source_seen);
        drain(dst, DST_TAG, base, slots, target_results, target_seen);
        for (unsigned i = 0; i < QD; i++) {
            if (!source_seen[i] || !target_seen[i] || source_results[i] != target_results[i])
                bad("lifecycle CQE pair");
            unsigned slot = (unsigned)source_results[i];
            if (slot >= slots || (bitmap[slot / 64U] & (UINT64_C(1) << (slot % 64U))))
                bad("lifecycle slot identity");
            bitmap[slot / 64U] |= UINT64_C(1) << (slot % 64U);
        }
    }
}

struct removal_span { unsigned offset, count; };
static unsigned *installed;
static struct removal_span *spans;
static unsigned span_count;

static void check_target(struct ring *src, struct ring *dst,
                         const unsigned char *sentinel, const uint64_t *bitmap)
{
    unsigned count = 0;
    span_count = 0;
    for (unsigned slot = 0; slot < slots; slot++) {
        if (!(bitmap[slot / 64U] & (UINT64_C(1) << (slot % 64U)))) continue;
        if (count == used) bad("too many installed slots");
        installed[count++] = slot;
        if (span_count && spans[span_count - 1].offset + spans[span_count - 1].count == slot)
            spans[span_count - 1].count++;
        else
            spans[span_count++] = (struct removal_span){slot, 1};
    }
    if (count != used) bad("lifecycle installed inventory");
    /* Auto allocation may advance its hint between reuse cycles. Validate the
     * slots actually returned in CQEs, not an assumed 0..used-1 placement. */
    for (unsigned base = 0; base < used; base += QD) {
        unsigned char buffers[QD] = {0};
        bool seen[QD] = {0};
        unsigned tail = reserve(dst, QD);
        for (unsigned i = 0; i < QD; i++, tail++) {
            unsigned slot = installed[base + i], index = tail & *dst->sq_mask;
            struct io_uring_sqe *sqe = &dst->sqes[index];
            memset(sqe, 0, sizeof(*sqe));
            sqe->opcode = IORING_OP_READ; sqe->flags = IOSQE_FIXED_FILE;
            sqe->fd = (int)slot; sqe->off = slot % SENTINEL_BYTES;
            sqe->addr = (uintptr_t)&buffers[i]; sqe->len = 1;
            sqe->user_data = tag(READ_TAG, base + i);
            dst->sq_array[index] = index;
        }
        store(dst->sq_tail, tail); enter_wait(dst, QD);
        unsigned head = load(dst->cq_head), cq_tail = load(dst->cq_tail);
        if (cq_tail - head != QD) bad("lifecycle readback count");
        for (unsigned i = 0; i < QD; i++) {
            struct io_uring_cqe *cqe = &dst->cqes[(head + i) & *dst->cq_mask];
            unsigned index = decode(cqe->user_data, READ_TAG, base, QD);
            if (seen[index] || cqe->flags || cqe->res != 1) bad("lifecycle readback CQE");
            seen[index] = true;
        }
        store(dst->cq_head, cq_tail);
        for (unsigned i = 0; i < QD; i++)
            if (!seen[i] || buffers[i] != sentinel[installed[base + i] % SENTINEL_BYTES])
                bad("lifecycle readback sentinel");
    }
    if (*src->sq_dropped || *src->cq_overflow || *dst->sq_dropped || *dst->cq_overflow ||
        load(src->sq_tail) != load(src->sq_head) || load(src->cq_tail) != load(src->cq_head) ||
        load(dst->sq_tail) != load(dst->sq_head) || load(dst->cq_tail) != load(dst->cq_head))
        bad("lifecycle queue state");
}

static void clear_target(struct ring *dst, int *empty)
{
    unsigned cleared = 0;
    for (unsigned i = 0; i < span_count; i++) {
        struct io_uring_files_update update = {.offset = spans[i].offset, .fds = (uintptr_t)empty};
        int ret = (int)syscall(__NR_io_uring_register, dst->fd,
                              IORING_REGISTER_FILES_UPDATE, &update, spans[i].count);
        if (ret < 0) die("remove fixed files");
        if (ret != (int)spans[i].count) bad("short fixed-file removal");
        cleared += (unsigned)ret;
    }
    if (cleared != used) bad("fixed-file removal inventory");
}

struct result { uint64_t registration, removal, fill, interval; };

int main(int argc, char **argv)
{
    if (argc != 7) {
        fprintf(stderr, "usage: %s --timing|--observe|--counts|--smoke serial|paced|reuse slots used gap_us output.tsv\n", argv[0]);
        return 2;
    }
    bool timing = !strcmp(argv[1], "--timing");
    observing = !strcmp(argv[1], "--observe") || !strcmp(argv[1], "--counts");
    bool counts = !strcmp(argv[1], "--counts");
    bool smoke = !strcmp(argv[1], "--smoke");
    bool reuse = !strcmp(argv[2], "reuse");
    bool paced = !strcmp(argv[2], "paced");
    if ((!timing && !observing && !smoke) ||
        (!reuse && !paced && strcmp(argv[2], "serial"))) return 2;
    unsigned parsed[3];
    for (unsigned i = 0; i < 3; i++) {
        char *end;
        unsigned long v = strtoul(argv[3 + i], &end, 10);
        if (*end || v > 500000) return 2;
        parsed[i] = (unsigned)v;
    }
    slots = parsed[0]; used = parsed[1]; unsigned gap = parsed[2];
    if (!slots || slots > 8192 || !used || used > slots || used % QD ||
        (paced && gap != 50000) || (!paced && gap)) return 2;
    unsigned warmups = timing ? 3 : 0;
    unsigned measured = timing ? 15 * 32 : (counts ? (reuse ? 4 : 1) : (smoke ? 1 : 16));
    unsigned total = warmups + measured;
    struct result *records = calloc(total, sizeof(*records));
    uint64_t *bitmap = calloc((slots + 63U) / 64U, sizeof(*bitmap));
    int *empty = malloc(used * sizeof(*empty));
    installed = malloc(used * sizeof(*installed));
    spans = calloc(used, sizeof(*spans));
    if (!records || !bitmap || !empty || !installed || !spans) die("auxiliary storage");
    for (unsigned i = 0; i < used; i++) empty[i] = -1;
    FILE *out = fopen(argv[6], "wx");
    if (!out) die("exclusive output");
    struct ring src, dst;
    unsigned char sentinel[SENTINEL_BYTES];
    pin_cpu();
    for (unsigned i = 0; i < SENTINEL_BYTES; i++)
        sentinel[i] = (unsigned char)((i * 131U + 17U) & 0xffU);
    int fd = (int)syscall(__NR_memfd_create, "msg-v3-lifecycle", MFD_CLOEXEC);
    if (fd < 0) die("memfd_create");
    if (pwrite(fd, sentinel, sizeof(sentinel), 0) != sizeof(sentinel)) die("pwrite");
    ring_init(&src); register_source(&src, fd);
    if (reuse) {
        mark(10, 1, 0); ring_init(&dst); mark(20, 1, 0);
        mark(21, 1, 0); register_sparse(&dst, slots); mark(30, 1, 0);
        mark(31, 1, 0); fill_target(&src, &dst, bitmap); mark(40, 1, 0);
        check_target(&src, &dst, sentinel, bitmap); mark(45, 1, 0);
    }
    for (unsigned i = 0; i < total; i++) {
        unsigned id = reuse ? 1 : i + 1, cycle = i + 1;
        uint64_t t0, t1, t2, t3;
        if (!reuse) {
            mark(10, id, cycle); ring_init(&dst); mark(20, id, cycle);
            mark(21, id, cycle); t0 = now_ns();
            register_sparse(&dst, slots); t1 = now_ns(); mark(30, id, cycle);
            mark(31, id, cycle); t2 = now_ns();
            fill_target(&src, &dst, bitmap); t3 = now_ns(); mark(40, id, cycle);
            records[i] = (struct result){t1 - t0, 0, t3 - t2, t3 - t0};
        } else {
            mark(46, id, cycle); t0 = now_ns();
            clear_target(&dst, empty); t1 = now_ns(); mark(50, id, cycle);
            mark(31, id, cycle); t2 = now_ns();
            fill_target(&src, &dst, bitmap); t3 = now_ns(); mark(40, id, cycle);
            records[i] = (struct result){0, t1 - t0, t3 - t2, t3 - t0};
        }
        check_target(&src, &dst, sentinel, bitmap); mark(45, id, cycle);
        if (!reuse) {
            mark(51, id, cycle); unregister_files(&dst); mark(52, id, cycle);
            ring_destroy(&dst); mark(60, id, cycle);
            if (gap) pause_us(gap);
            mark(70, id, cycle);
        }
    }
    if (reuse) {
        mark(51, 1, total + 1); unregister_files(&dst); mark(52, 1, total + 1);
        ring_destroy(&dst); mark(60, 1, total + 1);
    }
    if (observing) pause_us(500000);
    mark(99, 0, total);
    fprintf(out, "trial\tround\treplicate\twarmup\tmode\tslots\tinstalls\tgap_us\tregistration_ns\tremoval_ns\tfill_ns\tinterval_ns\tsemantic_pass\tcpu\ttiming_evidence\n");
    for (unsigned i = 0; i < total; i++) {
        int round = i < warmups ? -1 : (int)((i - warmups) / 32);
        unsigned rep = i < warmups ? i : (i - warmups) % 32;
        fprintf(out, "%u\t%d\t%u\t%u\t%s\t%u\t%u\t%u\t%"PRIu64"\t%"PRIu64"\t%"PRIu64"\t%"PRIu64"\t1\t%u\t%u\n",
                i, round, rep, i < warmups, argv[2], slots, used, gap,
                records[i].registration, records[i].removal, records[i].fill,
                records[i].interval, CPU, timing);
    }
    if (fclose(out)) die("close result");
    unregister_files(&src); ring_destroy(&src); close(fd);
    free(records); free(bitmap); free(empty); free(installed); free(spans);
    return 0;
}
