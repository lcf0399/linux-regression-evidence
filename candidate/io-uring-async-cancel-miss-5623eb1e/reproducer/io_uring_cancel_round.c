// SPDX-License-Identifier: GPL-2.0-or-later
/* Fixed raw-UAPI workloads for Linux v7.1.3 io_uring/cancel.c. */

#define _GNU_SOURCE

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/io_uring.h>
#include <poll.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/eventfd.h>
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
#define RING_ENTRIES 256U
#define PENDING 32U
#define DECOYS 8U
#define POINT_BATCHES 64U
#define SMOKE_BATCHES 1U
#define TRACE_BATCHES 4U
#define S0_POINT_CYCLES 512U
#define S0_SMOKE_CYCLES 8U
#define S0_TRACE_CYCLES 32U
#define WARMUP_ROUNDS 3U
#define MEASURED_ROUNDS 15U
#define ENTER_TIMEOUT_SECONDS 2LL
#define RELEASE_LIMIT_NS UINT64_C(2000000000)
#define INPUT_SHA256 \
	"69835c953fd54a06a3946e91dc04c7532d336bf07abeae87cbbc0e6f4e0b4860"

#define KIND_POLL UINT64_C(1)
#define KIND_CANCEL UINT64_C(2)
#define KIND_DECOY UINT64_C(3)
#define KIND_CLEANUP UINT64_C(4)
#define KIND_BULK UINT64_C(5)
#define KIND_NEVER UINT64_C(15)
#define KIND_SHIFT 60U
#define PROFILE_SHIFT 56U
#define ROUND_SHIFT 44U
#define BATCH_SHIFT 24U

_Static_assert(sizeof(struct io_uring_sqe) == 64, "unexpected SQE ABI");
_Static_assert(sizeof(struct io_uring_cqe) == 16, "unexpected CQE ABI");
_Static_assert(sizeof(struct io_uring_sync_cancel_reg) == 64,
	       "unexpected sync-cancel ABI");
_Static_assert(PENDING <= 64U, "bit masks require at most 64 requests");
_Static_assert(DECOYS <= 64U, "bit masks require at most 64 decoys");

enum profile_id {
	PROFILE_UNSET,
	PROFILE_A0_HIT,
	PROFILE_A0_MISS,
	PROFILE_F0_FD_ALL,
	PROFILE_S0_SYNC,
	PROFILE_P0_PREP,
	PROFILE_R0_RELEASE,
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
	unsigned int *sq_flags;
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

struct fixture {
	struct raw_ring ring;
	int target_fd;
	int decoy_fd;
};

struct result {
	uint64_t batches;
	uint64_t attempts;
	uint64_t logical_cancels;
	uint64_t poll_submitted;
	uint64_t cancel_submitted;
	uint64_t cancel_ok;
	uint64_t cancel_enoent;
	uint64_t cancel_other;
	uint64_t original_cqes;
	uint64_t cancel_cqes;
	uint64_t decoy_cqes;
	uint64_t pending_proofs;
	uint64_t unexpected_cqes;
	uint64_t duplicate_cqes;
	uint64_t bad_results;
	uint64_t bad_flags;
	uint64_t eventfd_state_failures;
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
	"\tround\tring_entries\tpending\tbatches\tattempts"
	"\tlogical_cancels\tpoll_submitted\tcancel_submitted"
	"\tcancel_ok\tcancel_enoent\tcancel_other\toriginal_cqes"
	"\tcancel_cqes\tdecoy_cqes\tpending_proofs\tunexpected_cqes"
	"\tduplicate_cqes\tbad_results\tbad_flags"
	"\teventfd_state_failures\ttimeouts\tsq_dropped\tcq_overflow"
	"\toutstanding\ttimed_ns\tns_per_attempt\tcpu\tsemantic_pass"
	"\tinput_sha256\ttrace_enabled";

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
	int fd;
	ssize_t nr;

	if (size == 0)
		return -1;
	fd = open(path, O_RDONLY | O_CLOEXEC);
	if (fd < 0)
		return -1;
	nr = read(fd, buffer, size - 1);
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

