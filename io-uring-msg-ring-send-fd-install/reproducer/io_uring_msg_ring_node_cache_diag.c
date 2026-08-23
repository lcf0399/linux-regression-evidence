// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Diagnostic companion for the MSG_RING SEND_FD reproducer.
 *
 * Cold: fill a sparse fixed-file table on a newly-created target ring.
 * Warm: fill and unregister the same-sized table first, then register a new
 *       sparse table on the same ring and fill it again.  On kernels with the
 *       io_rsrc_node cache, the second fill can reuse the returned nodes.
 *
 * Setup, priming, verification, unregister, and ring teardown remain outside
 * the timed window.  The historical standalone source is included unchanged
 * so the diagnostic reuses its raw-UAPI helpers and semantic checks.
 */

#define _GNU_SOURCE
#include <signal.h>

#define main f0_standalone_original_main
#include "io_uring_msg_ring_f0_standalone.c"
#undef main

#define DIAG_SLOTS 128U
#define DIAG_SMOKE_SLOTS 64U
#define DIAG_WARMUPS 1U
#define DIAG_PAIRS 15U
#define DIAG_REPLICATES 128U

enum diag_mode {
	MODE_TIMING,
	MODE_SMOKE,
	MODE_TRACE_COLD,
	MODE_TRACE_WARM,
};

static void check_ring_pair_empty(struct ring *src, struct ring *dst)
{
	if (*src->sq_dropped || *src->cq_overflow || *dst->sq_dropped ||
	    *dst->cq_overflow || load(src->sq_tail) != load(src->sq_head) ||
	    load(src->cq_tail) != load(src->cq_head) ||
	    load(dst->sq_tail) != load(dst->sq_head) ||
	    load(dst->cq_tail) != load(dst->cq_head))
		bad("ring not empty or overflowed");
}

/* Fill an already-registered sparse table and return only fill elapsed time. */
static uint64_t fill_sparse_table(struct ring *src, struct ring *dst,
				  const unsigned char *sentinel,
				  unsigned int slots, bool timed)
{
	uint64_t bitmap[SLOTS / 64U] = {0};
	uint64_t base, started = 0, elapsed = 0;

	if (!slots || slots > DIAG_SLOTS || slots % QD)
		bad("invalid diagnostic slot count");
	if (timed)
		started = now_ns();
	for (base = 0; base < slots; base += QD) {
		int src_res[QD] = {0}, dst_res[QD] = {0};
		bool src_seen[QD] = {0}, dst_seen[QD] = {0};
		unsigned int tail = reserve(src, QD), i;

		for (i = 0; i < QD; i++, tail++) {
			uint64_t seq = base + i;
			unsigned int index = tail & *src->sq_mask;
			struct io_uring_sqe *sqe = &src->sqes[index];

			memset(sqe, 0, sizeof(*sqe));
			sqe->opcode = IORING_OP_MSG_RING;
			sqe->fd = dst->fd;
			sqe->addr = IORING_MSG_SEND_FD;
			sqe->off = tag(DST_TAG, seq);
			sqe->addr3 = 0;
			sqe->file_index = IORING_FILE_INDEX_ALLOC;
			sqe->user_data = tag(SRC_TAG, seq);
			src->sq_array[index] = index;
		}
		store(src->sq_tail, tail);
		enter_wait(src, QD);
		drain(src, SRC_TAG, base, slots, src_res, src_seen);
		drain(dst, DST_TAG, base, slots, dst_res, dst_seen);
		for (i = 0; i < QD; i++) {
			unsigned int slot;

			if (!src_seen[i] || !dst_seen[i] ||
			    src_res[i] != dst_res[i])
				bad("source/target CQE mismatch");
			slot = (unsigned int)src_res[i];
			if (bitmap[slot / 64U] &
			    (UINT64_C(1) << (slot % 64U)))
				bad("duplicate installed slot");
			bitmap[slot / 64U] |= UINT64_C(1) << (slot % 64U);
		}
	}
	if (timed)
		elapsed = now_ns() - started;
	for (base = 0; base < slots; base++)
		if (!(bitmap[base / 64U] &
		      (UINT64_C(1) << (base % 64U))))
			bad("missing installed slot");
	verify_slots(dst, sentinel, slots);
	check_ring_pair_empty(src, dst);
	return elapsed;
}

static uint64_t run_cold(struct ring *src, const unsigned char *sentinel,
			 unsigned int slots, bool timed)
{
	return run_round(src, sentinel, slots, timed);
}

static uint64_t run_warm(struct ring *src, const unsigned char *sentinel,
			 unsigned int slots, bool timed)
{
	struct ring dst;
	uint64_t elapsed;

	ring_init(&dst);
	register_sparse(&dst, slots);
	fill_sparse_table(src, &dst, sentinel, slots, false);
	unregister_files(&dst);       /* return the first nodes to this ring */
	register_sparse(&dst, slots); /* the measured table starts empty */
	elapsed = fill_sparse_table(src, &dst, sentinel, slots, timed);
	unregister_files(&dst);
	ring_destroy(&dst);
	return elapsed;
}

/* Stop immediately before the traced fill so the runner can filter by PID. */
static void run_trace(struct ring *src, const unsigned char *sentinel,
		      unsigned int slots, bool warm)
{
	struct ring dst;

	ring_init(&dst);
	register_sparse(&dst, slots);
	if (warm) {
		fill_sparse_table(src, &dst, sentinel, slots, false);
		unregister_files(&dst);
		register_sparse(&dst, slots);
	}
	fprintf(stderr, "trace_ready=1 pid=%ld condition=%s slots=%u\n",
		(long)getpid(), warm ? "warm" : "cold", slots);
	fflush(stderr);
	if (raise(SIGSTOP))
		die("raise SIGSTOP");
	fill_sparse_table(src, &dst, sentinel, slots, false);
	unregister_files(&dst);
	ring_destroy(&dst);
	printf("trace_semantic_pass=1 condition=%s installs=%u cpu=%u\n",
	       warm ? "warm" : "cold", slots, CPU);
}

