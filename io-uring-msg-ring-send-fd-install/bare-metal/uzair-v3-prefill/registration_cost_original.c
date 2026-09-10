// SPDX-License-Identifier: GPL-2.0-or-later
/* Supplementary setup/first-fill measurements. NOT the frozen F0 binary.
 * Ring creation, semantic readback, unregister and destruction are outside
 * register_plus_fill_ns. Probe mode numbers are never timing evidence. */
#define main original_f0_standalone_main
#include "../../reproducer/io_uring_msg_ring_f0_standalone.c"
#undef main

__attribute__((noinline)) void phase_marker(unsigned int phase, unsigned int slots,
                                           unsigned int used)
{
    asm volatile("" : : "r"(phase), "r"(slots), "r"(used) : "memory");
}

static void fill(struct ring *source, struct ring *target, unsigned int slots,
                 unsigned int used, uint64_t *bitmap)
{
    for (uint64_t base = 0; base < used; base += QD) {
        int source_results[QD] = {0}, target_results[QD] = {0};
        bool source_seen[QD] = {0}, target_seen[QD] = {0};
        unsigned int tail = reserve(source, QD);
        for (unsigned int i = 0; i < QD; i++, tail++) {
            uint64_t seq = base + i;
            unsigned int index = tail & *source->sq_mask;
            struct io_uring_sqe *sqe = &source->sqes[index];
            memset(sqe, 0, sizeof(*sqe));
            sqe->opcode = IORING_OP_MSG_RING;
            sqe->fd = target->fd;
            sqe->addr = IORING_MSG_SEND_FD;
            sqe->off = tag(DST_TAG, seq);
            sqe->addr3 = 0;
            sqe->file_index = IORING_FILE_INDEX_ALLOC;
            sqe->user_data = tag(SRC_TAG, seq);
            source->sq_array[index] = index;
        }
        store(source->sq_tail, tail);
        enter_wait(source, QD);
        drain(source, SRC_TAG, base, slots, source_results, source_seen);
        drain(target, DST_TAG, base, slots, target_results, target_seen);
        for (unsigned int i = 0; i < QD; i++) {
            if (!source_seen[i] || !target_seen[i] || source_results[i] != target_results[i])
                bad("supplementary CQE mismatch");
            unsigned int slot = (unsigned int)source_results[i];
            if (slot >= used || (bitmap[slot / 64U] & (UINT64_C(1) << (slot % 64U))))
                bad("supplementary slot mismatch");
            bitmap[slot / 64U] |= UINT64_C(1) << (slot % 64U);
        }
    }
}

struct costs { uint64_t registration, first_fill, register_plus_fill; };

static struct costs one(struct ring *source, const unsigned char *sentinel,
                        unsigned int slots, unsigned int used, bool probe)
{
    struct ring target;
    uint64_t *bitmap = calloc((slots + 63U) / 64U, sizeof(*bitmap));
    if (!bitmap) die("calloc bitmap");
    if (probe) phase_marker(10, slots, used);
    ring_init(&target);
    if (probe) phase_marker(20, slots, used);
    uint64_t t0 = now_ns();
    register_sparse(&target, slots);
    uint64_t t1 = now_ns();
    if (probe) phase_marker(30, slots, used);
    uint64_t t2 = now_ns();
    fill(source, &target, slots, used, bitmap);
    uint64_t t3 = now_ns();
    if (probe) phase_marker(40, slots, used);
    for (unsigned int i = 0; i < used; i++)
        if (!(bitmap[i / 64U] & (UINT64_C(1) << (i % 64U))))
            bad("supplementary missing slot");
    verify_slots(&target, sentinel, used);
    if (*source->sq_dropped || *source->cq_overflow || *target.sq_dropped ||
        *target.cq_overflow || load(source->sq_tail) != load(source->sq_head) ||
        load(source->cq_tail) != load(source->cq_head) ||
        load(target.sq_tail) != load(target.sq_head) ||
        load(target.cq_tail) != load(target.cq_head))
        bad("supplementary ring state");
    unregister_files(&target);
    if (probe) phase_marker(50, slots, used);
    ring_destroy(&target);
    if (probe) {
        phase_marker(60, slots, used);
        usleep(500000); /* allow asynchronous ctx release; never timed */
        phase_marker(70, slots, used);
    }
    free(bitmap);
    return (struct costs){t1 - t0, t3 - t2, t3 - t0};
}

int main(int argc, char **argv)
{
    if (argc != 4 || (strcmp(argv[1], "--timing") && strcmp(argv[1], "--probe") && strcmp(argv[1], "--smoke"))) {
        fprintf(stderr, "usage: %s --timing|--probe|--smoke slots installs\n", argv[0]);
        return 2;
    }
    bool probe = !strcmp(argv[1], "--probe");
    bool timing = !strcmp(argv[1], "--timing");
    char *end;
    unsigned long parsed_slots = strtoul(argv[2], &end, 10);
    if (*end || parsed_slots == 0 || parsed_slots > 8192) return 2;
    unsigned long parsed_used = strtoul(argv[3], &end, 10);
    if (*end || parsed_used > parsed_slots || parsed_used % QD) return 2;
    unsigned int slots = parsed_slots, used = parsed_used;
    struct ring source;
    unsigned char sentinel[SENTINEL_BYTES];
    pin_cpu();
    for (unsigned int i = 0; i < SENTINEL_BYTES; i++)
        sentinel[i] = (unsigned char)((i * 131U + 17U) & 0xffU);
    int fd = (int)syscall(__NR_memfd_create, "msg-v3-setup-cost", MFD_CLOEXEC);
    if (fd < 0) die("memfd_create");
    if (pwrite(fd, sentinel, sizeof(sentinel), 0) != sizeof(sentinel)) die("pwrite");
    ring_init(&source);
    register_source(&source, fd);
    unsigned int reps = timing ? 32 : 1;
    unsigned int rounds = timing ? 15 : 1;
    if (timing)
        for (unsigned int i = 0; i < 3; i++)
            (void)one(&source, sentinel, slots, used, false);
    printf("round\tslots\tinstalls\treplicates\tregistration_ns\tfirst_fill_ns\tregister_plus_fill_ns\tsemantic_pass\tcpu\ttiming_evidence\n");
    for (unsigned int round = 0; round < rounds; round++) {
        struct costs sum = {0};
        for (unsigned int i = 0; i < reps; i++) {
            struct costs cost = one(&source, sentinel, slots, used, probe);
            sum.registration += cost.registration;
            sum.first_fill += cost.first_fill;
            sum.register_plus_fill += cost.register_plus_fill;
        }
        printf("%u\t%u\t%u\t%u\t%.6f\t%.6f\t%.6f\t1\t%u\t%u\n", round, slots, used, reps,
               (double)sum.registration / reps, (double)sum.first_fill / reps,
               (double)sum.register_plus_fill / reps, CPU, timing ? 1 : 0);
    }
    unregister_files(&source);
    ring_destroy(&source);
    close(fd);
    return 0;
}
