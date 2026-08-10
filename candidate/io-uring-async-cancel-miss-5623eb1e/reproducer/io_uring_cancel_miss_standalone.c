// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Minimal standalone reproducer for the io_uring async-cancel miss path.
 *
 * The longer io_uring_cancel_round.c is retained unchanged: it is the exact
 * source used for the formal parent/child measurements.  This shorter file is
 * only for reading and quick independent reproduction of the primary path.
 *
 * Formal result from the exact workload:
 *   parent midpoint: 143.465 ns/attempt
 *   child 5623eb1ed035: 156.137 ns/attempt (+8.833%)
 *
 * One batch:
 *   1. Arm 32 pending eventfd polls (outside timing).
 *   2. Time 32 ASYNC_CANCEL requests for keys that never existed.
 *   3. Require 32 -ENOENT results, then remove the real polls outside timing.
 *
 * The program uses the raw UAPI and has no liburing dependency.
 */

#define _GNU_SOURCE

#include <errno.h>
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
#include <time.h>
#include <unistd.h>

#define RING_ENTRIES 256U
#define PENDING 32U
#define BATCHES 64U
#define WARMUPS 3U
#define ROUNDS 15U
#define TEST_CPU 2U

#define TAG_POLL UINT64_C(1)
#define TAG_CANCEL UINT64_C(2)
#define TAG_CLEANUP UINT64_C(3)
#define TAG_NEVER UINT64_C(15)
#define TAG_KIND_SHIFT 60U
#define TAG_SEQ_SHIFT 8U
#define TAG_PAYLOAD_MASK ((UINT64_C(1) << TAG_KIND_SHIFT) - 1U)

_Static_assert(PENDING < 64U, "completion masks require fewer than 64 polls");

struct ring {
	int fd;
	bool one_mapping;
	void *sq_map, *cq_map, *sqes_map;
	size_t sq_size, cq_size, sqes_size;
	unsigned int *sq_head, *sq_tail, *sq_mask, *sq_entries;
	unsigned int *sq_dropped, *sq_array;
	struct io_uring_sqe *sqes;
	unsigned int local_tail;
	unsigned int *cq_head, *cq_tail, *cq_mask, *cq_overflow;
	struct io_uring_cqe *cqes;
};

static void die(const char *message)
{
	perror(message);
	exit(EXIT_FAILURE);
}

static void bad(const char *message)
{
	fprintf(stderr, "semantic check failed: %s\n", message);
	exit(EXIT_FAILURE);
}

static void timeout_handler(int signo)
{
	(void)signo;
	_exit(124);
}

static unsigned int load(const unsigned int *ptr)
{
	return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

static uint64_t now_ns(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0)
		die("clock_gettime");
	return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + ts.tv_nsec;
}

static uint64_t make_tag(uint64_t kind, uint64_t sequence, unsigned int index)
{
	return kind << TAG_KIND_SHIFT | sequence << TAG_SEQ_SHIFT | index;
}

static void ring_init(struct ring *ring)
{
	struct io_uring_params p = { 0 };
	size_t sq_size, cq_size;

	memset(ring, 0, sizeof(*ring));
	p.flags = IORING_SETUP_CQSIZE;
	p.cq_entries = RING_ENTRIES * 2U;
	ring->fd = syscall(__NR_io_uring_setup, RING_ENTRIES, &p);
	if (ring->fd < 0)
		die("io_uring_setup");

	sq_size = p.sq_off.array + p.sq_entries * sizeof(unsigned int);
	cq_size = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);
	ring->one_mapping = p.features & IORING_FEAT_SINGLE_MMAP;
	if (ring->one_mapping) {
		ring->sq_size = sq_size > cq_size ? sq_size : cq_size;
		ring->sq_map = mmap(NULL, ring->sq_size, PROT_READ | PROT_WRITE,
				    MAP_SHARED | MAP_POPULATE, ring->fd,
				    IORING_OFF_SQ_RING);
		if (ring->sq_map == MAP_FAILED)
			die("mmap SQ/CQ");
		ring->cq_map = ring->sq_map;
	} else {
		ring->sq_size = sq_size;
		ring->cq_size = cq_size;
		ring->sq_map = mmap(NULL, sq_size, PROT_READ | PROT_WRITE,
				    MAP_SHARED | MAP_POPULATE, ring->fd,
				    IORING_OFF_SQ_RING);
		ring->cq_map = mmap(NULL, cq_size, PROT_READ | PROT_WRITE,
				    MAP_SHARED | MAP_POPULATE, ring->fd,
				    IORING_OFF_CQ_RING);
		if (ring->sq_map == MAP_FAILED || ring->cq_map == MAP_FAILED)
			die("mmap rings");
	}
	ring->sqes_size = p.sq_entries * sizeof(struct io_uring_sqe);
	ring->sqes_map = mmap(NULL, ring->sqes_size, PROT_READ | PROT_WRITE,
			       MAP_SHARED | MAP_POPULATE, ring->fd,
			       IORING_OFF_SQES);
	if (ring->sqes_map == MAP_FAILED)
		die("mmap SQEs");

