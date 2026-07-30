// SPDX-License-Identifier: GPL-2.0-or-later
/* Fixed raw-UAPI workloads for Linux v7.1.3 io_uring/futex.c. */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/futex.h>
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

#define SCHEMA_VERSION 1U
#define CONTRACT_CPU 2U
#define RING_ENTRIES 64U
#define SCALAR_SLOTS 32U
#define WAITV_GROUPS 8U
#define WAITV_WIDTH 8U
#define WAITV_WAKE_INDEX 3U
#define POINT_CYCLES 512U
#define SMOKE_CYCLES 2U
#define TRACE_CYCLES 16U
#define WARMUP_ROUNDS 3U
#define MEASURED_ROUNDS 15U
#define ENTER_TIMEOUT_SECONDS 2LL
#define FUTEX_UAPI_FLAGS (FUTEX2_SIZE_U32 | FUTEX2_PRIVATE)
#define INPUT_SHA256 \
	"52839acf8ee73d91f39cd75122d33a73d571161f7f93f6d64c90f6203e9f02dd"

#define TAG_KIND_SHIFT 56U
#define TAG_CYCLE_SHIFT 16U
#define TAG_INDEX_MASK UINT64_C(0xffff)

enum tag_kind {
	TAG_WAIT = 1,
	TAG_WAKE = 2,
	TAG_CANCEL = 3,
	TAG_CHECK = 4,
	TAG_NOP = 5,
};

enum profile_id {
	PROFILE_UNSET,
	PROFILE_S0_WAIT_WAKE,
	PROFILE_C0_WAIT_CANCEL,
	PROFILE_V0_WAITV_WAKE,
	PROFILE_Z0_ZERO_WAKE,
	PROFILE_E0_MISMATCH,
	PROFILE_N0_NOP,
	PROFILE_L0_LIFECYCLE,
};

enum mode_id {
	MODE_UNSET,
	MODE_DESCRIBE,
	MODE_SMOKE,
	MODE_TRACE,
	MODE_POINT,
};

enum phase_id {
	PHASE_SMOKE,
	PHASE_TRACE,
	PHASE_WARMUP,
	PHASE_MEASURED,
};

struct options {
	enum profile_id profile;
	enum mode_id mode;
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
	struct io_uring_cqe *cqes;
};

struct futex_cell {
	uint32_t value;
	unsigned char pad[60];
};

struct fixture {
	struct raw_ring ring;
	_Alignas(64) struct futex_cell cells[WAITV_GROUPS * WAITV_WIDTH];
	struct futex_waitv vectors[WAITV_GROUPS][WAITV_WIDTH];
};

struct result {
	uint64_t cycles;
	uint64_t pairs;
	uint64_t wait_submitted;
	uint64_t wake_submitted;
	uint64_t cancel_submitted;
	uint64_t nop_submitted;
	uint64_t wait_cqes;
	uint64_t wake_cqes;
	uint64_t cancel_cqes;
	uint64_t check_cqes;
	uint64_t nop_cqes;
	uint64_t bad_results;
	uint64_t bad_flags;
	uint64_t duplicate_cqes;
	uint64_t unexpected_cqes;
	uint64_t timeouts;
	uint64_t sq_dropped;
	uint64_t cq_overflow;
	uint64_t outstanding;
	uint64_t timed_ns;
	bool semantic_pass;
};

struct workload {
	char kernel_release[128];
	char boot_id[128];
};

static volatile sig_atomic_t stop_requested;

static const char result_header[] =
	"schema_version\tkernel_release\tboot_id\tmode\tprofile\tphase"
	"\tround\tring_entries\tslots\tvector_width\tcycles\tpairs"
	"\twait_submitted\twake_submitted\tcancel_submitted\tnop_submitted"
	"\twait_cqes\twake_cqes\tcancel_cqes\tcheck_cqes\tnop_cqes"
	"\tbad_results\tbad_flags\tduplicate_cqes\tunexpected_cqes"
	"\ttimeouts\tsq_dropped\tcq_overflow\toutstanding\ttimed_ns"
	"\tns_per_pair\tcpu\tsemantic_pass\tinput_sha256\ttrace_enabled";

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

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
		perror("clock_gettime");
		exit(EXIT_FAILURE);
	}
	return (uint64_t)ts.tv_sec * UINT64_C(1000000000) +
	       (uint64_t)ts.tv_nsec;
}

static int read_text(const char *path, char *buffer, size_t size)
{
	ssize_t nr;
	int fd;

	if (!size)
		return -1;
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	nr = read(fd, buffer, size - 1U);
	close(fd);
	if (nr < 0)
		return -1;
	buffer[nr] = '\0';
	while (nr > 0 && (buffer[nr - 1] == '\n' || buffer[nr - 1] == '\r'))
		buffer[--nr] = '\0';
	return 0;
}

