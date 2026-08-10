// SPDX-License-Identifier: GPL-2.0-or-later
/* Raw-UAPI semantic and timing workload for Linux io_uring/nop.c. */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/io_uring.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#ifndef __NR_io_uring_setup
#error "io_uring_setup syscall number is required"
#endif
#ifndef __NR_io_uring_enter
#error "io_uring_enter syscall number is required"
#endif
#ifndef __NR_io_uring_register
#error "io_uring_register syscall number is required"
#endif
#ifndef IORING_NOP_INJECT_RESULT
#define IORING_NOP_INJECT_RESULT (1U << 0)
#define IORING_NOP_FILE          (1U << 1)
#define IORING_NOP_FIXED_FILE    (1U << 2)
#define IORING_NOP_FIXED_BUFFER  (1U << 3)
#define IORING_NOP_TW            (1U << 4)
#define IORING_NOP_CQE32         (1U << 5)
#endif

#define SCHEMA_VERSION 1U
#define CONTRACT_CPU 2U
#define RING_ENTRIES 128U
#define BATCH_SIZE 64U
#define BATCHES_PER_SAMPLE 4096U
#define WARMUP_ROUNDS 3U
#define MEASURED_ROUNDS 15U
#define SMOKE_BATCHES 2U
#define TRACE_BATCHES 64U
#define INJECT_VALUE 0x1234U
#define TAG_PREFIX UINT64_C(0xa700000000000000)
#define INPUT_SHA256 \
	"9d3695aa28124023d59e7a87ea19ed0069a48d0219198abad1523a9e39b6b233"

_Static_assert(sizeof(struct io_uring_sqe) == 64, "unexpected SQE ABI");
_Static_assert(sizeof(struct io_uring_cqe) == 16, "unexpected CQE ABI");

enum profile_id {
	PROFILE_UNSET,
	PROFILE_PLAIN,
	PROFILE_INJECT,
	PROFILE_FORMAL,
	PROFILE_V7_SEMANTIC,
};

enum run_mode {
	MODE_UNSET,
	MODE_DESCRIBE,
	MODE_SMOKE,
	MODE_TRACE,
	MODE_POINT,
	MODE_SEMANTIC,
};

struct options {
	enum profile_id profile;
	enum run_mode mode;
	const char *output_path;
	bool execute;
};

struct raw_ring {
	int fd;
	bool single_mmap;
	void *sq_ptr;
	void *cq_ptr;
	void *sqes_ptr;
	size_t sq_sz;
	size_t cq_sz;
	size_t sqes_sz;
	size_t cqe_stride;
	unsigned int *sq_head;
	unsigned int *sq_tail;
	unsigned int *sq_mask;
	unsigned int *sq_entries;
	unsigned int *sq_dropped;
	unsigned int *sq_array;
	struct io_uring_sqe *sqes;
	unsigned int *cq_head;
	unsigned int *cq_tail;
	unsigned int *cq_mask;
	unsigned int *cq_entries;
	unsigned int *cq_overflow;
	char *cqes;
};

struct result {
	uint64_t requested_ops;
	uint64_t submitted_sqes;
	uint64_t completed_cqes;
	uint64_t successful_ops;
	uint64_t validation_failures;
	uint64_t duplicate_cqes;
	uint64_t unexpected_cqes;
	uint64_t bad_cqe_flags;
	uint64_t sq_dropped;
	uint64_t cq_overflow;
	uint64_t outstanding;
	uint64_t cleanup_failures;
	uint64_t timed_ns;
	bool semantic_pass;
};

struct identity {
	char kernel_release[128];
	char boot_id[128];
};

static volatile sig_atomic_t stop_requested;

static const char result_header[] =
	"schema_version\tkernel_release\tboot_id\trun_mode\tprofile\tphase"
	"\tround\torder\tslot\tring_entries\tqd\tbatches\trequested_ops"
	"\tsubmitted_sqes\tcompleted_cqes\tsuccessful_ops"
	"\tvalidation_failures\tduplicate_cqes\tunexpected_cqes"
	"\tbad_cqe_flags\tsq_dropped\tcq_overflow\toutstanding"
	"\tcleanup_failures\ttimed_ns\tns_per_op\tcpu\tsemantic_pass"
	"\texpected_res\tinput_sha256\ttrace_enabled";