#define SQ_PTR(member) ((unsigned int *)((char *)ring->sq_map + p.sq_off.member))
#define CQ_PTR(member) ((unsigned int *)((char *)ring->cq_map + p.cq_off.member))
	ring->sq_head = SQ_PTR(head);
	ring->sq_tail = SQ_PTR(tail);
	ring->sq_mask = SQ_PTR(ring_mask);
	ring->sq_entries = SQ_PTR(ring_entries);
	ring->sq_dropped = SQ_PTR(dropped);
	ring->sq_array = SQ_PTR(array);
	ring->sqes = ring->sqes_map;
	ring->local_tail = load(ring->sq_tail);
	ring->cq_head = CQ_PTR(head);
	ring->cq_tail = CQ_PTR(tail);
	ring->cq_mask = CQ_PTR(ring_mask);
	ring->cq_overflow = CQ_PTR(overflow);
	ring->cqes = (struct io_uring_cqe *)((char *)ring->cq_map + p.cq_off.cqes);
#undef SQ_PTR
#undef CQ_PTR
}

static void ring_destroy(struct ring *ring)
{
	munmap(ring->sqes_map, ring->sqes_size);
	munmap(ring->sq_map, ring->sq_size);
	if (!ring->one_mapping)
		munmap(ring->cq_map, ring->cq_size);
	close(ring->fd);
}

static struct io_uring_sqe *get_sqe(struct ring *ring)
{
	unsigned int index;
	struct io_uring_sqe *sqe;

	if (ring->local_tail - load(ring->sq_head) >= *ring->sq_entries)
		bad("submission queue full");
	index = ring->local_tail++ & *ring->sq_mask;
	ring->sq_array[index] = index;
	sqe = &ring->sqes[index];
	memset(sqe, 0, sizeof(*sqe));
	return sqe;
}

/* Submit prepared SQEs, wait, and copy exactly the expected CQEs. */
static void submit_and_collect(struct ring *ring, unsigned int expected,
			       struct io_uring_cqe *copy)
{
	unsigned int old_tail = load(ring->sq_tail);
	unsigned int submitted = ring->local_tail - old_tail;
	unsigned int flags = expected ? IORING_ENTER_GETEVENTS : 0;
	unsigned int head, ready, i;
	int ret;

	__atomic_store_n(ring->sq_tail, ring->local_tail, __ATOMIC_RELEASE);
	do {
		ret = syscall(__NR_io_uring_enter, ring->fd, submitted, expected,
			      flags, NULL, 0);
	} while (ret < 0 && errno == EINTR);
	if (ret < 0)
		die("io_uring_enter");
	if (ret != (int)submitted)
		bad("short submission");
	if (!expected)
		return;

	head = load(ring->cq_head);
	ready = load(ring->cq_tail) - head;
	if (ready != expected)
		bad("unexpected CQE count");
	for (i = 0; i < expected; i++)
		copy[i] = ring->cqes[(head + i) & *ring->cq_mask];
	__atomic_store_n(ring->cq_head, head + expected, __ATOMIC_RELEASE);
}

static void queue_poll(struct ring *ring, int event_fd, uint64_t user_data)
{
	struct io_uring_sqe *sqe = get_sqe(ring);

	sqe->opcode = IORING_OP_POLL_ADD;
	sqe->fd = event_fd;
	sqe->poll32_events = POLLIN;
	sqe->user_data = user_data;
}

static void queue_cancel(struct ring *ring, uint64_t target, uint64_t user_data)
{
	struct io_uring_sqe *sqe = get_sqe(ring);

	sqe->opcode = IORING_OP_ASYNC_CANCEL;
	sqe->fd = -1;
	sqe->addr = target;
	sqe->user_data = user_data;
}