static int pin_cpu(void)
{
	cpu_set_t set;
	long online = sysconf(_SC_NPROCESSORS_ONLN);

	if (online <= (long)CONTRACT_CPU)
		return -1;
	CPU_ZERO(&set);
	CPU_SET(CONTRACT_CPU, &set);
	if (sched_setaffinity(0, sizeof(set), &set) != 0) {
		perror("sched_setaffinity");
		return -1;
	}
	return sched_getcpu() == (int)CONTRACT_CPU ? 0 : -1;
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

static void raw_ring_unmap(struct raw_ring *ring)
{
	if (ring->sqes_ptr)
		munmap(ring->sqes_ptr, ring->sqes_sz);
	if (ring->sq_ptr)
		munmap(ring->sq_ptr, ring->sq_sz);
	if (!ring->single_mmap && ring->cq_ptr)
		munmap(ring->cq_ptr, ring->cq_sz);
	ring->sqes_ptr = ring->sq_ptr = ring->cq_ptr = NULL;
}

static void raw_ring_destroy(struct raw_ring *ring)
{
	raw_ring_unmap(ring);
	if (ring->fd >= 0)
		close(ring->fd);
	memset(ring, 0, sizeof(*ring));
	ring->fd = -1;
}

static int raw_ring_init(struct raw_ring *ring)
{
	struct io_uring_params p;
	size_t sq_sz, cq_sz, shared_sz;
	void *sq, *cq;

	memset(ring, 0, sizeof(*ring));
	ring->fd = -1;
	memset(&p, 0, sizeof(p));
	p.flags = IORING_SETUP_CQSIZE;
	p.cq_entries = RING_ENTRIES * 2U;
	ring->fd = (int)syscall(__NR_io_uring_setup, RING_ENTRIES, &p);
	if (ring->fd < 0) {
		perror("io_uring_setup");
		return -1;
	}
	sq_sz = p.sq_off.array + p.sq_entries * sizeof(unsigned int);
	cq_sz = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);
	ring->single_mmap = !!(p.features & IORING_FEAT_SINGLE_MMAP);
	if (ring->single_mmap) {
		shared_sz = sq_sz > cq_sz ? sq_sz : cq_sz;
		sq = mmap(NULL, shared_sz, PROT_READ | PROT_WRITE,
			  MAP_SHARED | MAP_POPULATE, ring->fd, IORING_OFF_SQ_RING);
		if (sq == MAP_FAILED)
			goto fail;
		cq = sq;
		ring->sq_sz = ring->cq_sz = shared_sz;
	} else {
		sq = mmap(NULL, sq_sz, PROT_READ | PROT_WRITE,
			  MAP_SHARED | MAP_POPULATE, ring->fd, IORING_OFF_SQ_RING);
		if (sq == MAP_FAILED)
			goto fail;
		ring->sq_ptr = sq;
		ring->sq_sz = sq_sz;
		cq = mmap(NULL, cq_sz, PROT_READ | PROT_WRITE,
			  MAP_SHARED | MAP_POPULATE, ring->fd, IORING_OFF_CQ_RING);
		if (cq == MAP_FAILED)
			goto fail;
		ring->cq_sz = cq_sz;
	}
	ring->sq_ptr = sq;
	ring->cq_ptr = cq;
	ring->sqes_sz = p.sq_entries * sizeof(struct io_uring_sqe);
	ring->sqes_ptr = mmap(NULL, ring->sqes_sz, PROT_READ | PROT_WRITE,
			       MAP_SHARED | MAP_POPULATE, ring->fd,
			       IORING_OFF_SQES);
	if (ring->sqes_ptr == MAP_FAILED) {
		ring->sqes_ptr = NULL;
		goto fail;
	}
	ring->sq_head = (unsigned int *)((char *)sq + p.sq_off.head);
	ring->sq_tail = (unsigned int *)((char *)sq + p.sq_off.tail);
	ring->sq_mask = (unsigned int *)((char *)sq + p.sq_off.ring_mask);
	ring->sq_entries = (unsigned int *)((char *)sq + p.sq_off.ring_entries);
	ring->sq_dropped = (unsigned int *)((char *)sq + p.sq_off.dropped);
	ring->sq_array = (unsigned int *)((char *)sq + p.sq_off.array);
	ring->sqes = ring->sqes_ptr;
	ring->cq_head = (unsigned int *)((char *)cq + p.cq_off.head);
	ring->cq_tail = (unsigned int *)((char *)cq + p.cq_off.tail);
	ring->cq_mask = (unsigned int *)((char *)cq + p.cq_off.ring_mask);
	ring->cq_entries = (unsigned int *)((char *)cq + p.cq_off.ring_entries);
	ring->cq_overflow = (unsigned int *)((char *)cq + p.cq_off.overflow);
	ring->cqes = (struct io_uring_cqe *)((char *)cq + p.cq_off.cqes);
	return 0;
fail:
	perror("io_uring mmap");
	raw_ring_destroy(ring);
	return -1;
}