static void handle_signal(int signo)
{
	(void)signo;
	stop_requested = 1;
}

static unsigned int load_acquire(const unsigned int *ptr)
{
	return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

static void store_release(unsigned int *ptr, unsigned int value)
{
	__atomic_store_n(ptr, value, __ATOMIC_RELEASE);
}

static uint64_t now_ns(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts)) {
		perror("clock_gettime");
		exit(EXIT_FAILURE);
	}
	return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + ts.tv_nsec;
}

static int pin_cpu(void)
{
	cpu_set_t set;

	if (sysconf(_SC_NPROCESSORS_ONLN) <= (long)CONTRACT_CPU)
		return -1;
	CPU_ZERO(&set);
	CPU_SET(CONTRACT_CPU, &set);
	if (sched_setaffinity(0, sizeof(set), &set))
		return -1;
	return sched_getcpu() == (int)CONTRACT_CPU ? 0 : -1;
}

static int read_text(const char *path, char *buf, size_t size)
{
	FILE *f = fopen(path, "re");
	size_t nr;

	if (!f)
		return -1;
	nr = fread(buf, 1, size - 1U, f);
	if (ferror(f)) {
		fclose(f);
		return -1;
	}
	fclose(f);
	buf[nr] = '\0';
	while (nr && (buf[nr - 1] == '\n' || buf[nr - 1] == '\r'))
		buf[--nr] = '\0';
	return 0;
}

static struct io_uring_cqe *cqe_at(const struct raw_ring *ring,
					   unsigned int logical_index)
{
	return (struct io_uring_cqe *)(ring->cqes +
		(logical_index & *ring->cq_mask) * ring->cqe_stride);
}

static unsigned int cq_available(const struct raw_ring *ring)
{
	return load_acquire(ring->cq_tail) - load_acquire(ring->cq_head);
}

static uint64_t ring_outstanding(const struct raw_ring *ring)
{
	return (uint64_t)(load_acquire(ring->sq_tail) -
			  load_acquire(ring->sq_head)) + cq_available(ring);
}

static void raw_ring_destroy(struct raw_ring *ring)
{
	if (ring->sqes_ptr)
		munmap(ring->sqes_ptr, ring->sqes_sz);
	if (ring->sq_ptr)
		munmap(ring->sq_ptr, ring->sq_sz);
	if (ring->cq_ptr && !ring->single_mmap)
		munmap(ring->cq_ptr, ring->cq_sz);
	if (ring->fd >= 0)
		close(ring->fd);
	memset(ring, 0, sizeof(*ring));
	ring->fd = -1;
}

static int raw_ring_init(struct raw_ring *ring, unsigned int setup_flags)
{
	struct io_uring_params p;
	size_t sq_sz, cq_sz;
	void *sq, *cq, *sqes;

	memset(ring, 0, sizeof(*ring));
	ring->fd = -1;
	memset(&p, 0, sizeof(p));
	p.flags = setup_flags;
	ring->fd = syscall(__NR_io_uring_setup, RING_ENTRIES, &p);
	if (ring->fd < 0)
		return -1;
	ring->cqe_stride = setup_flags & IORING_SETUP_CQE32 ? 32U : 16U;
	sq_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned int);
	cq_sz = p.cq_off.cqes + p.cq_entries * ring->cqe_stride;
	ring->single_mmap = !!(p.features & IORING_FEAT_SINGLE_MMAP);
	if (ring->single_mmap && cq_sz > sq_sz)
		sq_sz = cq_sz;
	sq = mmap(NULL, sq_sz, PROT_READ | PROT_WRITE,
		  MAP_SHARED | MAP_POPULATE, ring->fd, IORING_OFF_SQ_RING);
	if (sq == MAP_FAILED)
		goto fail;
	if (ring->single_mmap) {
		cq = sq;
		cq_sz = sq_sz;
	} else {
		cq = mmap(NULL, cq_sz, PROT_READ | PROT_WRITE,
			  MAP_SHARED | MAP_POPULATE, ring->fd, IORING_OFF_CQ_RING);
		if (cq == MAP_FAILED)
			goto fail_sq;
	}
	ring->sqes_sz = p.sq_entries * sizeof(struct io_uring_sqe);
	sqes = mmap(NULL, ring->sqes_sz, PROT_READ | PROT_WRITE,
		     MAP_SHARED | MAP_POPULATE, ring->fd, IORING_OFF_SQES);
	if (sqes == MAP_FAILED)
		goto fail_cq;
	ring->sq_ptr = sq;
	ring->cq_ptr = cq;
	ring->sqes_ptr = sqes;
	ring->sq_sz = sq_sz;
	ring->cq_sz = cq_sz;
	ring->sq_head = (unsigned int *)((char *)sq + p.sq_off.head);
	ring->sq_tail = (unsigned int *)((char *)sq + p.sq_off.tail);
	ring->sq_mask = (unsigned int *)((char *)sq + p.sq_off.ring_mask);
	ring->sq_entries = (unsigned int *)((char *)sq + p.sq_off.ring_entries);
	ring->sq_dropped = (unsigned int *)((char *)sq + p.sq_off.dropped);
	ring->sq_array = (unsigned int *)((char *)sq + p.sq_off.array);
	ring->sqes = sqes;
	ring->cq_head = (unsigned int *)((char *)cq + p.cq_off.head);
	ring->cq_tail = (unsigned int *)((char *)cq + p.cq_off.tail);
	ring->cq_mask = (unsigned int *)((char *)cq + p.cq_off.ring_mask);
	ring->cq_entries = (unsigned int *)((char *)cq + p.cq_off.ring_entries);
	ring->cq_overflow = (unsigned int *)((char *)cq + p.cq_off.overflow);
	ring->cqes = (char *)cq + p.cq_off.cqes;
	return 0;

