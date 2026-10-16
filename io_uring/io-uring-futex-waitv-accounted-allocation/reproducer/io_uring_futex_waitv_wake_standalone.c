// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Standalone io_uring futex WAITV -> WAKE microbenchmark.
 *
 * One cycle submits eight WAITV requests.  Each request waits on eight
 * private 32-bit futex words.  Waking element 3 completes each WAITV request.
 * Timing covers WAITV preparation/allocation, submission, the matching wakes,
 * and collection of the 16 CQEs.  A post-timing wake verifies that the seven
 * residual waiters from every vector were removed.
 *
 * The retained workload's exact parent/child result was:
 *   816095894c0f parent midpoint: 1047.099 ns/pair
 *   6e0d71c288fd child:          1120.562 ns/pair (+7.016%)
 *
 * This program uses the raw UAPI and does not require liburing.
 */

#define _GNU_SOURCE

#include <errno.h>
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
#include <time.h>
#include <unistd.h>

#define RING_ENTRIES 64U
#define GROUPS 8U
#define WIDTH 8U
#define WAKE_INDEX 3U
#define POINT_CYCLES 512U
#define WARMUPS 3U
#define ROUNDS 15U
#define TEST_CPU 2U
#define FUTEX_FLAGS (FUTEX2_SIZE_U32 | FUTEX2_PRIVATE)
#define TAG_KIND_SHIFT 62U
#define TAG_INDEX_MASK ((UINT64_C(1) << TAG_KIND_SHIFT) - 1U)

enum tag_kind {
	TAG_WAITV = 0,
	TAG_WAKE = 1,
	TAG_CHECK = 2,
};

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

struct futex_cell {
	uint32_t value;
	unsigned char pad[60];
};

struct fixture {
	struct ring ring;
	_Alignas(64) struct futex_cell cells[GROUPS * WIDTH];
	struct futex_waitv vectors[GROUPS][WIDTH];
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

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0)
		die("clock_gettime");
	return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + ts.tv_nsec;
}

static uint64_t make_tag(enum tag_kind kind, unsigned int index)
{
	return (uint64_t)kind << TAG_KIND_SHIFT | index;
}

static enum tag_kind tag_kind(uint64_t tag)
{
	return (enum tag_kind)(tag >> TAG_KIND_SHIFT);
}

static unsigned int tag_index(uint64_t tag)
{
	return (unsigned int)(tag & TAG_INDEX_MASK);
}

static void pin_test_cpu(void)
{
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(TEST_CPU, &set);
	if (sched_setaffinity(0, sizeof(set), &set) != 0)
		die("sched_setaffinity");
	if (sched_getcpu() != (int)TEST_CPU)
		bad("CPU affinity did not take effect");
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
	ring->local_tail = load_acquire(ring->sq_tail);
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

	if (ring->local_tail - load_acquire(ring->sq_head) >= *ring->sq_entries)
		bad("submission queue full");
	index = ring->local_tail++ & *ring->sq_mask;
	ring->sq_array[index] = index;
	sqe = &ring->sqes[index];
	memset(sqe, 0, sizeof(*sqe));
	return sqe;
}

static void submit(struct ring *ring, unsigned int min_complete)
{
	unsigned int old_tail = load_acquire(ring->sq_tail);
	unsigned int count = ring->local_tail - old_tail;
	unsigned int flags = min_complete ? IORING_ENTER_GETEVENTS : 0;
	int ret;

	store_release(ring->sq_tail, ring->local_tail);
	do {
		ret = syscall(__NR_io_uring_enter, ring->fd, count, min_complete,
			      flags, NULL, 0);
	} while (ret < 0 && errno == EINTR);
	if (ret < 0)
		die("io_uring_enter");
	if (ret != (int)count)
		bad("short submission");
}

static void queue_waitv(struct ring *ring, struct futex_waitv *vector,
			unsigned int group)
{
	struct io_uring_sqe *sqe = get_sqe(ring);

	sqe->opcode = IORING_OP_FUTEX_WAITV;
	sqe->addr = (uintptr_t)vector;
	sqe->len = WIDTH;
	sqe->user_data = make_tag(TAG_WAITV, group);
}

static void queue_wake(struct ring *ring, uint32_t *word,
		       enum tag_kind kind, unsigned int group)
{
	struct io_uring_sqe *sqe = get_sqe(ring);

	sqe->opcode = IORING_OP_FUTEX_WAKE;
	sqe->fd = FUTEX_FLAGS;
	sqe->addr = (uintptr_t)word;
	sqe->addr2 = 1;
	sqe->addr3 = FUTEX_BITSET_MATCH_ANY;
	sqe->user_data = make_tag(kind, group);
}