static int probe_opcode(const struct raw_ring *ring, unsigned int opcode)
{
	struct io_uring_probe *probe;
	size_t bytes = sizeof(*probe) + 256U * sizeof(probe->ops[0]);
	unsigned int i;
	int ret = -1;

	probe = calloc(1, bytes);
	if (!probe)
		return -1;
	if (syscall(__NR_io_uring_register, ring->fd, IORING_REGISTER_PROBE,
		    probe, 256U) < 0)
		goto out;
	for (i = 0; i < probe->ops_len; i++) {
		if (probe->ops[i].op == opcode &&
		    (probe->ops[i].flags & IO_URING_OP_SUPPORTED)) {
			ret = 0;
			break;
		}
	}
out:
	free(probe);
	return ret;
}

static int capability_gate(const struct raw_ring *ring, enum profile_id profile)
{
	if (profile == PROFILE_N0_NOP || profile == PROFILE_L0_LIFECYCLE)
		return probe_opcode(ring, IORING_OP_NOP);
	if (profile == PROFILE_V0_WAITV_WAKE &&
	    probe_opcode(ring, IORING_OP_FUTEX_WAITV) != 0)
		return -1;
	if (profile == PROFILE_C0_WAIT_CANCEL &&
	    probe_opcode(ring, IORING_OP_ASYNC_CANCEL) != 0)
		return -1;
	if (profile != PROFILE_V0_WAITV_WAKE &&
	    probe_opcode(ring, IORING_OP_FUTEX_WAIT) != 0)
		return -1;
	return probe_opcode(ring, IORING_OP_FUTEX_WAKE);
}

static struct io_uring_sqe *queue_sqe(struct raw_ring *ring)
{
	unsigned int head = load_acquire(ring->sq_head);
	unsigned int tail = load_acquire(ring->sq_tail);
	unsigned int index;
	struct io_uring_sqe *sqe;

	if (tail - head >= *ring->sq_entries)
		return NULL;
	index = tail & *ring->sq_mask;
	sqe = &ring->sqes[index];
	memset(sqe, 0, sizeof(*sqe));
	ring->sq_array[index] = index;
	store_release(ring->sq_tail, tail + 1U);
	return sqe;
}

static int enter_ring(struct raw_ring *ring, unsigned int to_submit,
		      unsigned int min_complete, struct result *result)
{
	struct __kernel_timespec timeout;
	struct io_uring_getevents_arg arg;
	unsigned int flags = 0;
	void *argp = NULL;
	size_t argsz = 0;
	int ret;

	if (min_complete) {
		memset(&timeout, 0, sizeof(timeout));
		timeout.tv_sec = ENTER_TIMEOUT_SECONDS;
		memset(&arg, 0, sizeof(arg));
		arg.ts = (uintptr_t)&timeout;
		flags = IORING_ENTER_GETEVENTS | IORING_ENTER_EXT_ARG;
		argp = &arg;
		argsz = sizeof(arg);
	}
	do {
		ret = (int)syscall(__NR_io_uring_enter, ring->fd, to_submit,
				   min_complete, flags, argp, argsz);
	} while (ret < 0 && errno == EINTR && !stop_requested);
	if (ret < 0) {
		if (errno == ETIME)
			result->timeouts++;
		else
			perror("io_uring_enter");
		return -1;
	}
	if (to_submit && ret != (int)to_submit)
		return -1;
	return 0;
}

static int wait_and_copy(struct raw_ring *ring, unsigned int expected,
			 struct io_uring_cqe *copy, struct result *result)
{
	unsigned int available, head, i;

	available = cq_available(ring);
	if (available < expected &&
	    enter_ring(ring, 0, expected - available, result) != 0)
		return -1;
	available = cq_available(ring);
	head = load_acquire(ring->cq_head);
	if (available != expected) {
		result->unexpected_cqes += available > expected ?
			available - expected : expected - available;
		store_release(ring->cq_head, head + available);
		return -1;
	}
	for (i = 0; i < expected; i++)
		copy[i] = ring->cqes[(head + i) & *ring->cq_mask];
	store_release(ring->cq_head, head + expected);
	return 0;
}

static int submit_and_copy(struct raw_ring *ring, unsigned int submitted,
			   unsigned int expected, struct io_uring_cqe *copy,
			   struct result *result)
{
	if (enter_ring(ring, submitted, expected, result) != 0)
		return -1;
	return wait_and_copy(ring, expected, copy, result);
}

static uint64_t make_tag(enum tag_kind kind, unsigned int cycle,
			 unsigned int index)
{
	return (uint64_t)kind << TAG_KIND_SHIFT |
	       (uint64_t)cycle << TAG_CYCLE_SHIFT |
	       (uint64_t)index;
}

static enum tag_kind tag_kind(uint64_t tag)
{
	return (enum tag_kind)(tag >> TAG_KIND_SHIFT);
}

static unsigned int tag_index(uint64_t tag)
{
	return (unsigned int)(tag & TAG_INDEX_MASK);
}