fail_cq:
	if (!ring->single_mmap)
		munmap(cq, cq_sz);
fail_sq:
	munmap(sq, sq_sz);
fail:
	close(ring->fd);
	ring->fd = -1;
	return -1;
}

static int reserve_batch(struct raw_ring *ring, unsigned int nr,
			 unsigned int *tail_out)
{
	unsigned int head = load_acquire(ring->sq_head);
	unsigned int tail = load_acquire(ring->sq_tail);
	unsigned int i;

	if (*ring->sq_entries - (tail - head) < nr)
		return -1;
	for (i = 0; i < nr; i++) {
		unsigned int index = (tail + i) & *ring->sq_mask;

		memset(&ring->sqes[index], 0, sizeof(ring->sqes[index]));
		ring->sq_array[index] = index;
	}
	*tail_out = tail;
	return 0;
}

static int enter_batch(struct raw_ring *ring, unsigned int tail,
			       unsigned int nr)
{
	int ret;

	store_release(ring->sq_tail, tail + nr);
	do {
		ret = syscall(__NR_io_uring_enter, ring->fd, nr, nr,
			      IORING_ENTER_GETEVENTS, NULL, 0);
	} while (ret < 0 && errno == EINTR && !stop_requested);
	return ret == (int)nr && cq_available(ring) >= nr ? 0 : -1;
}

static uint64_t make_tag(enum profile_id profile, uint64_t batch,
			 unsigned int slot)
{
	return TAG_PREFIX | ((uint64_t)(profile & 0xffU) << 48) |
	       ((batch & UINT64_C(0xffffffffff)) << 8) | slot;
}

static int reap_batch(struct raw_ring *ring, enum profile_id profile,
			      uint64_t batch, unsigned int nr, int expected_res,
			      struct result *result)
{
	bool seen[BATCH_SIZE] = { false };
	unsigned int head = load_acquire(ring->cq_head);
	unsigned int i;

	for (i = 0; i < nr; i++) {
		struct io_uring_cqe *cqe = cqe_at(ring, head + i);
		uint64_t tag = cqe->user_data;
		unsigned int slot = tag & 0xffU;
		unsigned int got_profile = (tag >> 48) & 0xffU;
		uint64_t got_batch = (tag >> 8) & UINT64_C(0xffffffffff);

		result->completed_cqes++;
		if ((tag & UINT64_C(0xff00000000000000)) != TAG_PREFIX ||
		    got_profile != (unsigned int)profile || got_batch != batch ||
		    slot >= nr) {
			result->unexpected_cqes++;
			continue;
		}
		if (seen[slot])
			result->duplicate_cqes++;
		seen[slot] = true;
		if (cqe->res != expected_res)
			result->validation_failures++;
		else
			result->successful_ops++;
		if (cqe->flags)
			result->bad_cqe_flags++;
	}
	for (i = 0; i < nr; i++)
		if (!seen[i])
			result->validation_failures++;
	store_release(ring->cq_head, head + nr);
	return 0;
}