static void collect_pairs(struct ring *ring)
{
	bool waits[GROUPS] = { false }, wakes[GROUPS] = { false };
	unsigned int head = load_acquire(ring->cq_head);
	unsigned int ready = load_acquire(ring->cq_tail) - head;
	unsigned int i;

	if (ready != GROUPS * 2U)
		bad("unexpected timed CQE count");
	for (i = 0; i < ready; i++) {
		struct io_uring_cqe *cqe =
			&ring->cqes[(head + i) & *ring->cq_mask];
		enum tag_kind kind = tag_kind(cqe->user_data);
		unsigned int group = tag_index(cqe->user_data);

		if (group >= GROUPS || cqe->flags)
			bad("unknown timed CQE");
		if (kind == TAG_WAITV) {
			if (cqe->res != (int)WAKE_INDEX || waits[group])
				bad("wrong or duplicate WAITV CQE");
			waits[group] = true;
		} else if (kind == TAG_WAKE) {
			if (cqe->res != 1 || wakes[group])
				bad("wrong or duplicate WAKE CQE");
			wakes[group] = true;
		} else {
			bad("unexpected timed CQE kind");
		}
	}
	for (i = 0; i < GROUPS; i++)
		if (!waits[i] || !wakes[i])
			bad("missing timed completion");
	store_release(ring->cq_head, head + ready);
}

static void collect_checks(struct ring *ring)
{
	bool seen[GROUPS] = { false };
	unsigned int head = load_acquire(ring->cq_head);
	unsigned int ready = load_acquire(ring->cq_tail) - head;
	unsigned int i;

	if (ready != GROUPS)
		bad("unexpected cleanup CQE count");
	for (i = 0; i < ready; i++) {
		struct io_uring_cqe *cqe =
			&ring->cqes[(head + i) & *ring->cq_mask];
		unsigned int group = tag_index(cqe->user_data);

		if (tag_kind(cqe->user_data) != TAG_CHECK || group >= GROUPS ||
		    cqe->res != 0 || cqe->flags || seen[group])
			bad("residual WAITV waiter or bad cleanup CQE");
		seen[group] = true;
	}
	for (i = 0; i < GROUPS; i++)
		if (!seen[i])
			bad("missing cleanup completion");
	store_release(ring->cq_head, head + ready);
}

static void fixture_init(struct fixture *fixture)
{
	unsigned int group, index;

	memset(fixture, 0, sizeof(*fixture));
	ring_init(&fixture->ring);
	for (group = 0; group < GROUPS; group++) {
		for (index = 0; index < WIDTH; index++) {
			unsigned int cell = group * WIDTH + index;
			struct futex_waitv *item = &fixture->vectors[group][index];

			item->val = 0;
			item->uaddr = (uintptr_t)&fixture->cells[cell].value;
			item->flags = FUTEX_FLAGS;
			item->__reserved = 0;
		}
	}
}

static uint64_t run_cycle(struct fixture *fixture, bool measure)
{
	uint64_t start = 0, elapsed;
	unsigned int group, index;

	for (group = 0; group < GROUPS; group++)
		for (index = 0; index < WIDTH; index++)
			fixture->cells[group * WIDTH + index].value = 0;
	if (measure)
		start = now_ns();
	for (group = 0; group < GROUPS; group++)
		queue_waitv(&fixture->ring, fixture->vectors[group], group);
	submit(&fixture->ring, 0);
	for (group = 0; group < GROUPS; group++)
		queue_wake(&fixture->ring,
			   &fixture->cells[group * WIDTH + WAKE_INDEX].value,
			   TAG_WAKE, group);
	submit(&fixture->ring, GROUPS * 2U);
	collect_pairs(&fixture->ring);
	elapsed = measure ? now_ns() - start : 0;

	/* This semantic check is intentionally outside the timed region. */
	for (group = 0; group < GROUPS; group++)
		queue_wake(&fixture->ring,
			   &fixture->cells[group * WIDTH].value,
			   TAG_CHECK, group);
	submit(&fixture->ring, GROUPS);
	collect_checks(&fixture->ring);
	return elapsed;
}

int main(int argc, char **argv)
{
	struct fixture fixture;
	unsigned int cycles = POINT_CYCLES, rounds = ROUNDS, warmups = WARMUPS;
	uint64_t total_ns = 0;
	unsigned int round, cycle;

	if (argc == 2 && !strcmp(argv[1], "--smoke")) {
		cycles = 2;
		rounds = 1;
		warmups = 0;
	} else if (argc != 1) {
		fprintf(stderr, "Usage: %s [--smoke]\n", argv[0]);
		return EXIT_FAILURE;
	}
	if (signal(SIGALRM, timeout_handler) == SIG_ERR)
		die("signal");
	alarm(120);
	pin_test_cpu();
	fixture_init(&fixture);

	for (round = 0; round < warmups; round++)
		for (cycle = 0; cycle < cycles; cycle++)
			(void)run_cycle(&fixture, false);

	puts("round\tpairs\tns_per_pair\tsemantic_pass");
	for (round = 0; round < rounds; round++) {
		uint64_t elapsed = 0;

		for (cycle = 0; cycle < cycles; cycle++)
			elapsed += run_cycle(&fixture, true);
		total_ns += elapsed;
		printf("%u\t%u\t%.6f\t1\n", round, cycles * GROUPS,
		       (double)elapsed / (cycles * GROUPS));
	}
	printf("mean\t%u\t%.6f\t1\n", rounds * cycles * GROUPS,
	       (double)total_ns / (rounds * cycles * GROUPS));

	if (*fixture.ring.sq_dropped || *fixture.ring.cq_overflow ||
	    load_acquire(fixture.ring.cq_tail) !=
		load_acquire(fixture.ring.cq_head) ||
	    load_acquire(fixture.ring.sq_head) != fixture.ring.local_tail)
		bad("ring not empty at exit");
	alarm(0);
	ring_destroy(&fixture.ring);
	return 0;
}