static void check_cqes(const struct io_uring_cqe *cqes, unsigned int count,
		       uint64_t sequence, bool cleanup)
{
	uint64_t poll_mask = 0, cancel_mask = 0;
	unsigned int i;

	for (i = 0; i < count; i++) {
		uint64_t kind = cqes[i].user_data >> TAG_KIND_SHIFT;
		uint64_t seq = (cqes[i].user_data & TAG_PAYLOAD_MASK) >> TAG_SEQ_SHIFT;
		unsigned int index = cqes[i].user_data & 0xffU;
		uint64_t bit;

		if (seq != sequence || index >= PENDING || cqes[i].flags)
			bad("unknown CQE");
		bit = UINT64_C(1) << index;
		if (!cleanup && kind == TAG_CANCEL && cqes[i].res == -ENOENT) {
			if (cancel_mask & bit)
				bad("duplicate cancel CQE");
			cancel_mask |= bit;
		} else if (cleanup && kind == TAG_POLL &&
			   cqes[i].res == -ECANCELED) {
			if (poll_mask & bit)
				bad("duplicate poll CQE");
			poll_mask |= bit;
		} else if (cleanup && kind == TAG_CLEANUP && cqes[i].res == 0) {
			if (cancel_mask & bit)
				bad("duplicate cleanup CQE");
			cancel_mask |= bit;
		} else {
			bad("wrong CQE result");
		}
	}
	if (cancel_mask != (UINT64_C(1) << PENDING) - 1U ||
	    (cleanup && poll_mask != (UINT64_C(1) << PENDING) - 1U))
		bad("missing CQE");
}

static void prove_pending(struct ring *ring, int event_fd)
{
	uint64_t value;

	if (load(ring->cq_tail) != load(ring->cq_head))
		bad("poll completed before cancellation");
	errno = 0;
	if (read(event_fd, &value, sizeof(value)) >= 0 || errno != EAGAIN)
		bad("eventfd unexpectedly signaled");
}

static uint64_t run_batch(struct ring *ring, int event_fd,
			  uint64_t sequence, bool measure)
{
	struct io_uring_cqe cqes[PENDING * 2U];
	uint64_t start = 0, elapsed = 0;
	unsigned int i;

	/* Real pending requests form the fixture; setup is not timed. */
	for (i = 0; i < PENDING; i++)
		queue_poll(ring, event_fd, make_tag(TAG_POLL, sequence, i));
	submit_and_collect(ring, 0, NULL);
	prove_pending(ring, event_fd);

	if (measure)
		start = now_ns();
	for (i = 0; i < PENDING; i++)
		queue_cancel(ring, make_tag(TAG_NEVER, sequence, i),
			     make_tag(TAG_CANCEL, sequence, i));
	submit_and_collect(ring, PENDING, cqes);
	if (measure)
		elapsed = now_ns() - start;
	check_cqes(cqes, PENDING, sequence, false);

	/* Remove the real polls after timing so the next batch starts clean. */
	for (i = 0; i < PENDING; i++)
		queue_cancel(ring, make_tag(TAG_POLL, sequence, i),
			     make_tag(TAG_CLEANUP, sequence, i));
	submit_and_collect(ring, PENDING * 2U, cqes);
	check_cqes(cqes, PENDING * 2U, sequence, true);
	return elapsed;
}

static void pin_test_cpu(void)
{
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(TEST_CPU, &set);
	if (sched_setaffinity(0, sizeof(set), &set) != 0)
		die("sched_setaffinity");
}

int main(void)
{
	struct ring ring;
	uint64_t sequence = 0, total_ns = 0;
	unsigned int round, batch;
	int event_fd;

	if (signal(SIGALRM, timeout_handler) == SIG_ERR)
		die("signal");
	alarm(120);
	pin_test_cpu();
	ring_init(&ring);
	event_fd = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
	if (event_fd < 0)
		die("eventfd");

	for (round = 0; round < WARMUPS; round++)
		for (batch = 0; batch < BATCHES; batch++)
			(void)run_batch(&ring, event_fd, sequence++, false);

	puts("round\tattempts\tns_per_attempt\tsemantic_pass");
	for (round = 0; round < ROUNDS; round++) {
		uint64_t elapsed = 0;

		for (batch = 0; batch < BATCHES; batch++)
			elapsed += run_batch(&ring, event_fd, sequence++, true);
		total_ns += elapsed;
		printf("%u\t%u\t%.6f\t1\n", round, BATCHES * PENDING,
		       (double)elapsed / (BATCHES * PENDING));
	}
	printf("mean\t%u\t%.6f\t1\n", ROUNDS * BATCHES * PENDING,
	       (double)total_ns / (ROUNDS * BATCHES * PENDING));

	if (*ring.sq_dropped || *ring.cq_overflow ||
	    load(ring.cq_tail) != load(ring.cq_head) ||
	    load(ring.sq_head) != ring.local_tail)
		bad("ring not empty at exit");
	alarm(0);
	close(event_fd);
	ring_destroy(&ring);
	return 0;
}