static void finish_result(struct raw_ring *ring, struct result *result)
{
	result->sq_dropped = *ring->sq_dropped;
	result->cq_overflow = *ring->cq_overflow;
	result->outstanding = ring_outstanding(ring);
	result->semantic_pass = !stop_requested &&
		!result->validation_failures && !result->duplicate_cqes &&
		!result->unexpected_cqes && !result->bad_cqe_flags &&
		!result->sq_dropped && !result->cq_overflow &&
		!result->outstanding && !result->cleanup_failures &&
		result->requested_ops == result->submitted_sqes &&
		result->submitted_sqes == result->completed_cqes &&
		result->completed_cqes == result->successful_ops;
}

static int submit_nop_batch(struct raw_ring *ring, enum profile_id profile,
			    uint64_t batch, unsigned int nr,
			    struct result *result)
{
	unsigned int tail, i;
	int expected_res = profile == PROFILE_INJECT ? INJECT_VALUE : 0;

	if (reserve_batch(ring, nr, &tail))
		return -1;
	for (i = 0; i < nr; i++) {
		unsigned int index = (tail + i) & *ring->sq_mask;
		struct io_uring_sqe *sqe = &ring->sqes[index];

		sqe->opcode = IORING_OP_NOP;
		sqe->fd = -1;
		if (profile == PROFILE_INJECT) {
			sqe->rw_flags = IORING_NOP_INJECT_RESULT;
			sqe->len = INJECT_VALUE;
		}
		sqe->user_data = make_tag(profile, batch, i);
	}
	result->requested_ops += nr;
	result->submitted_sqes += nr;
	if (enter_batch(ring, tail, nr))
		return -1;
	return reap_batch(ring, profile, batch, nr, expected_res, result);
}

static int run_sample(struct raw_ring *ring, enum profile_id profile,
			      uint64_t batches, bool timed, struct result *result)
{
	uint64_t batch, start = 0, end = 0;
	int ret = 0;

	memset(result, 0, sizeof(*result));
	if (timed)
		start = now_ns();
	for (batch = 0; batch < batches && !stop_requested; batch++)
		if (submit_nop_batch(ring, profile, batch, BATCH_SIZE, result)) {
			ret = -1;
			break;
		}
	if (timed)
		end = now_ns();
	result->timed_ns = timed ? end - start : 0;
	finish_result(ring, result);
	return ret || !result->semantic_pass ? -1 : 0;
}

static int submit_single(struct raw_ring *ring, struct io_uring_sqe *src,
			 int expected_res, bool check_extra, uint64_t extra1,
			 uint64_t extra2, struct result *result)
{
	unsigned int tail, index, head;
	struct io_uring_cqe *cqe;
	uint64_t tag = make_tag(PROFILE_V7_SEMANTIC,
				result->requested_ops, 0);

	if (reserve_batch(ring, 1, &tail))
		return -1;
	index = tail & *ring->sq_mask;
	ring->sqes[index] = *src;
	ring->sq_array[index] = index;
	ring->sqes[index].user_data = tag;
	result->requested_ops++;
	result->submitted_sqes++;
	if (enter_batch(ring, tail, 1))
		return -1;
	head = load_acquire(ring->cq_head);
	cqe = cqe_at(ring, head);
	result->completed_cqes++;
	if (cqe->user_data != tag || cqe->res != expected_res || cqe->flags)
		result->validation_failures++;
	else if (check_extra &&
		 (cqe->big_cqe[0] != extra1 || cqe->big_cqe[1] != extra2))
		result->validation_failures++;
	else
		result->successful_ops++;
	store_release(ring->cq_head, head + 1);
	return 0;
}

