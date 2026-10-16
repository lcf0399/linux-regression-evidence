// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Standalone scalar io_uring futex WAIT -> WAKE microbenchmark.
 *
 * One cycle queues 32 FUTEX_WAIT requests, submits them, then queues one
 * FUTEX_WAKE for each word.  Timing covers SQE preparation, both submits,
 * and collection of all 64 CQEs.  Every wait must return 0 and every wake 1.
 *
 * The default remains the original private-futex workload.  --shared keeps
 * the operation shape unchanged but places the words in a shared mapping and
 * omits FUTEX2_PRIVATE, so patch 2's shared-wait branch can be tested.
 *
 * The exact parent/child result obtained with the longer retained workload was:
 *   6a8118a77eec parent midpoint: 180.027 ns/pair
 *   079afb081c42 child:          196.758 ns/pair (+9.293%)
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
#define SLOTS 32U
#define POINT_CYCLES 512U
#define WARMUPS 3U
#define ROUNDS 15U
#define TEST_CPU 2U
#define PRIVATE_FUTEX_FLAGS (FUTEX2_SIZE_U32 | FUTEX2_PRIVATE)
#define SHARED_FUTEX_FLAGS FUTEX2_SIZE_U32
#define WAKE_TAG (UINT64_C(1) << 63)

_Static_assert(SLOTS <= 64U, "completion masks use 64 bits");

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

static void queue_wait(struct ring *ring, uint32_t *word, unsigned int index,
		       unsigned int futex_flags)
{
	struct io_uring_sqe *sqe = get_sqe(ring);

	sqe->opcode = IORING_OP_FUTEX_WAIT;
	sqe->fd = futex_flags;
	sqe->addr = (uintptr_t)word;
	sqe->addr2 = 0;
	sqe->addr3 = FUTEX_BITSET_MATCH_ANY;
	sqe->user_data = index;
}

static void queue_wake(struct ring *ring, uint32_t *word, unsigned int index,
		       unsigned int futex_flags)
{
	struct io_uring_sqe *sqe = get_sqe(ring);

	sqe->opcode = IORING_OP_FUTEX_WAKE;
	sqe->fd = futex_flags;
	sqe->addr = (uintptr_t)word;
	sqe->addr2 = 1;
	sqe->addr3 = FUTEX_BITSET_MATCH_ANY;
	sqe->user_data = WAKE_TAG | index;
}

static void collect_and_check(struct ring *ring)
{
	uint64_t waits = 0, wakes = 0;
	unsigned int head, ready, i;

	head = load_acquire(ring->cq_head);
	ready = load_acquire(ring->cq_tail) - head;
	if (ready != SLOTS * 2U)
		bad("unexpected CQE count");
	for (i = 0; i < ready; i++) {
		struct io_uring_cqe *cqe =
			&ring->cqes[(head + i) & *ring->cq_mask];
		unsigned int index = cqe->user_data & ~WAKE_TAG;
		uint64_t bit;

		if (index >= SLOTS || cqe->flags)
			bad("unknown CQE");
		bit = UINT64_C(1) << index;
		if (cqe->user_data & WAKE_TAG) {
			if (cqe->res != 1 || (wakes & bit))
				bad("wrong or duplicate WAKE CQE");
			wakes |= bit;
		} else {
			if (cqe->res != 0 || (waits & bit))
				bad("wrong or duplicate WAIT CQE");
			waits |= bit;
		}
	}
	if (waits != (UINT64_C(1) << SLOTS) - 1U ||
	    wakes != (UINT64_C(1) << SLOTS) - 1U)
		bad("missing completion");
	store_release(ring->cq_head, head + ready);
}

static uint64_t run_cycle(struct ring *ring, struct futex_cell cells[SLOTS],
			  unsigned int futex_flags, bool measure)
{
	uint64_t start = 0;
	unsigned int i;

	for (i = 0; i < SLOTS; i++)
		cells[i].value = 0;
	if (measure)
		start = now_ns();
	for (i = 0; i < SLOTS; i++)
		queue_wait(ring, &cells[i].value, i, futex_flags);
	submit(ring, 0);
	for (i = 0; i < SLOTS; i++)
		queue_wake(ring, &cells[i].value, i, futex_flags);
	submit(ring, SLOTS * 2U);
	collect_and_check(ring);
	return measure ? now_ns() - start : 0;
}

int main(int argc, char **argv)
{
	_Alignas(64) struct futex_cell private_cells[SLOTS] = { 0 };
	struct futex_cell *cells = private_cells;
	struct ring ring;
	unsigned int cycles = POINT_CYCLES, rounds = ROUNDS, warmups = WARMUPS;
	unsigned int futex_flags = PRIVATE_FUTEX_FLAGS;
	uint64_t total_ns = 0;
	unsigned int round, cycle, i;
	bool shared = false, smoke = false;

	for (i = 1; i < (unsigned int)argc; i++) {
		if (!strcmp(argv[i], "--smoke"))
			smoke = true;
		else if (!strcmp(argv[i], "--shared"))
			shared = true;
		else {
			fprintf(stderr, "Usage: %s [--smoke] [--shared]\n",
				argv[0]);
			return EXIT_FAILURE;
		}
	}
	if (smoke) {
		cycles = 2;
		rounds = 1;
		warmups = 0;
	}
	if (shared) {
		cells = mmap(NULL, sizeof(private_cells), PROT_READ | PROT_WRITE,
			     MAP_SHARED | MAP_ANONYMOUS, -1, 0);
		if (cells == MAP_FAILED)
			die("mmap shared futex cells");
		futex_flags = SHARED_FUTEX_FLAGS;
	}
	if (signal(SIGALRM, timeout_handler) == SIG_ERR)
		die("signal");
	alarm(120);
	pin_test_cpu();
	ring_init(&ring);

	for (round = 0; round < warmups; round++)
		for (cycle = 0; cycle < cycles; cycle++)
			(void)run_cycle(&ring, cells, futex_flags, false);

	puts("round\tpairs\tns_per_pair\tsemantic_pass");
	for (round = 0; round < rounds; round++) {
		uint64_t elapsed = 0;

		for (cycle = 0; cycle < cycles; cycle++)
			elapsed += run_cycle(&ring, cells, futex_flags, true);
		total_ns += elapsed;
		printf("%u\t%u\t%.6f\t1\n", round, cycles * SLOTS,
		       (double)elapsed / (cycles * SLOTS));
	}
	printf("mean\t%u\t%.6f\t1\n", rounds * cycles * SLOTS,
	       (double)total_ns / (rounds * cycles * SLOTS));

	if (*ring.sq_dropped || *ring.cq_overflow ||
	    load_acquire(ring.cq_tail) != load_acquire(ring.cq_head) ||
	    load_acquire(ring.sq_head) != ring.local_tail)
		bad("ring not empty at exit");
	alarm(0);
	ring_destroy(&ring);
	if (shared)
		munmap(cells, sizeof(private_cells));
	return 0;
}