static int queue_wait(struct raw_ring *ring, uint32_t *word,
		      uint32_t expected, uint64_t user_data)
{
	struct io_uring_sqe *sqe = queue_sqe(ring);

	if (!sqe)
		return -1;
	sqe->opcode = IORING_OP_FUTEX_WAIT;
	sqe->fd = FUTEX_UAPI_FLAGS;
	sqe->addr = (uintptr_t)word;
	sqe->addr2 = expected;
	sqe->addr3 = FUTEX_BITSET_MATCH_ANY;
	sqe->user_data = user_data;
	return 0;
}

static int queue_wake(struct raw_ring *ring, uint32_t *word,
		      uint32_t nr_wake, uint64_t user_data)
{
	struct io_uring_sqe *sqe = queue_sqe(ring);

	if (!sqe)
		return -1;
	sqe->opcode = IORING_OP_FUTEX_WAKE;
	sqe->fd = FUTEX_UAPI_FLAGS;
	sqe->addr = (uintptr_t)word;
	sqe->addr2 = nr_wake;
	sqe->addr3 = FUTEX_BITSET_MATCH_ANY;
	sqe->user_data = user_data;
	return 0;
}

static int queue_waitv(struct raw_ring *ring, struct futex_waitv *vector,
		       unsigned int width, uint64_t user_data)
{
	struct io_uring_sqe *sqe = queue_sqe(ring);

	if (!sqe)
		return -1;
	sqe->opcode = IORING_OP_FUTEX_WAITV;
	sqe->addr = (uintptr_t)vector;
	sqe->len = width;
	sqe->user_data = user_data;
	return 0;
}

static int queue_cancel(struct raw_ring *ring, uint64_t target,
			uint64_t user_data)
{
	struct io_uring_sqe *sqe = queue_sqe(ring);

	if (!sqe)
		return -1;
	sqe->opcode = IORING_OP_ASYNC_CANCEL;
	sqe->addr = target;
	sqe->cancel_flags = IORING_ASYNC_CANCEL_USERDATA;
	sqe->user_data = user_data;
	return 0;
}

static int queue_nop(struct raw_ring *ring, uint64_t user_data)
{
	struct io_uring_sqe *sqe = queue_sqe(ring);

	if (!sqe)
		return -1;
	sqe->opcode = IORING_OP_NOP;
	sqe->user_data = user_data;
	return 0;
}

static int fixture_init(struct fixture *fixture)
{
	unsigned int group, index;

	memset(fixture, 0, sizeof(*fixture));
	if (raw_ring_init(&fixture->ring) != 0)
		return -1;
	for (group = 0; group < WAITV_GROUPS; group++) {
		for (index = 0; index < WAITV_WIDTH; index++) {
			struct futex_waitv *item = &fixture->vectors[group][index];
			unsigned int cell = group * WAITV_WIDTH + index;

			item->val = 0;
			item->uaddr = (uintptr_t)&fixture->cells[cell].value;
			item->flags = FUTEX_UAPI_FLAGS;
			item->__reserved = 0;
		}
	}
	return 0;
}

static void fixture_destroy(struct fixture *fixture)
{
	raw_ring_destroy(&fixture->ring);
}

static void count_cqe(struct result *result, enum tag_kind kind)
{
	switch (kind) {
	case TAG_WAIT: result->wait_cqes++; break;
	case TAG_WAKE: result->wake_cqes++; break;
	case TAG_CANCEL: result->cancel_cqes++; break;
	case TAG_CHECK: result->check_cqes++; break;
	case TAG_NOP: result->nop_cqes++; break;
	default: result->unexpected_cqes++; break;
	}
}

static int validate_pair_cqes(const struct io_uring_cqe *cqes,
			      unsigned int nr, unsigned int slots,
			      int wait_result, int second_result,
			      enum tag_kind second_kind,
			      struct result *result)
{
	bool seen_wait[SCALAR_SLOTS] = { false };
	bool seen_second[SCALAR_SLOTS] = { false };
	unsigned int i;

	if (slots > SCALAR_SLOTS)
		return -1;
	for (i = 0; i < nr; i++) {
		enum tag_kind kind = tag_kind(cqes[i].user_data);
		unsigned int index = tag_index(cqes[i].user_data);
		bool *seen;
		int expected;

		count_cqe(result, kind);
		if (cqes[i].flags)
			result->bad_flags++;
		if (index >= slots || (kind != TAG_WAIT && kind != second_kind)) {
			result->unexpected_cqes++;
			continue;
		}
		seen = kind == TAG_WAIT ? &seen_wait[index] : &seen_second[index];
		expected = kind == TAG_WAIT ? wait_result : second_result;
		if (*seen)
			result->duplicate_cqes++;
		*seen = true;
		if (cqes[i].res != expected)
			result->bad_results++;
	}
	for (i = 0; i < slots; i++) {
		if (!seen_wait[i] || !seen_second[i])
			result->unexpected_cqes++;
	}
	return result->bad_results || result->bad_flags ||
	       result->duplicate_cqes || result->unexpected_cqes ? -1 : 0;
}