static int run_v7_semantic(struct result *result)
{
	struct raw_ring ring, big;
	struct io_uring_sqe sqe;
	struct iovec iov;
	void *buffer = MAP_FAILED;
	int file = -1, files[1], ret = -1;
	const uint64_t extra1 = UINT64_C(0x1122334455667788);
	const uint64_t extra2 = UINT64_C(0x99aabbccddeeff00);

	memset(result, 0, sizeof(*result));
	memset(&ring, 0, sizeof(ring));
	memset(&big, 0, sizeof(big));
	ring.fd = big.fd = -1;
	if (raw_ring_init(&ring, 0))
		goto out;
	file = open("/dev/null", O_RDONLY | O_CLOEXEC);
	buffer = mmap(NULL, 4096, PROT_READ | PROT_WRITE,
		      MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
	if (file < 0 || buffer == MAP_FAILED)
		goto out;
	files[0] = file;
	iov.iov_base = buffer;
	iov.iov_len = 4096;
	if (syscall(__NR_io_uring_register, ring.fd, IORING_REGISTER_FILES,
		    files, 1) ||
	    syscall(__NR_io_uring_register, ring.fd, IORING_REGISTER_BUFFERS,
		    &iov, 1))
		goto out;

#define RUN_CASE(flags_value, fd_value, buf_value, expected_value) do { \
	memset(&sqe, 0, sizeof(sqe)); \
	sqe.opcode = IORING_OP_NOP; \
	sqe.rw_flags = (flags_value); \
	sqe.fd = (fd_value); \
	sqe.buf_index = (buf_value); \
	if (submit_single(&ring, &sqe, (expected_value), false, 0, 0, result)) \
		goto out; \
} while (0)

	RUN_CASE(IORING_NOP_FILE, file, 0, 0);
	RUN_CASE(IORING_NOP_FILE, -1, 0, -EBADF);
	RUN_CASE(IORING_NOP_FILE | IORING_NOP_FIXED_FILE, 0, 0, 0);
	RUN_CASE(IORING_NOP_FILE | IORING_NOP_FIXED_FILE, 99, 0, -EBADF);
	RUN_CASE(IORING_NOP_FIXED_BUFFER, -1, 0, 0);
	RUN_CASE(IORING_NOP_FIXED_BUFFER, -1, 99, -EFAULT);
	RUN_CASE(IORING_NOP_TW, -1, 0, 0);
	RUN_CASE(1U << 6, -1, 0, -EINVAL);
#undef RUN_CASE

	if (raw_ring_init(&big, IORING_SETUP_CQE32))
		goto out;
	memset(&sqe, 0, sizeof(sqe));
	sqe.opcode = IORING_OP_NOP;
	sqe.rw_flags = IORING_NOP_CQE32;
	sqe.off = extra1;
	sqe.addr = extra2;
	if (submit_single(&big, &sqe, 0, true, extra1, extra2, result))
		goto out;
	finish_result(&ring, result);
	if (!result->semantic_pass)
		goto out;
	ret = 0;
out:
	if (ring.fd >= 0) {
		if (syscall(__NR_io_uring_register, ring.fd,
			    IORING_UNREGISTER_BUFFERS, NULL, 0) && errno != ENXIO)
			result->cleanup_failures++;
		if (syscall(__NR_io_uring_register, ring.fd,
			    IORING_UNREGISTER_FILES, NULL, 0) && errno != ENXIO)
			result->cleanup_failures++;
	}
	if (file >= 0 && close(file))
		result->cleanup_failures++;
	if (buffer != MAP_FAILED && munmap(buffer, 4096))
		result->cleanup_failures++;
	raw_ring_destroy(&big);
	raw_ring_destroy(&ring);
	if (result->cleanup_failures) {
		result->semantic_pass = false;
		ret = -1;
	}
	return ret;
}

static const char *profile_name(enum profile_id profile)
{
	switch (profile) {
	case PROFILE_PLAIN: return "n0_plain_nop";
	case PROFILE_INJECT: return "i0_inject_result";
	case PROFILE_FORMAL: return "formal_plain_inject";
	case PROFILE_V7_SEMANTIC: return "v7_new_flags_semantic";
	default: return "unset";
	}
}

static const char *mode_name(enum run_mode mode)
{
	switch (mode) {
	case MODE_DESCRIBE: return "describe";
	case MODE_SMOKE: return "smoke";
	case MODE_TRACE: return "trace";
	case MODE_POINT: return "point";
	case MODE_SEMANTIC: return "semantic";
	default: return "unset";
	}
}