	if (online <= (long)CONTRACT_CPU) {
		fprintf(stderr, "CPU %u unavailable (online=%ld)\n",
			CONTRACT_CPU, online);
		return -1;
	}
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
			  load_acquire(ring->sq_head)) +
	       (uint64_t)cq_available(ring);
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
		if (sq == MAP_FAILED) {
			perror("mmap shared SQ/CQ");
			goto fail;
		}
		cq = sq;
		ring->sq_sz = ring->cq_sz = shared_sz;
	} else {
		sq = mmap(NULL, sq_sz, PROT_READ | PROT_WRITE,
			  MAP_SHARED | MAP_POPULATE, ring->fd, IORING_OFF_SQ_RING);
		if (sq == MAP_FAILED) {
			perror("mmap SQ");
			goto fail;
		}
		ring->sq_ptr = sq;
		ring->sq_sz = sq_sz;
		cq = mmap(NULL, cq_sz, PROT_READ | PROT_WRITE,
			  MAP_SHARED | MAP_POPULATE, ring->fd, IORING_OFF_CQ_RING);
		if (cq == MAP_FAILED) {
			perror("mmap CQ");
			goto fail;
		}
		ring->cq_sz = cq_sz;
	}
	ring->sq_ptr = sq;
	ring->cq_ptr = cq;
	ring->sqes_sz = p.sq_entries * sizeof(struct io_uring_sqe);
	ring->sqes_ptr = mmap(NULL, ring->sqes_sz, PROT_READ | PROT_WRITE,
			       MAP_SHARED | MAP_POPULATE, ring->fd,
			       IORING_OFF_SQES);
	if (ring->sqes_ptr == MAP_FAILED) {
		perror("mmap SQEs");
		ring->sqes_ptr = NULL;
		goto fail;
	}
	ring->sq_head = (unsigned int *)((char *)sq + p.sq_off.head);
	ring->sq_tail = (unsigned int *)((char *)sq + p.sq_off.tail);
	ring->sq_mask = (unsigned int *)((char *)sq + p.sq_off.ring_mask);
	ring->sq_entries = (unsigned int *)((char *)sq + p.sq_off.ring_entries);
	ring->sq_flags = (unsigned int *)((char *)sq + p.sq_off.flags);
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
	raw_ring_destroy(ring);
	return -1;
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
	if (to_submit && ret != (int)to_submit) {
		fprintf(stderr, "submitted=%d expected=%u\n", ret, to_submit);
		return -1;
	}
	return 0;
}

static int wait_and_copy(struct raw_ring *ring, unsigned int expected,
			 struct io_uring_cqe *copy, struct result *result)
{
	unsigned int available, head, i;