static int validate_single_cqes(const struct io_uring_cqe *cqes,
				unsigned int nr, unsigned int slots,
				enum tag_kind kind, int expected,
				struct result *result)
{
	bool seen[SCALAR_SLOTS] = { false };
	unsigned int i;

	for (i = 0; i < nr; i++) {
		unsigned int index = tag_index(cqes[i].user_data);

		count_cqe(result, tag_kind(cqes[i].user_data));
		if (cqes[i].flags)
			result->bad_flags++;
		if (tag_kind(cqes[i].user_data) != kind || index >= slots) {
			result->unexpected_cqes++;
			continue;
		}
		if (seen[index])
			result->duplicate_cqes++;
		seen[index] = true;
		if (cqes[i].res != expected)
			result->bad_results++;
	}
	for (i = 0; i < slots; i++)
		if (!seen[i])
			result->unexpected_cqes++;
	return result->bad_results || result->bad_flags ||
	       result->duplicate_cqes || result->unexpected_cqes ? -1 : 0;
}

static int run_scalar_pair_cycle(struct fixture *fixture,
				 enum profile_id profile,
				 unsigned int cycle, bool timed,
				 struct result *result)
{
	struct io_uring_cqe cqes[SCALAR_SLOTS * 2U];
	uint64_t wait_tags[SCALAR_SLOTS];
	uint64_t start = 0;
	unsigned int i;
	int rc;

	for (i = 0; i < SCALAR_SLOTS; i++)
		fixture->cells[i].value = 0;
	if (timed)
		start = now_ns();
	for (i = 0; i < SCALAR_SLOTS; i++) {
		wait_tags[i] = make_tag(TAG_WAIT, cycle, i);
		if (queue_wait(&fixture->ring, &fixture->cells[i].value, 0,
			       wait_tags[i]) != 0)
			return -1;
	}
	if (enter_ring(&fixture->ring, SCALAR_SLOTS, 0, result) != 0)
		return -1;
	for (i = 0; i < SCALAR_SLOTS; i++) {
		if (profile == PROFILE_S0_WAIT_WAKE) {
			if (queue_wake(&fixture->ring, &fixture->cells[i].value, 1,
				       make_tag(TAG_WAKE, cycle, i)) != 0)
				return -1;
		} else if (queue_cancel(&fixture->ring, wait_tags[i],
					make_tag(TAG_CANCEL, cycle, i)) != 0) {
			return -1;
		}
	}
	if (submit_and_copy(&fixture->ring, SCALAR_SLOTS,
			    SCALAR_SLOTS * 2U, cqes, result) != 0)
		return -1;
	if (timed)
		result->timed_ns += now_ns() - start;
	result->wait_submitted += SCALAR_SLOTS;
	if (profile == PROFILE_S0_WAIT_WAKE)
		result->wake_submitted += SCALAR_SLOTS;
	else
		result->cancel_submitted += SCALAR_SLOTS;
	rc = validate_pair_cqes(cqes, SCALAR_SLOTS * 2U, SCALAR_SLOTS,
				profile == PROFILE_S0_WAIT_WAKE ? 0 : -ECANCELED,
				1, profile == PROFILE_S0_WAIT_WAKE ?
				TAG_WAKE : TAG_CANCEL, result);
	result->cycles++;
	result->pairs += SCALAR_SLOTS;
	return rc;
}

static int run_waitv_cycle(struct fixture *fixture, unsigned int cycle,
			   bool timed, struct result *result)
{
	struct io_uring_cqe cqes[WAITV_GROUPS * 2U];
	struct io_uring_cqe checks[WAITV_GROUPS];
	uint64_t start = 0;
	unsigned int group, index;

	for (group = 0; group < WAITV_GROUPS; group++)
		for (index = 0; index < WAITV_WIDTH; index++)
			fixture->cells[group * WAITV_WIDTH + index].value = 0;
	if (timed)
		start = now_ns();
	for (group = 0; group < WAITV_GROUPS; group++) {
		if (queue_waitv(&fixture->ring, fixture->vectors[group], WAITV_WIDTH,
				make_tag(TAG_WAIT, cycle, group)) != 0)
			return -1;
	}
	if (enter_ring(&fixture->ring, WAITV_GROUPS, 0, result) != 0)
		return -1;
	for (group = 0; group < WAITV_GROUPS; group++) {
		unsigned int cell = group * WAITV_WIDTH + WAITV_WAKE_INDEX;
		if (queue_wake(&fixture->ring, &fixture->cells[cell].value, 1,
			       make_tag(TAG_WAKE, cycle, group)) != 0)
			return -1;
	}
	if (submit_and_copy(&fixture->ring, WAITV_GROUPS,
			    WAITV_GROUPS * 2U, cqes, result) != 0)
		return -1;
	if (timed)
		result->timed_ns += now_ns() - start;
	result->wait_submitted += WAITV_GROUPS;
	result->wake_submitted += WAITV_GROUPS;
	if (validate_pair_cqes(cqes, WAITV_GROUPS * 2U, WAITV_GROUPS,
				WAITV_WAKE_INDEX, 1, TAG_WAKE, result) != 0)
		return -1;

	/* Prove that completion removed the seven residual waiters per vector. */
	for (group = 0; group < WAITV_GROUPS; group++) {
		unsigned int cell = group * WAITV_WIDTH;
		if (queue_wake(&fixture->ring, &fixture->cells[cell].value, 1,
			       make_tag(TAG_CHECK, cycle, group)) != 0)
			return -1;
	}
	if (submit_and_copy(&fixture->ring, WAITV_GROUPS, WAITV_GROUPS,
			    checks, result) != 0)
		return -1;
	if (validate_single_cqes(checks, WAITV_GROUPS, WAITV_GROUPS,
				  TAG_CHECK, 0, result) != 0)
		return -1;
	result->wake_submitted += WAITV_GROUPS;
	result->cycles++;
	result->pairs += WAITV_GROUPS;
	return 0;
}