static int trace_enabled(void)
{
	char buf[32];

	return !read_text("/sys/kernel/tracing/tracing_on", buf, sizeof(buf)) &&
	       !strcmp(buf, "1");
}

static void emit_result(FILE *out, const struct identity *id,
			enum run_mode mode, enum profile_id profile,
			const char *phase, unsigned int round,
			const char *order, const char *slot, uint64_t batches,
			const struct result *result)
{
	double ns_per_op = result->requested_ops ?
		(double)result->timed_ns / result->requested_ops : 0.0;
	int expected = profile == PROFILE_INJECT ? INJECT_VALUE : 0;

	fprintf(out,
		"%u\t%s\t%s\t%s\t%s\t%s\t%u\t%s\t%s\t%u\t%u\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%.6f\t%d\t%d\t%d\t%s\t%d\n",
		SCHEMA_VERSION, id->kernel_release, id->boot_id, mode_name(mode),
		profile_name(profile), phase, round, order, slot, RING_ENTRIES,
		BATCH_SIZE, batches, result->requested_ops, result->submitted_sqes,
		result->completed_cqes, result->successful_ops,
		result->validation_failures, result->duplicate_cqes,
		result->unexpected_cqes, result->bad_cqe_flags,
		result->sq_dropped, result->cq_overflow, result->outstanding,
		result->cleanup_failures, result->timed_ns, ns_per_op,
		sched_getcpu(), result->semantic_pass ? 1 : 0, expected,
		INPUT_SHA256, trace_enabled());
	fflush(out);
}

static int parse_profile(const char *text, enum profile_id *profile)
{
	if (!strcmp(text, "plain")) *profile = PROFILE_PLAIN;
	else if (!strcmp(text, "inject")) *profile = PROFILE_INJECT;
	else if (!strcmp(text, "formal")) *profile = PROFILE_FORMAL;
	else if (!strcmp(text, "v7-semantic")) *profile = PROFILE_V7_SEMANTIC;
	else return -1;
	return 0;
}

static int parse_mode(const char *text, enum run_mode *mode)
{
	if (!strcmp(text, "describe")) *mode = MODE_DESCRIBE;
	else if (!strcmp(text, "smoke")) *mode = MODE_SMOKE;
	else if (!strcmp(text, "trace")) *mode = MODE_TRACE;
	else if (!strcmp(text, "point")) *mode = MODE_POINT;
	else if (!strcmp(text, "semantic")) *mode = MODE_SEMANTIC;
	else return -1;
	return 0;
}

static int parse_options(int argc, char **argv, struct options *opts)
{
	int i;

	memset(opts, 0, sizeof(*opts));
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--profile") && i + 1 < argc) {
			if (parse_profile(argv[++i], &opts->profile)) return -1;
		} else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
			if (parse_mode(argv[++i], &opts->mode)) return -1;
		} else if (!strcmp(argv[i], "--output") && i + 1 < argc) {
			opts->output_path = argv[++i];
		} else if (!strcmp(argv[i], "--execute")) {
			opts->execute = true;
		} else return -1;
	}
	if (opts->profile == PROFILE_UNSET || opts->mode == MODE_UNSET)
		return -1;
	if (opts->mode == MODE_POINT && opts->profile != PROFILE_FORMAL)
		return -1;
	if ((opts->mode == MODE_TRACE || opts->mode == MODE_SMOKE) &&
	    opts->profile != PROFILE_PLAIN && opts->profile != PROFILE_INJECT)
		return -1;
	if (opts->mode == MODE_SEMANTIC &&
	    opts->profile != PROFILE_V7_SEMANTIC)
		return -1;
	return 0;
}

static int run_one_emit(struct raw_ring *ring, const struct identity *id,
			const struct options *opts, enum profile_id profile,
			FILE *out, unsigned int round, const char *order,
			const char *slot, uint64_t batches, bool timed,
			const char *phase)
{
	struct result result;
	int ret = run_sample(ring, profile, batches, timed, &result);

	emit_result(out, id, opts->mode, profile, phase, round, order, slot,
		    batches, &result);
	return ret;
}