	available = cq_available(ring);
	if (available < expected && enter_ring(ring, 0, expected - available,
					      result) != 0)
		return -1;
	available = cq_available(ring);
	head = load_acquire(ring->cq_head);
	if (available != expected) {
		result->unexpected_cqes += available > expected ?
			available - expected : expected - available;
		for (i = 0; i < available; i++)
			(void)ring->cqes[(head + i) & *ring->cq_mask];
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

static uint64_t make_tag(uint64_t kind, enum profile_id profile,
			 unsigned int round, unsigned int batch,
			 unsigned int index)
{
	return kind << KIND_SHIFT |
	       (uint64_t)profile << PROFILE_SHIFT |
	       (uint64_t)(round & 0xfffU) << ROUND_SHIFT |
	       (uint64_t)(batch & 0xfffffU) << BATCH_SHIFT |
	       (uint64_t)index;
}

static int queue_poll(struct raw_ring *ring, int fd, uint64_t user_data)
{
	struct io_uring_sqe *sqe = queue_sqe(ring);

	if (!sqe)
		return -1;
	sqe->opcode = IORING_OP_POLL_ADD;
	sqe->fd = fd;
	sqe->poll32_events = POLLIN;
	sqe->user_data = user_data;
	return 0;
}

static int queue_cancel_key(struct raw_ring *ring, uint64_t target,
			    uint64_t user_data)
{
	struct io_uring_sqe *sqe = queue_sqe(ring);

	if (!sqe)
		return -1;
	sqe->opcode = IORING_OP_ASYNC_CANCEL;
	sqe->fd = -1;
	sqe->addr = target;
	sqe->cancel_flags = 0;
	sqe->user_data = user_data;
	return 0;
}

static int queue_cancel_fd_all(struct raw_ring *ring, int fd,
			       uint64_t user_data)
{
	struct io_uring_sqe *sqe = queue_sqe(ring);

	if (!sqe)
		return -1;
	sqe->opcode = IORING_OP_ASYNC_CANCEL;
	sqe->fd = fd;
	sqe->cancel_flags = IORING_ASYNC_CANCEL_FD |
				    IORING_ASYNC_CANCEL_ALL;
	sqe->user_data = user_data;
	return 0;
}

static int eventfd_is_unsignaled(int fd)
{
	uint64_t value;
	ssize_t nr;

	errno = 0;
	nr = read(fd, &value, sizeof(value));
	return nr < 0 && errno == EAGAIN ? 0 : -1;
}

static int fixture_init(struct fixture *fixture)
{
	memset(fixture, 0, sizeof(*fixture));
	fixture->ring.fd = -1;
	fixture->target_fd = fixture->decoy_fd = -1;
	if (raw_ring_init(&fixture->ring) != 0)
		return -1;
	fixture->target_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	fixture->decoy_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (fixture->target_fd < 0 || fixture->decoy_fd < 0) {
		perror("eventfd");
		return -1;
	}
	return 0;
}

static void fixture_destroy(struct fixture *fixture)
{
	if (fixture->target_fd >= 0)
		close(fixture->target_fd);
	if (fixture->decoy_fd >= 0)
		close(fixture->decoy_fd);
	raw_ring_destroy(&fixture->ring);
	fixture->target_fd = fixture->decoy_fd = -1;
}

static int prove_pending(struct fixture *fixture, struct result *result,
			 bool check_decoy)
{
	if (cq_available(&fixture->ring) != 0 ||
	    eventfd_is_unsignaled(fixture->target_fd) != 0 ||
	    (check_decoy && eventfd_is_unsignaled(fixture->decoy_fd) != 0)) {
		result->eventfd_state_failures++;
		return -1;
	}
	result->pending_proofs++;
	return 0;
}

static void observe_cqe_common(const struct io_uring_cqe *cqe,
			       struct result *result)
{
	if (cqe->flags != 0)
		result->bad_flags++;
}

static int validate_keyed(const struct io_uring_cqe *cqes,
			  unsigned int count, enum profile_id profile,
			  unsigned int round, unsigned int batch,
			  bool miss, struct result *result)
{
	uint64_t poll_seen = 0, cancel_seen = 0;
	unsigned int i, index;

	for (i = 0; i < count; i++) {
		bool found = false;

		observe_cqe_common(&cqes[i], result);
		for (index = 0; index < PENDING; index++) {
			uint64_t poll = make_tag(KIND_POLL, profile, round,
						 batch, index);
			uint64_t cancel = make_tag(KIND_CANCEL, profile, round,
						   batch, index);

			if (!miss && cqes[i].user_data == poll) {
				found = true;
				if (poll_seen & (UINT64_C(1) << index))
					result->duplicate_cqes++;
				poll_seen |= UINT64_C(1) << index;
				result->original_cqes++;
				if (cqes[i].res != -ECANCELED)
					result->bad_results++;
				break;
			}
			if (cqes[i].user_data == cancel) {
				found = true;
				if (cancel_seen & (UINT64_C(1) << index))
					result->duplicate_cqes++;
				cancel_seen |= UINT64_C(1) << index;
				result->cancel_cqes++;
				if (miss && cqes[i].res == -ENOENT)
					result->cancel_enoent++;
				else if (!miss && cqes[i].res == 0)
					result->cancel_ok++;
				else {
					result->cancel_other++;
					result->bad_results++;
				}
				break;
			}
		}
		if (!found)
			result->unexpected_cqes++;
	}
	if (cancel_seen != ((UINT64_C(1) << PENDING) - 1U))
		result->unexpected_cqes++;
	if (!miss && poll_seen != ((UINT64_C(1) << PENDING) - 1U))
		result->unexpected_cqes++;
	return result->unexpected_cqes || result->duplicate_cqes ||
	       result->bad_results || result->bad_flags ? -1 : 0;
}

static int arm_target(struct fixture *fixture, enum profile_id profile,
		      unsigned int round, unsigned int batch,
		      struct result *result)
{
	unsigned int i;

	for (i = 0; i < PENDING; i++) {
		if (queue_poll(&fixture->ring, fixture->target_fd,
			       make_tag(KIND_POLL, profile, round, batch, i)) != 0)
			return -1;
	}
	if (enter_ring(&fixture->ring, PENDING, 0, result) != 0)
		return -1;
	result->poll_submitted += PENDING;
	return prove_pending(fixture, result, false);
}

static int cleanup_target(struct fixture *fixture, enum profile_id profile,
			  unsigned int round, unsigned int batch,
			  struct result *result)
{
	struct io_uring_cqe cqes[PENDING * 2U];
	unsigned int i;

	for (i = 0; i < PENDING; i++) {
		if (queue_cancel_key(&fixture->ring,
				     make_tag(KIND_POLL, profile, round, batch, i),
				     make_tag(KIND_CLEANUP, profile, round,
					      batch, i)) != 0)
			return -1;
	}
	if (submit_and_copy(&fixture->ring, PENDING, PENDING * 2U, cqes,
			    result) != 0)
		return -1;
	for (i = 0; i < PENDING * 2U; i++) {
		unsigned int index;
		bool found = false;

		observe_cqe_common(&cqes[i], result);
		for (index = 0; index < PENDING; index++) {
			if (cqes[i].user_data == make_tag(KIND_POLL, profile,
							 round, batch, index)) {
				found = true;
				result->original_cqes++;
				if (cqes[i].res != -ECANCELED)
					result->bad_results++;
				break;
			}
			if (cqes[i].user_data == make_tag(KIND_CLEANUP, profile,
							 round, batch, index)) {
				found = true;
				result->cancel_cqes++;
				if (cqes[i].res != 0)
					result->bad_results++;
				break;
			}
		}
		if (!found)
			result->unexpected_cqes++;
	}
	return result->unexpected_cqes || result->bad_results ||
	       result->bad_flags ? -1 : 0;
}

static int run_keyed_round(struct fixture *fixture, enum profile_id profile,
			   unsigned int round, unsigned int batches,
			   bool timed, struct result *result)
{
	struct io_uring_cqe cqes[PENDING * 2U];
	unsigned int batch, i;

	memset(result, 0, sizeof(*result));
	result->batches = batches;
	result->attempts = (uint64_t)batches * PENDING;
	result->logical_cancels = profile == PROFILE_A0_HIT ?
		result->attempts : 0;
	for (batch = 0; batch < batches && !stop_requested; batch++) {
		uint64_t start = 0;
		unsigned int expected = profile == PROFILE_A0_HIT ?
			PENDING * 2U : PENDING;

		if (arm_target(fixture, profile, round, batch, result) != 0)
			return -1;
		if (timed)
			start = now_ns();
		for (i = 0; i < PENDING; i++) {
			uint64_t target = profile == PROFILE_A0_HIT ?
				make_tag(KIND_POLL, profile, round, batch, i) :
				make_tag(KIND_NEVER, profile, round, batch, i);
			if (queue_cancel_key(&fixture->ring, target,
					     make_tag(KIND_CANCEL, profile, round,
						      batch, i)) != 0)
				return -1;
		}
		if (submit_and_copy(&fixture->ring, PENDING, expected, cqes,
				    result) != 0)
			return -1;
		if (timed)
			result->timed_ns += now_ns() - start;
		result->cancel_submitted += PENDING;
		if (validate_keyed(cqes, expected, profile, round, batch,
				   profile == PROFILE_A0_MISS, result) != 0)
			return -1;
		if (profile == PROFILE_A0_MISS &&
		    cleanup_target(fixture, profile, round, batch, result) != 0)
			return -1;
	}
	result->sq_dropped = *fixture->ring.sq_dropped;
	result->cq_overflow = *fixture->ring.cq_overflow;
	result->outstanding = ring_outstanding(&fixture->ring);
	result->semantic_pass = !stop_requested && result->timeouts == 0 &&
		result->unexpected_cqes == 0 && result->duplicate_cqes == 0 &&
		result->bad_results == 0 && result->bad_flags == 0 &&
		result->eventfd_state_failures == 0 && result->sq_dropped == 0 &&
		result->cq_overflow == 0 && result->outstanding == 0 &&
		result->pending_proofs == batches &&
		(profile == PROFILE_A0_HIT ?
		 (result->cancel_ok == result->attempts &&
		  result->original_cqes == result->attempts) :
		 (result->cancel_enoent == result->attempts));
	return result->semantic_pass ? 0 : -1;
}

static int arm_f0(struct fixture *fixture, unsigned int round,
		  unsigned int batch, struct result *result)
{
	unsigned int i;

	for (i = 0; i < PENDING; i++) {
		if (queue_poll(&fixture->ring, fixture->target_fd,
			       make_tag(KIND_POLL, PROFILE_F0_FD_ALL, round,
					batch, i)) != 0)
			return -1;
	}
	for (i = 0; i < DECOYS; i++) {
		if (queue_poll(&fixture->ring, fixture->decoy_fd,
			       make_tag(KIND_DECOY, PROFILE_F0_FD_ALL, round,
					batch, i)) != 0)
			return -1;
	}
	if (enter_ring(&fixture->ring, PENDING + DECOYS, 0, result) != 0)
		return -1;
	result->poll_submitted += PENDING + DECOYS;
	return prove_pending(fixture, result, true);
}

static int validate_f0(const struct io_uring_cqe *cqes, unsigned int round,
		       unsigned int batch, struct result *result)
{
	uint64_t poll_seen = 0;
	bool bulk_seen = false;
	unsigned int i, index;

	for (i = 0; i < PENDING + 1U; i++) {
		bool found = false;

		observe_cqe_common(&cqes[i], result);
		if (cqes[i].user_data == make_tag(KIND_BULK, PROFILE_F0_FD_ALL,
						      round, batch, 0)) {
			found = true;
			if (bulk_seen)
				result->duplicate_cqes++;
			bulk_seen = true;
			result->cancel_cqes++;
			if (cqes[i].res == (int)PENDING)
				result->cancel_ok++;
			else {
				result->cancel_other++;
				result->bad_results++;
			}
		}
		for (index = 0; !found && index < PENDING; index++) {
			if (cqes[i].user_data == make_tag(KIND_POLL,
							 PROFILE_F0_FD_ALL,
							 round, batch, index)) {
				found = true;
				if (poll_seen & (UINT64_C(1) << index))
					result->duplicate_cqes++;
				poll_seen |= UINT64_C(1) << index;
				result->original_cqes++;
				if (cqes[i].res != -ECANCELED)
					result->bad_results++;
			}
		}
		if (!found)
			result->unexpected_cqes++;
	}
	if (!bulk_seen || poll_seen != ((UINT64_C(1) << PENDING) - 1U))
		result->unexpected_cqes++;
	return result->unexpected_cqes || result->duplicate_cqes ||
	       result->bad_results || result->bad_flags ? -1 : 0;
}

static int cleanup_decoys(struct fixture *fixture, unsigned int round,
			  unsigned int batch, struct result *result)
{
	struct io_uring_cqe cqes[DECOYS * 2U];
	unsigned int i, index;
	uint64_t decoy_seen = 0, cancel_seen = 0;

	if (cq_available(&fixture->ring) != 0 ||
	    eventfd_is_unsignaled(fixture->decoy_fd) != 0) {
		result->eventfd_state_failures++;
		return -1;
	}
	for (i = 0; i < DECOYS; i++) {
		if (queue_cancel_key(&fixture->ring,
				     make_tag(KIND_DECOY, PROFILE_F0_FD_ALL,
					      round, batch, i),
				     make_tag(KIND_CLEANUP, PROFILE_F0_FD_ALL,
					      round, batch, i)) != 0)
			return -1;
	}
	if (submit_and_copy(&fixture->ring, DECOYS, DECOYS * 2U, cqes,
			    result) != 0)
		return -1;
	for (i = 0; i < DECOYS * 2U; i++) {
		bool found = false;

		observe_cqe_common(&cqes[i], result);
		for (index = 0; index < DECOYS; index++) {
			if (cqes[i].user_data == make_tag(KIND_DECOY,
							 PROFILE_F0_FD_ALL,
							 round, batch, index)) {
				found = true;
				if (decoy_seen & (UINT64_C(1) << index))
					result->duplicate_cqes++;
				decoy_seen |= UINT64_C(1) << index;
				result->decoy_cqes++;
				if (cqes[i].res != -ECANCELED)
					result->bad_results++;
				break;
			}
			if (cqes[i].user_data == make_tag(KIND_CLEANUP,
							 PROFILE_F0_FD_ALL,
							 round, batch, index)) {
				found = true;
				if (cancel_seen & (UINT64_C(1) << index))
					result->duplicate_cqes++;
				cancel_seen |= UINT64_C(1) << index;
				result->cancel_cqes++;
				if (cqes[i].res != 0)
					result->bad_results++;
				break;
			}
		}
		if (!found)
			result->unexpected_cqes++;
	}
	if (decoy_seen != ((UINT64_C(1) << DECOYS) - 1U) ||
	    cancel_seen != ((UINT64_C(1) << DECOYS) - 1U))
		result->unexpected_cqes++;
	return result->unexpected_cqes || result->duplicate_cqes ||
	       result->bad_results || result->bad_flags ? -1 : 0;
}

static int run_f0_round(struct fixture *fixture, unsigned int round,
			unsigned int batches, bool timed,
			struct result *result)
{
	struct io_uring_cqe cqes[PENDING + 1U];
	unsigned int batch;

	memset(result, 0, sizeof(*result));
	result->batches = result->attempts = batches;
	result->logical_cancels = (uint64_t)batches * PENDING;
	for (batch = 0; batch < batches && !stop_requested; batch++) {
		uint64_t start = 0;

		if (arm_f0(fixture, round, batch, result) != 0)
			return -1;
		if (timed)
			start = now_ns();
		if (queue_cancel_fd_all(&fixture->ring, fixture->target_fd,
					make_tag(KIND_BULK, PROFILE_F0_FD_ALL,
						 round, batch, 0)) != 0)
			return -1;
		if (submit_and_copy(&fixture->ring, 1U, PENDING + 1U, cqes,
				    result) != 0)
			return -1;
		if (timed)
			result->timed_ns += now_ns() - start;
		result->cancel_submitted++;
		if (validate_f0(cqes, round, batch, result) != 0 ||
		    cleanup_decoys(fixture, round, batch, result) != 0)
			return -1;
	}
	result->sq_dropped = *fixture->ring.sq_dropped;
	result->cq_overflow = *fixture->ring.cq_overflow;
	result->outstanding = ring_outstanding(&fixture->ring);
	result->semantic_pass = !stop_requested && result->timeouts == 0 &&
		result->unexpected_cqes == 0 && result->duplicate_cqes == 0 &&
		result->bad_results == 0 && result->bad_flags == 0 &&
		result->eventfd_state_failures == 0 && result->sq_dropped == 0 &&
		result->cq_overflow == 0 && result->outstanding == 0 &&
		result->pending_proofs == batches && result->cancel_ok == batches &&
		result->original_cqes == (uint64_t)batches * PENDING &&
		result->decoy_cqes == (uint64_t)batches * DECOYS;
	return result->semantic_pass ? 0 : -1;
}

static int run_s0_round(struct fixture *fixture, unsigned int round,
			unsigned int cycles, bool timed,
			struct result *result)
{
	struct io_uring_sync_cancel_reg sc;
	struct io_uring_cqe cqe;
	unsigned int cycle;

	memset(result, 0, sizeof(*result));
	result->batches = result->attempts = result->logical_cancels = cycles;
	for (cycle = 0; cycle < cycles && !stop_requested; cycle++) {
		uint64_t poll_tag = make_tag(KIND_POLL, PROFILE_S0_SYNC, round,
					     cycle, 0);
		uint64_t start = 0;
		int ret;

		if (queue_poll(&fixture->ring, fixture->target_fd, poll_tag) != 0 ||
		    enter_ring(&fixture->ring, 1U, 0, result) != 0)
			return -1;
		result->poll_submitted++;
		if (prove_pending(fixture, result, false) != 0)
			return -1;
		memset(&sc, 0, sizeof(sc));
		sc.addr = poll_tag;
		sc.fd = -1;
		sc.timeout.tv_sec = 1;
		if (timed)
			start = now_ns();
		do {
			ret = (int)syscall(__NR_io_uring_register, fixture->ring.fd,
					   IORING_REGISTER_SYNC_CANCEL, &sc, 1U);
		} while (ret < 0 && errno == EINTR && !stop_requested);
		if (timed)
			result->timed_ns += now_ns() - start;
		result->cancel_submitted++;
		if (ret != 0) {
			if (ret < 0 && errno == ENOENT)
				result->cancel_enoent++;
			else
				result->cancel_other++;
			result->bad_results++;
			return -1;
		}
		result->cancel_ok++;
		if (wait_and_copy(&fixture->ring, 1U, &cqe, result) != 0)
			return -1;
		observe_cqe_common(&cqe, result);
		result->original_cqes++;
		if (cqe.user_data != poll_tag) {
			result->unexpected_cqes++;
			return -1;
		}
		if (cqe.res != -ECANCELED) {
			result->bad_results++;
			return -1;
		}
	}
	result->sq_dropped = *fixture->ring.sq_dropped;
	result->cq_overflow = *fixture->ring.cq_overflow;
	result->outstanding = ring_outstanding(&fixture->ring);
	result->semantic_pass = !stop_requested && result->timeouts == 0 &&
		result->unexpected_cqes == 0 && result->bad_results == 0 &&
		result->bad_flags == 0 && result->eventfd_state_failures == 0 &&
		result->sq_dropped == 0 && result->cq_overflow == 0 &&
		result->outstanding == 0 && result->pending_proofs == cycles &&
		result->cancel_ok == cycles && result->original_cqes == cycles;
	return result->semantic_pass ? 0 : -1;
}

static int run_p0(struct fixture *fixture, struct result *result)
{
	struct io_uring_sqe *sqe;
	struct io_uring_cqe cqe;

	memset(result, 0, sizeof(*result));
	result->batches = result->attempts = 1;
	sqe = queue_sqe(&fixture->ring);
	if (!sqe)
		return -1;
	sqe->opcode = IORING_OP_ASYNC_CANCEL;
	sqe->fd = -1;
	sqe->cancel_flags = IORING_ASYNC_CANCEL_OP;
	sqe->len = 255U;
	sqe->user_data = make_tag(KIND_CANCEL, PROFILE_P0_PREP, 0, 0, 0);
	if (submit_and_copy(&fixture->ring, 1U, 1U, &cqe, result) != 0)
		return -1;
	result->cancel_submitted = result->cancel_cqes = 1;
	observe_cqe_common(&cqe, result);
	if (cqe.user_data != make_tag(KIND_CANCEL, PROFILE_P0_PREP, 0, 0, 0))
		result->unexpected_cqes++;
	if (cqe.res == -EINVAL)
		result->cancel_ok++;
	else if (cqe.res == -ENOENT)
		result->cancel_enoent++;
	else {
		result->cancel_other++;
		result->bad_results++;
	}
	result->sq_dropped = *fixture->ring.sq_dropped;
	result->cq_overflow = *fixture->ring.cq_overflow;
	result->outstanding = ring_outstanding(&fixture->ring);
	result->semantic_pass = result->unexpected_cqes == 0 &&
		result->bad_results == 0 && result->bad_flags == 0 &&
		result->sq_dropped == 0 && result->cq_overflow == 0 &&
		result->outstanding == 0 &&
		(result->cancel_ok == 1 || result->cancel_enoent == 1);
	return result->semantic_pass ? 0 : -1;
}

static int count_open_fds(void)
{
	DIR *dir = opendir("/proc/self/fd");
	struct dirent *entry;
	int count = -1;

	if (!dir)
		return -1;
	count = 0;
	while ((entry = readdir(dir)) != NULL) {
		if (strcmp(entry->d_name, ".") && strcmp(entry->d_name, ".."))
			count++;
	}
	closedir(dir);
	return count - 1; /* exclude the directory descriptor used by opendir */
}

static int run_r0(struct result *result)
{
	struct fixture fixture;
	unsigned int i;
	int before, after, close_ret;
	uint64_t start;

	memset(result, 0, sizeof(*result));
	result->batches = result->attempts = 1;
	result->logical_cancels = PENDING + DECOYS;
	before = count_open_fds();
	if (before < 0 || fixture_init(&fixture) != 0)
		return -1;
	for (i = 0; i < PENDING; i++) {
		if (queue_poll(&fixture.ring, fixture.target_fd,
			       make_tag(KIND_POLL, PROFILE_R0_RELEASE, 0, 0, i)) != 0)
			goto fail;
	}
	for (i = 0; i < DECOYS; i++) {
		if (queue_poll(&fixture.ring, fixture.decoy_fd,
			       make_tag(KIND_DECOY, PROFILE_R0_RELEASE, 0, 0, i)) != 0)
			goto fail;
	}
	if (enter_ring(&fixture.ring, PENDING + DECOYS, 0, result) != 0 ||
	    prove_pending(&fixture, result, true) != 0)
		goto fail;
	result->poll_submitted = PENDING + DECOYS;
	raw_ring_unmap(&fixture.ring);
	start = now_ns();
	close_ret = close(fixture.ring.fd);
	result->timed_ns = now_ns() - start;
	fixture.ring.fd = -1;
	close(fixture.target_fd);
	close(fixture.decoy_fd);
	fixture.target_fd = fixture.decoy_fd = -1;
	after = count_open_fds();
	result->semantic_pass = close_ret == 0 && after == before &&
		result->timed_ns < RELEASE_LIMIT_NS;
	return result->semantic_pass ? 0 : -1;
fail:
	fixture_destroy(&fixture);
	return -1;
}

static const char *profile_name(enum profile_id profile)
{
	switch (profile) {
	case PROFILE_A0_HIT: return "a0_hit";
	case PROFILE_A0_MISS: return "a0_miss";
	case PROFILE_F0_FD_ALL: return "f0_fd_all";
	case PROFILE_S0_SYNC: return "s0_sync";
	case PROFILE_P0_PREP: return "p0_prep";
	case PROFILE_R0_RELEASE: return "r0_release";
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
	double ns_per_attempt = result->attempts ?
		(double)result->timed_ns / (double)result->attempts : 0.0;

	fprintf(output,
		"%u\t%s\t%s\t%s\t%s\t%s\t%u\t%u\t%u\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%.6f\t%u\t%s\t%s\t%d\n",
		SCHEMA_VERSION, workload->kernel_release, workload->boot_id,
		mode_name(options->mode), profile_name(options->profile),
		phase_name(phase), round, RING_ENTRIES, PENDING, result->batches,
		result->attempts, result->logical_cancels, result->poll_submitted,
		result->cancel_submitted, result->cancel_ok,
		result->cancel_enoent, result->cancel_other,
		result->original_cqes, result->cancel_cqes, result->decoy_cqes,
		result->pending_proofs, result->unexpected_cqes,
		result->duplicate_cqes, result->bad_results, result->bad_flags,
		result->eventfd_state_failures, result->timeouts,
		result->sq_dropped, result->cq_overflow, result->outstanding,
		result->timed_ns, ns_per_attempt, CONTRACT_CPU,
		result->semantic_pass ? "1" : "0", INPUT_SHA256,
		options->mode == MODE_TRACE ? 1 : 0);
	fflush(output);
}

static int run_profile_round(struct fixture *fixture,
			     enum profile_id profile, unsigned int round,
			     unsigned int work_units, bool timed,
			     struct result *result)
{
	switch (profile) {
	case PROFILE_A0_HIT:
	case PROFILE_A0_MISS:
		return run_keyed_round(fixture, profile, round, work_units,
				       timed, result);
	case PROFILE_F0_FD_ALL:
		return run_f0_round(fixture, round, work_units, timed, result);
	case PROFILE_S0_SYNC:
		return run_s0_round(fixture, round, work_units, timed, result);
	case PROFILE_P0_PREP:
		return run_p0(fixture, result);
	default:
		return -1;
	}
}

static unsigned int work_units(enum profile_id profile, enum mode_id mode)
{
	if (profile == PROFILE_S0_SYNC) {
		if (mode == MODE_SMOKE)
			return S0_SMOKE_CYCLES;
		if (mode == MODE_TRACE)
			return S0_TRACE_CYCLES;
		return S0_POINT_CYCLES;
	}
	if (mode == MODE_SMOKE)
		return SMOKE_BATCHES;
	if (mode == MODE_TRACE)
		return TRACE_BATCHES;
	return POINT_BATCHES;
}

static int parse_profile(const char *text, enum profile_id *profile)
{
	if (!strcmp(text, "a0_hit")) *profile = PROFILE_A0_HIT;
	else if (!strcmp(text, "a0_miss")) *profile = PROFILE_A0_MISS;
	else if (!strcmp(text, "f0_fd_all")) *profile = PROFILE_F0_FD_ALL;
	else if (!strcmp(text, "s0_sync")) *profile = PROFILE_S0_SYNC;
	else if (!strcmp(text, "p0_prep")) *profile = PROFILE_P0_PREP;
	else if (!strcmp(text, "r0_release")) *profile = PROFILE_R0_RELEASE;
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
		"usage: %s --profile a0_hit|a0_miss|f0_fd_all|s0_sync|p0_prep|r0_release "
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
	    (options->profile == PROFILE_P0_PREP ||
	     options->profile == PROFILE_R0_RELEASE))
		return -1;
	return 0;
}

static void describe(const struct options *options)
{
	printf("{\"profile\":\"%s\",\"ring_entries\":%u,"
	       "\"pending\":%u,\"point_batches\":%u,"
	       "\"warmups\":%u,\"measured_rounds\":%u,"
	       "\"cpu\":%u,\"input_sha256\":\"%s\"}\n",
	       profile_name(options->profile), RING_ENTRIES, PENDING,
	       options->profile == PROFILE_S0_SYNC ? S0_POINT_CYCLES :
	       POINT_BATCHES, WARMUP_ROUNDS, MEASURED_ROUNDS,
	       CONTRACT_CPU, INPUT_SHA256);
}

int main(int argc, char **argv)
{
	struct options options;
	struct workload workload;
	struct fixture fixture;
	struct result result;
	struct utsname uts;
	FILE *output = NULL;
	unsigned int round, units;
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
	    signal(SIGTERM, handle_signal) == SIG_ERR) {
		perror("signal");
		return EXIT_FAILURE;
	}
	if (pin_cpu() != 0)
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

	if (options.profile == PROFILE_R0_RELEASE) {
		if (run_r0(&result) != 0)
			goto out;
		write_result(output, &workload, &options,
			     options.mode == MODE_TRACE ? PHASE_TRACE : PHASE_SMOKE,
			     0, &result);
		ret = EXIT_SUCCESS;
		goto out;
	}
	if (fixture_init(&fixture) != 0)
		goto out;
	units = work_units(options.profile, options.mode);
	if (options.mode == MODE_SMOKE || options.mode == MODE_TRACE) {
		if (run_profile_round(&fixture, options.profile, 0, units,
				      false, &result) != 0)
			goto destroy;
		write_result(output, &workload, &options,
			     options.mode == MODE_TRACE ? PHASE_TRACE : PHASE_SMOKE,
			     0, &result);
		ret = EXIT_SUCCESS;
		goto destroy;
	}
	for (round = 0; round < WARMUP_ROUNDS; round++) {
		if (run_profile_round(&fixture, options.profile, round, units,
				      false, &result) != 0)
			goto destroy;
	}
	for (round = 0; round < MEASURED_ROUNDS; round++) {
		if (run_profile_round(&fixture, options.profile,
				      WARMUP_ROUNDS + round, units, true,
				      &result) != 0)
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