static int run_single_cycle(struct fixture *fixture, enum profile_id profile,
			    unsigned int cycle, bool timed,
			    struct result *result)
{
	struct io_uring_cqe cqes[SCALAR_SLOTS];
	uint64_t start = 0;
	unsigned int i;
	enum tag_kind kind;
	int expected;

	if (timed)
		start = now_ns();
	for (i = 0; i < SCALAR_SLOTS; i++) {
		if (profile == PROFILE_Z0_ZERO_WAKE) {
			fixture->cells[i].value = 0;
			kind = TAG_WAKE;
			expected = 0;
			if (queue_wake(&fixture->ring, &fixture->cells[i].value, 1,
				       make_tag(kind, cycle, i)) != 0)
				return -1;
		} else if (profile == PROFILE_E0_MISMATCH) {
			fixture->cells[i].value = 1;
			kind = TAG_WAIT;
			expected = -EAGAIN;
			if (queue_wait(&fixture->ring, &fixture->cells[i].value, 0,
				       make_tag(kind, cycle, i)) != 0)
				return -1;
		} else {
			kind = TAG_NOP;
			expected = 0;
			if (queue_nop(&fixture->ring, make_tag(kind, cycle, i)) != 0)
				return -1;
		}
	}
	if (submit_and_copy(&fixture->ring, SCALAR_SLOTS, SCALAR_SLOTS,
			    cqes, result) != 0)
		return -1;
	if (timed)
		result->timed_ns += now_ns() - start;
	if (profile == PROFILE_Z0_ZERO_WAKE)
		result->wake_submitted += SCALAR_SLOTS;
	else if (profile == PROFILE_E0_MISMATCH)
		result->wait_submitted += SCALAR_SLOTS;
	else
		result->nop_submitted += SCALAR_SLOTS;
	if (validate_single_cqes(cqes, SCALAR_SLOTS, SCALAR_SLOTS,
				  kind, expected, result) != 0)
		return -1;
	result->cycles++;
	result->pairs += SCALAR_SLOTS;
	return 0;
}

static int run_lifecycle(struct result *result)
{
	struct fixture fixture;
	struct result inner;

	memset(&inner, 0, sizeof(inner));
	if (fixture_init(&fixture) != 0)
		return -1;
	if (probe_opcode(&fixture.ring, IORING_OP_FUTEX_WAIT) != 0 ||
	    probe_opcode(&fixture.ring, IORING_OP_FUTEX_WAKE) != 0 ||
	    probe_opcode(&fixture.ring, IORING_OP_FUTEX_WAITV) != 0 ||
	    run_scalar_pair_cycle(&fixture, PROFILE_S0_WAIT_WAKE, 0,
				  false, &inner) != 0) {
		fixture_destroy(&fixture);
		return -1;
	}
	/* Closing a populated cache must exercise io_futex_cache_free(). */
	fixture_destroy(&fixture);
	/* Ring teardown is deferred; keep the traced process alive briefly. */
	usleep(200000);
	memset(result, 0, sizeof(*result));
	result->cycles = 1;
	result->pairs = 1;
	result->semantic_pass = true;
	return 0;
}

static int run_profile_round(struct fixture *fixture, enum profile_id profile,
			     unsigned int base_cycle, unsigned int cycles,
			     bool timed, struct result *result)
{
	unsigned int cycle;

	memset(result, 0, sizeof(*result));
	for (cycle = 0; cycle < cycles; cycle++) {
		int rc;

		if (profile == PROFILE_S0_WAIT_WAKE ||
		    profile == PROFILE_C0_WAIT_CANCEL)
			rc = run_scalar_pair_cycle(fixture, profile,
						   base_cycle + cycle, timed, result);
		else if (profile == PROFILE_V0_WAITV_WAKE)
			rc = run_waitv_cycle(fixture, base_cycle + cycle, timed, result);
		else
			rc = run_single_cycle(fixture, profile,
					      base_cycle + cycle, timed, result);
		if (rc != 0 || stop_requested)
			return -1;
	}
	result->sq_dropped = *fixture->ring.sq_dropped;
	result->cq_overflow = *fixture->ring.cq_overflow;
	result->outstanding = ring_outstanding(&fixture->ring);
	result->semantic_pass = !result->bad_results && !result->bad_flags &&
		!result->duplicate_cqes && !result->unexpected_cqes &&
		!result->timeouts && !result->sq_dropped &&
		!result->cq_overflow && !result->outstanding;
	return result->semantic_pass ? 0 : -1;
}