static int run_formal(struct raw_ring *ring, const struct identity *id,
			 const struct options *opts, FILE *out)
{
	unsigned int round;

	for (round = 0; round < WARMUP_ROUNDS; round++)
		if (run_one_emit(ring, id, opts, PROFILE_PLAIN, out, round,
				 "P-I", "warmup-a", BATCHES_PER_SAMPLE, false,
				 "warmup") ||
		    run_one_emit(ring, id, opts, PROFILE_INJECT, out, round,
				 "P-I", "warmup-b", BATCHES_PER_SAMPLE, false,
				 "warmup"))
			return -1;
	for (round = 0; round < MEASURED_ROUNDS; round++) {
		enum profile_id seq[4] = { PROFILE_PLAIN, PROFILE_INJECT,
					   PROFILE_INJECT, PROFILE_PLAIN };
		const char *slots[4] = { "A", "B", "C", "D" };
		unsigned int i;

		if (round & 1U) {
			seq[0] = seq[3] = PROFILE_INJECT;
			seq[1] = seq[2] = PROFILE_PLAIN;
		}
		for (i = 0; i < 4; i++)
			if (run_one_emit(ring, id, opts, seq[i], out, round,
					 round & 1U ? "I-P-P-I" : "P-I-I-P",
					 slots[i], BATCHES_PER_SAMPLE, true,
					 "measured"))
				return -1;
	}
	return 0;
}

int main(int argc, char **argv)
{
	struct options opts;
	struct identity id;
	struct raw_ring ring;
	struct result result;
	struct utsname uts;
	FILE *out = stdout;
	int ret = 0;

	if (parse_options(argc, argv, &opts)) {
		fprintf(stderr, "usage: %s --profile plain|inject|formal|v7-semantic --mode describe|smoke|trace|point|semantic [--execute --output FILE]\n", argv[0]);
		return 2;
	}
	if (opts.mode == MODE_DESCRIBE) {
		printf("profile=%s\nring_entries=%u\nqd=%u\nbatches_per_sample=%u\n"
		       "ops_per_sample=%u\nwarmups=%u\nmeasured=%u\ncpu=%u\ninput_sha256=%s\n",
		       profile_name(opts.profile), RING_ENTRIES, BATCH_SIZE,
		       BATCHES_PER_SAMPLE, BATCHES_PER_SAMPLE * BATCH_SIZE,
		       WARMUP_ROUNDS, MEASURED_ROUNDS, CONTRACT_CPU, INPUT_SHA256);
		return 0;
	}
	if (!opts.execute) {
		fprintf(stderr, "refusing active run without --execute\n");
		return 2;
	}
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);
	if (pin_cpu()) {
		fprintf(stderr, "cannot pin to CPU %u\n", CONTRACT_CPU);
		return 1;
	}
	memset(&id, 0, sizeof(id));
	if (uname(&uts))
		return 1;
	strncpy(id.kernel_release, uts.release, sizeof(id.kernel_release) - 1U);
	if (read_text("/proc/sys/kernel/random/boot_id", id.boot_id,
		      sizeof(id.boot_id)))
		strcpy(id.boot_id, "unknown");
	if (opts.output_path) {
		bool need_header = access(opts.output_path, F_OK) != 0;

		out = fopen(opts.output_path, "ae");
		if (!out)
			return 1;
		if (need_header)
			fprintf(out, "%s\n", result_header);
	} else fprintf(out, "%s\n", result_header);

	if (opts.mode == MODE_SEMANTIC) {
		ret = run_v7_semantic(&result);
		emit_result(out, &id, opts.mode, PROFILE_V7_SEMANTIC,
			    "semantic-v7", 0, "U", "S", 0, &result);
	} else {
		if (raw_ring_init(&ring, 0)) {
			fprintf(stderr, "io_uring setup failed: %s\n", strerror(errno));
			ret = -1;
			goto done;
		}
		if (opts.mode == MODE_POINT)
			ret = run_formal(&ring, &id, &opts, out);
		else
			ret = run_one_emit(&ring, &id, &opts, opts.profile, out, 0,
					   "U", "S",
					   opts.mode == MODE_TRACE ? TRACE_BATCHES : SMOKE_BATCHES,
					   false,
					   opts.mode == MODE_TRACE ? "direct-hit" : "smoke");
		raw_ring_destroy(&ring);
	}
done:
	if (out != stdout)
		fclose(out);
	return ret ? 1 : 0;
}