static unsigned int parse_uint(const char *text, const char *name)
{
	char *end = NULL;
	unsigned long value;

	errno = 0;
	value = strtoul(text, &end, 10);
	if (errno || !end || *end || !value || value > UINT32_MAX) {
		fprintf(stderr, "invalid %s: %s\n", name, text);
		exit(EXIT_FAILURE);
	}
	return (unsigned int)value;
}

int main(int argc, char **argv)
{
	enum diag_mode mode = MODE_TIMING;
	struct ring source;
	unsigned char sentinel[SENTINEL_BYTES];
	unsigned int slots = DIAG_SLOTS, warmups = DIAG_WARMUPS;
	unsigned int pairs = DIAG_PAIRS, replicates = DIAG_REPLICATES, i;
	int memfd;

	for (i = 1; i < (unsigned int)argc; i++) {
		if (!strcmp(argv[i], "--smoke"))
			mode = MODE_SMOKE;
		else if (!strcmp(argv[i], "--timing"))
			mode = MODE_TIMING;
		else if (!strcmp(argv[i], "--trace-cold"))
			mode = MODE_TRACE_COLD;
		else if (!strcmp(argv[i], "--trace-warm"))
			mode = MODE_TRACE_WARM;
		else if (!strcmp(argv[i], "--slots") && i + 1 < (unsigned int)argc)
			slots = parse_uint(argv[++i], "slots");
		else if (!strcmp(argv[i], "--warmups") && i + 1 < (unsigned int)argc)
			warmups = parse_uint(argv[++i], "warmups");
		else if (!strcmp(argv[i], "--pairs") && i + 1 < (unsigned int)argc)
			pairs = parse_uint(argv[++i], "pairs");
		else if (!strcmp(argv[i], "--replicates") &&
			 i + 1 < (unsigned int)argc)
			replicates = parse_uint(argv[++i], "replicates");
		else {
			fprintf(stderr, "usage: %s [--smoke|--timing|--trace-cold|--trace-warm] "
				"[--slots 64|128] [--warmups N] [--pairs N] "
				"[--replicates N]\n", argv[0]);
			return EXIT_FAILURE;
		}
	}
	if (!slots || slots > DIAG_SLOTS || slots % QD)
		bad("slots must be 64 or 128");

	pin_cpu();
	for (i = 0; i < SENTINEL_BYTES; i++)
		sentinel[i] = (unsigned char)((i * 131U + 17U) & 0xffU);
	memfd = (int)syscall(__NR_memfd_create, "msg-ring-cache-diag",
			     MFD_CLOEXEC);
	if (memfd < 0)
		die("memfd_create");
	if (pwrite(memfd, sentinel, sizeof(sentinel), 0) != sizeof(sentinel))
		die("pwrite sentinel");
	ring_init(&source);
	register_source(&source, memfd);

	if (mode == MODE_TRACE_COLD || mode == MODE_TRACE_WARM) {
		run_trace(&source, sentinel, slots, mode == MODE_TRACE_WARM);
	} else if (mode == MODE_SMOKE) {
		run_cold(&source, sentinel, DIAG_SMOKE_SLOTS, false);
		run_warm(&source, sentinel, DIAG_SMOKE_SLOTS, false);
		printf("semantic_pass=1 conditions=2 installs_per_condition=%u cpu=%u\n",
		       DIAG_SMOKE_SLOTS, CPU);
	} else {
		uint64_t cold, warm;
		unsigned int j;

		for (i = 0; i < warmups; i++) {
			for (j = 0; j < replicates; j++) {
				if (i & 1U) {
					run_warm(&source, sentinel, slots, false);
					run_cold(&source, sentinel, slots, false);
				} else {
					run_cold(&source, sentinel, slots, false);
					run_warm(&source, sentinel, slots, false);
				}
			}
		}
		printf("pair\torder\tslots\treplicates\tinstalls_per_condition\t"
		       "cold_elapsed_ns\twarm_elapsed_ns\t"
		       "cold_ns_per_install\twarm_ns_per_install\t"
		       "warm_vs_cold_pct\tsemantic_pass\tcpu\n");
		for (i = 0; i < pairs; i++) {
			cold = warm = 0;
			for (j = 0; j < replicates; j++) {
				if (i & 1U) {
					warm += run_warm(&source, sentinel, slots, true);
					cold += run_cold(&source, sentinel, slots, true);
				} else {
					cold += run_cold(&source, sentinel, slots, true);
					warm += run_warm(&source, sentinel, slots, true);
				}
			}
			printf("%u\t%s\t%u\t%u\t%u\t%" PRIu64 "\t%" PRIu64
			       "\t%.6f\t%.6f\t%.6f\t1\t%u\n",
			       i, (i & 1U) ? "warm-first" : "cold-first", slots,
			       replicates, slots * replicates, cold, warm,
			       (double)cold / (slots * replicates),
			       (double)warm / (slots * replicates),
			       ((double)warm / cold - 1.0) * 100.0, CPU);
		}
	}

	unregister_files(&source);
	ring_destroy(&source);
	close(memfd);
	return EXIT_SUCCESS;
}