static const char *profile_name(enum profile_id profile)
{
	switch (profile) {
	case PROFILE_S0_WAIT_WAKE: return "s0_wait_wake";
	case PROFILE_C0_WAIT_CANCEL: return "c0_wait_cancel";
	case PROFILE_V0_WAITV_WAKE: return "v0_waitv_wake";
	case PROFILE_Z0_ZERO_WAKE: return "z0_zero_wake";
	case PROFILE_E0_MISMATCH: return "e0_mismatch";
	case PROFILE_N0_NOP: return "n0_nop";
	case PROFILE_L0_LIFECYCLE: return "l0_lifecycle";
	default: return "unset";
	}
}

static const char *mode_name(enum mode_id mode)
{
	switch (mode) {
	case MODE_DESCRIBE: return "describe";
	case MODE_SMOKE: return "smoke";
	case MODE_TRACE: return "trace";
	case MODE_POINT: return "point";
	default: return "unset";
	}
}

static const char *phase_name(enum phase_id phase)
{
	switch (phase) {
	case PHASE_SMOKE: return "smoke";
	case PHASE_TRACE: return "trace";
	case PHASE_WARMUP: return "warmup";
	case PHASE_MEASURED: return "measured";
	default: return "unknown";
	}
}

static void write_result(FILE *output, const struct workload *workload,
			 const struct options *options, enum phase_id phase,
			 unsigned int round, const struct result *result)
{
	double ns_per_pair = result->pairs ?
		(double)result->timed_ns / (double)result->pairs : 0.0;
	unsigned int width = options->profile == PROFILE_V0_WAITV_WAKE ?
		WAITV_WIDTH : 1U;
	unsigned int slots = options->profile == PROFILE_V0_WAITV_WAKE ?
		WAITV_GROUPS : SCALAR_SLOTS;

	fprintf(output,
		"%u\t%s\t%s\t%s\t%s\t%s\t%u\t%u\t%u\t%u"
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%.6f\t%u\t%s\t%s\t%d\n",
		SCHEMA_VERSION, workload->kernel_release, workload->boot_id,
		mode_name(options->mode), profile_name(options->profile),
		phase_name(phase), round, RING_ENTRIES, slots, width,
		result->cycles, result->pairs, result->wait_submitted,
		result->wake_submitted, result->cancel_submitted,
		result->nop_submitted, result->wait_cqes, result->wake_cqes,
		result->cancel_cqes, result->check_cqes, result->nop_cqes,
		result->bad_results, result->bad_flags, result->duplicate_cqes,
		result->unexpected_cqes, result->timeouts, result->sq_dropped,
		result->cq_overflow, result->outstanding, result->timed_ns,
		ns_per_pair, CONTRACT_CPU, result->semantic_pass ? "1" : "0",
		INPUT_SHA256, options->mode == MODE_TRACE ? 1 : 0);
	fflush(output);
}

static int parse_profile(const char *text, enum profile_id *profile)
{
	if (!strcmp(text, "s0_wait_wake")) *profile = PROFILE_S0_WAIT_WAKE;
	else if (!strcmp(text, "c0_wait_cancel")) *profile = PROFILE_C0_WAIT_CANCEL;
	else if (!strcmp(text, "v0_waitv_wake")) *profile = PROFILE_V0_WAITV_WAKE;
	else if (!strcmp(text, "z0_zero_wake")) *profile = PROFILE_Z0_ZERO_WAKE;
	else if (!strcmp(text, "e0_mismatch")) *profile = PROFILE_E0_MISMATCH;
	else if (!strcmp(text, "n0_nop")) *profile = PROFILE_N0_NOP;
	else if (!strcmp(text, "l0_lifecycle")) *profile = PROFILE_L0_LIFECYCLE;
	else return -1;
	return 0;
}

static int parse_mode(const char *text, enum mode_id *mode)
{
	if (!strcmp(text, "describe")) *mode = MODE_DESCRIBE;
	else if (!strcmp(text, "smoke")) *mode = MODE_SMOKE;
	else if (!strcmp(text, "trace")) *mode = MODE_TRACE;
	else if (!strcmp(text, "point")) *mode = MODE_POINT;
	else return -1;
	return 0;
}

static void usage(const char *program)
{
	fprintf(stderr,
		"usage: %s --profile s0_wait_wake|c0_wait_cancel|v0_waitv_wake|"
		"z0_zero_wake|e0_mismatch|n0_nop|l0_lifecycle "
		"--mode describe|smoke|trace|point [--output FILE] [--execute]\n",
		program);
}

static int parse_options(int argc, char **argv, struct options *options)
{
	int i;

	memset(options, 0, sizeof(*options));
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--profile") && i + 1 < argc) {
			if (parse_profile(argv[++i], &options->profile) != 0)
				return -1;
		} else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
			if (parse_mode(argv[++i], &options->mode) != 0)
				return -1;
		} else if (!strcmp(argv[i], "--output") && i + 1 < argc) {
			options->output_path = argv[++i];
		} else if (!strcmp(argv[i], "--execute")) {
			options->execute = true;
		} else {
			return -1;
		}
	}
	if (options->profile == PROFILE_UNSET || options->mode == MODE_UNSET)
		return -1;
	if (options->mode != MODE_DESCRIBE &&
	    (!options->execute || !options->output_path))
		return -1;
	if (options->mode == MODE_POINT &&
	    options->profile != PROFILE_S0_WAIT_WAKE &&
	    options->profile != PROFILE_C0_WAIT_CANCEL &&
	    options->profile != PROFILE_V0_WAITV_WAKE)
		return -1;
	return 0;
}

static void describe(const struct options *options)
{
	printf("{\"profile\":\"%s\",\"ring_entries\":%u,"
	       "\"scalar_slots\":%u,\"waitv_groups\":%u,"
	       "\"waitv_width\":%u,\"point_cycles\":%u,"
	       "\"warmups\":%u,\"measured_rounds\":%u,"
	       "\"cpu\":%u,\"input_sha256\":\"%s\"}\n",
	       profile_name(options->profile), RING_ENTRIES, SCALAR_SLOTS,
	       WAITV_GROUPS, WAITV_WIDTH, POINT_CYCLES, WARMUP_ROUNDS,
	       MEASURED_ROUNDS, CONTRACT_CPU, INPUT_SHA256);
}

int main(int argc, char **argv)
{
	struct options options;
	struct workload workload;
	struct fixture fixture;
	struct result result;
	struct utsname uts;
	FILE *output = NULL;
	unsigned int round, cycles;
	int ret = EXIT_FAILURE;

	if (parse_options(argc, argv, &options) != 0) {
		usage(argv[0]);
		return 2;
	}
	if (options.mode == MODE_DESCRIBE) {
		describe(&options);
		return 0;
	}
	if (signal(SIGINT, handle_signal) == SIG_ERR ||
	    signal(SIGTERM, handle_signal) == SIG_ERR || pin_cpu() != 0)
		return EXIT_FAILURE;
	memset(&workload, 0, sizeof(workload));
	if (uname(&uts) != 0)
		return EXIT_FAILURE;
	strncpy(workload.kernel_release, uts.release,
		sizeof(workload.kernel_release) - 1U);
	if (read_text("/proc/sys/kernel/random/boot_id", workload.boot_id,
		      sizeof(workload.boot_id)) != 0)
		strcpy(workload.boot_id, "unknown");
	output = fopen(options.output_path, "wx");
	if (!output) {
		perror("fopen output");
		return EXIT_FAILURE;
	}
	fprintf(output, "%s\n", result_header);

	if (options.profile == PROFILE_L0_LIFECYCLE) {
		memset(&result, 0, sizeof(result));
		if (run_lifecycle(&result) != 0)
			goto out;
		write_result(output, &workload, &options,
			     options.mode == MODE_TRACE ? PHASE_TRACE : PHASE_SMOKE,
			     0, &result);
		ret = EXIT_SUCCESS;
		goto out;
	}
	if (fixture_init(&fixture) != 0)
		goto out;
	if (capability_gate(&fixture.ring, options.profile) != 0) {
		fprintf(stderr, "required io_uring opcode is unsupported\n");
		goto destroy;
	}
	cycles = options.mode == MODE_POINT ? POINT_CYCLES :
		(options.mode == MODE_TRACE ? TRACE_CYCLES : SMOKE_CYCLES);
	if (options.mode == MODE_SMOKE || options.mode == MODE_TRACE) {
		if (run_profile_round(&fixture, options.profile, 0, cycles,
				      false, &result) != 0)
			goto destroy;
		write_result(output, &workload, &options,
			     options.mode == MODE_TRACE ? PHASE_TRACE : PHASE_SMOKE,
			     0, &result);
		ret = EXIT_SUCCESS;
		goto destroy;
	}
	for (round = 0; round < WARMUP_ROUNDS; round++) {
		if (run_profile_round(&fixture, options.profile,
				      round * cycles, cycles, false, &result) != 0)
			goto destroy;
	}
	for (round = 0; round < MEASURED_ROUNDS; round++) {
		if (run_profile_round(&fixture, options.profile,
				      (WARMUP_ROUNDS + round) * cycles,
				      cycles, true, &result) != 0)
			goto destroy;
		write_result(output, &workload, &options, PHASE_MEASURED,
			     round, &result);
	}
	ret = EXIT_SUCCESS;
destroy:
	fixture_destroy(&fixture);
out:
	if (output && fclose(output) != 0)
		ret = EXIT_FAILURE;
	return ret;
}
