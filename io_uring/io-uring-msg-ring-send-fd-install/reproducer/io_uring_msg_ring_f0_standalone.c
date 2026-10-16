// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Concise raw-UAPI reproducer for MSG_RING / IORING_MSG_SEND_FD.
 *
 * The unchanged 1,165-line io_uring_msg_ring_round.c remains the exact source
 * used for the formal result around commit 7029acd8a950:
 *
 *   parent midpoint 102.526 ns/install -> child 114.441 (+11.621%)
 *
 * This file keeps only F0.  A source ring owns one fixed memfd.  Each round
 * creates a target ring with 4,096 sparse fixed-file slots, fills them with
 * SEND_FD in batches of 64, and checks both completion streams.  After timing,
 * every installed slot is read as a fixed file and its sentinel byte checked.
 */

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <inttypes.h>
#include <linux/io_uring.h>
#include <linux/memfd.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define CPU 2U
#define ENTRIES 256U
#define QD 64U
#define SLOTS 4096U
#define SMOKE_SLOTS 256U
#define WARMUPS 3U
#define ROUNDS 15U
#define SENTINEL_BYTES 4096U
#define SEQ_MASK ((UINT64_C(1) << 48) - 1)
#define TAG_MASK (~SEQ_MASK)
#define SRC_TAG UINT64_C(0x5301000000000000)
#define DST_TAG UINT64_C(0x5401000000000000)
#define READ_TAG UINT64_C(0x5601000000000000)

struct ring {
	int fd;
	bool one_map;
	void *sq_map, *cq_map, *sqes_map;
	size_t sq_size, cq_size, sqes_size;
	unsigned int *sq_head, *sq_tail, *sq_mask, *sq_entries;
	unsigned int *sq_dropped, *sq_array;
	struct io_uring_sqe *sqes;
	unsigned int *cq_head, *cq_tail, *cq_mask, *cq_overflow;
	struct io_uring_cqe *cqes;
};

static void die(const char *what)
{
	perror(what);
	exit(EXIT_FAILURE);
}

static void bad(const char *what)
{
	fprintf(stderr, "semantic check failed: %s\n", what);
	exit(EXIT_FAILURE);
}

static unsigned int load(const unsigned int *p)
{
	return __atomic_load_n(p, __ATOMIC_ACQUIRE);
}

static void store(unsigned int *p, unsigned int value)
{
	__atomic_store_n(p, value, __ATOMIC_RELEASE);
}

static uint64_t now_ns(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts))
		die("clock_gettime");
	return (uint64_t)ts.tv_sec * UINT64_C(1000000000) + ts.tv_nsec;
}

static void pin_cpu(void)
{
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(CPU, &set);
	if (sched_setaffinity(0, sizeof(set), &set))
		die("sched_setaffinity");
	if (sched_getcpu() != (int)CPU)
		bad("CPU affinity");
}

static void ring_destroy(struct ring *r)
{
	if (r->sqes_map)
		munmap(r->sqes_map, r->sqes_size);
	if (r->sq_map)
		munmap(r->sq_map, r->sq_size);
	if (!r->one_map && r->cq_map)
		munmap(r->cq_map, r->cq_size);
	if (r->fd >= 0)
		close(r->fd);
	memset(r, 0, sizeof(*r));
	r->fd = -1;
}

static void ring_init(struct ring *r)
{
	struct io_uring_params p = {0};
	size_t sq_size, cq_size, map_size;
	void *sq, *cq;

	memset(r, 0, sizeof(*r));
	r->fd = (int)syscall(__NR_io_uring_setup, ENTRIES, &p);
	if (r->fd < 0)
		die("io_uring_setup");
	sq_size = p.sq_off.array + p.sq_entries * sizeof(unsigned int);
	cq_size = p.cq_off.cqes + p.cq_entries * sizeof(struct io_uring_cqe);
	r->one_map = p.features & IORING_FEAT_SINGLE_MMAP;
	if (r->one_map) {
		map_size = sq_size > cq_size ? sq_size : cq_size;
		sq = mmap(NULL, map_size, PROT_READ | PROT_WRITE,
			  MAP_SHARED | MAP_POPULATE, r->fd, IORING_OFF_SQ_RING);
		if (sq == MAP_FAILED)
			die("mmap SQ/CQ");
		cq = sq;
		r->sq_size = r->cq_size = map_size;
	} else {
		sq = mmap(NULL, sq_size, PROT_READ | PROT_WRITE,
			  MAP_SHARED | MAP_POPULATE, r->fd, IORING_OFF_SQ_RING);
		if (sq == MAP_FAILED)
			die("mmap SQ");
		cq = mmap(NULL, cq_size, PROT_READ | PROT_WRITE,
			  MAP_SHARED | MAP_POPULATE, r->fd, IORING_OFF_CQ_RING);
		if (cq == MAP_FAILED)
			die("mmap CQ");
		r->sq_size = sq_size;
		r->cq_size = cq_size;
	}
	r->sqes_size = p.sq_entries * sizeof(struct io_uring_sqe);
	r->sqes_map = mmap(NULL, r->sqes_size, PROT_READ | PROT_WRITE,
			    MAP_SHARED | MAP_POPULATE, r->fd, IORING_OFF_SQES);
	if (r->sqes_map == MAP_FAILED)
		die("mmap SQEs");
	r->sq_map = sq;
	r->cq_map = cq;
	r->sq_head = (unsigned int *)((char *)sq + p.sq_off.head);
	r->sq_tail = (unsigned int *)((char *)sq + p.sq_off.tail);
	r->sq_mask = (unsigned int *)((char *)sq + p.sq_off.ring_mask);
	r->sq_entries = (unsigned int *)((char *)sq + p.sq_off.ring_entries);
	r->sq_dropped = (unsigned int *)((char *)sq + p.sq_off.dropped);
	r->sq_array = (unsigned int *)((char *)sq + p.sq_off.array);
	r->sqes = r->sqes_map;
	r->cq_head = (unsigned int *)((char *)cq + p.cq_off.head);
	r->cq_tail = (unsigned int *)((char *)cq + p.cq_off.tail);
	r->cq_mask = (unsigned int *)((char *)cq + p.cq_off.ring_mask);
	r->cq_overflow = (unsigned int *)((char *)cq + p.cq_off.overflow);
	r->cqes = (struct io_uring_cqe *)((char *)cq + p.cq_off.cqes);
	if (*r->sq_entries < QD)
		bad("ring smaller than QD");
}

static void enter_wait(struct ring *r, unsigned int count)
{
	struct __kernel_timespec timeout = {.tv_sec = 5};
	struct io_uring_getevents_arg arg = {.ts = (uintptr_t)&timeout};
	int ret;

	do {
		ret = (int)syscall(__NR_io_uring_enter, r->fd, count, count,
			IORING_ENTER_GETEVENTS | IORING_ENTER_EXT_ARG,
			&arg, sizeof(arg));
	} while (ret < 0 && errno == EINTR);
	if (ret < 0)
		die("io_uring_enter");
	if (ret != (int)count)
		bad("short submit");
}

static unsigned int reserve(struct ring *r, unsigned int count)
{
	unsigned int head = load(r->sq_head), tail = load(r->sq_tail);

	if (tail - head + count > *r->sq_entries)
		bad("SQ full");
	return tail;
}

static uint64_t tag(uint64_t kind, uint64_t seq)
{
	return kind | (seq & SEQ_MASK);
}

static unsigned int decode(uint64_t value, uint64_t kind,
			   uint64_t base, unsigned int count)
{
	uint64_t seq = value & SEQ_MASK;

	if ((value & TAG_MASK) != kind || seq < base || seq >= base + count)
		bad("unexpected user_data");
	return (unsigned int)(seq - base);
}

static void register_source(struct ring *src, int fd)
{
	if (syscall(__NR_io_uring_register, src->fd,
		    IORING_REGISTER_FILES, &fd, 1))
		die("register source file");
}

static void register_sparse(struct ring *dst, unsigned int slots)
{
	struct io_uring_rsrc_register reg = {
		.nr = slots,
		.flags = IORING_RSRC_REGISTER_SPARSE,
	};

	if (syscall(__NR_io_uring_register, dst->fd,
		    IORING_REGISTER_FILES2, &reg, sizeof(reg)))
		die("register sparse files");
}

static void unregister_files(struct ring *r)
{
	if (syscall(__NR_io_uring_register, r->fd,
		    IORING_UNREGISTER_FILES, NULL, 0))
		die("unregister files");
}

static void drain(struct ring *r, uint64_t kind, uint64_t base,
		  unsigned int total, int result[QD], bool seen[QD])
{
	unsigned int head = load(r->cq_head), tail = load(r->cq_tail), i;

	if (tail - head != QD)
		bad("wrong CQE count");
	for (i = 0; i < QD; i++) {
		struct io_uring_cqe *cqe = &r->cqes[(head + i) & *r->cq_mask];
		unsigned int index = decode(cqe->user_data, kind, base, QD);

		if (seen[index] || cqe->flags || cqe->res < 0 ||
		    (unsigned int)cqe->res >= total)
			bad("invalid SEND_FD CQE");
		seen[index] = true;
		result[index] = cqe->res;
	}
	store(r->cq_head, tail);
}

static void verify_slots(struct ring *dst, const unsigned char *sentinel,
			 unsigned int slots)
{
	unsigned char buffers[QD];
	uint64_t base;

	for (base = 0; base < slots; base += QD) {
		bool seen[QD] = {0};
		unsigned int tail = reserve(dst, QD), head, cq_tail, i;

		memset(buffers, 0, sizeof(buffers));
		for (i = 0; i < QD; i++, tail++) {
			unsigned int slot = (unsigned int)base + i;
			unsigned int index = tail & *dst->sq_mask;
			struct io_uring_sqe *sqe = &dst->sqes[index];

			memset(sqe, 0, sizeof(*sqe));
			sqe->opcode = IORING_OP_READ;
			sqe->flags = IOSQE_FIXED_FILE;
			sqe->fd = (int)slot;
			sqe->off = slot % SENTINEL_BYTES;
			sqe->addr = (uintptr_t)&buffers[i];
			sqe->len = 1;
			sqe->user_data = tag(READ_TAG, slot);
			dst->sq_array[index] = index;
		}
		store(dst->sq_tail, tail);
		enter_wait(dst, QD);
		head = load(dst->cq_head);
		cq_tail = load(dst->cq_tail);
		if (cq_tail - head != QD)
			bad("wrong verification CQE count");
		for (i = 0; i < QD; i++) {
			struct io_uring_cqe *cqe =
				&dst->cqes[(head + i) & *dst->cq_mask];
			unsigned int index = decode(cqe->user_data, READ_TAG,
						    base, QD);

			if (seen[index] || cqe->flags || cqe->res != 1)
				bad("invalid fixed-file read CQE");
			seen[index] = true;
		}
		store(dst->cq_head, cq_tail);
		for (i = 0; i < QD; i++)
			if (!seen[i] || buffers[i] !=
			    sentinel[((unsigned int)base + i) % SENTINEL_BYTES])
				bad("fixed-file sentinel mismatch");
	}
}

static uint64_t run_round(struct ring *src, const unsigned char *sentinel,
			  unsigned int slots, bool timed)
{
	struct ring dst;
	uint64_t bitmap[SLOTS / 64U] = {0};
	uint64_t base, started = 0, elapsed = 0;

	if (!slots || slots > SLOTS || slots % QD)
		bad("invalid slot count");
	ring_init(&dst);
	register_sparse(&dst, slots);
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
			sqe->fd = dst.fd;
			sqe->addr = IORING_MSG_SEND_FD;
			sqe->off = tag(DST_TAG, seq);
			sqe->addr3 = 0; /* source fixed-file slot */
			sqe->file_index = IORING_FILE_INDEX_ALLOC;
			sqe->user_data = tag(SRC_TAG, seq);
			src->sq_array[index] = index;
		}
		store(src->sq_tail, tail);
		enter_wait(src, QD);
		drain(src, SRC_TAG, base, slots, src_res, src_seen);
		drain(&dst, DST_TAG, base, slots, dst_res, dst_seen);
		for (i = 0; i < QD; i++) {
			unsigned int slot;

			if (!src_seen[i] || !dst_seen[i] || src_res[i] != dst_res[i])
				bad("source/target CQE mismatch");
			slot = (unsigned int)src_res[i];
			if (bitmap[slot / 64U] & (UINT64_C(1) << (slot % 64U)))
				bad("duplicate installed slot");
			bitmap[slot / 64U] |= UINT64_C(1) << (slot % 64U);
		}
	}
	if (timed)
		elapsed = now_ns() - started;
	for (base = 0; base < slots; base++)
		if (!(bitmap[base / 64U] & (UINT64_C(1) << (base % 64U))))
			bad("missing installed slot");
	verify_slots(&dst, sentinel, slots); /* deliberately outside timing */
	if (*src->sq_dropped || *src->cq_overflow || *dst.sq_dropped ||
	    *dst.cq_overflow || load(src->sq_tail) != load(src->sq_head) ||
	    load(src->cq_tail) != load(src->cq_head) ||
	    load(dst.sq_tail) != load(dst.sq_head) ||
	    load(dst.cq_tail) != load(dst.cq_head))
		bad("ring not empty or overflowed");
	unregister_files(&dst);
	ring_destroy(&dst);
	return elapsed;
}

int main(int argc, char **argv)
{
	struct ring source;
	unsigned char sentinel[SENTINEL_BYTES];
	uint64_t elapsed, sum = 0;
	unsigned int i;
	int memfd;
	bool smoke = false;

	if (argc == 2 && !strcmp(argv[1], "--smoke"))
		smoke = true;
	else if (argc != 1) {
		fprintf(stderr, "usage: %s [--smoke]\n", argv[0]);
		return EXIT_FAILURE;
	}

	pin_cpu();
	for (i = 0; i < SENTINEL_BYTES; i++)
		sentinel[i] = (unsigned char)((i * 131U + 17U) & 0xffU);
	memfd = (int)syscall(__NR_memfd_create, "msg-ring-f0-short", MFD_CLOEXEC);
	if (memfd < 0)
		die("memfd_create");
	if (pwrite(memfd, sentinel, sizeof(sentinel), 0) != sizeof(sentinel))
		die("pwrite sentinel");
	ring_init(&source);
	register_source(&source, memfd);
	run_round(&source, sentinel, SMOKE_SLOTS, false);
	if (smoke) {
		printf("semantic_pass=1 installs=%u cpu=%u\n", SMOKE_SLOTS, CPU);
		goto out;
	}
	for (i = 0; i < WARMUPS; i++)
		run_round(&source, sentinel, SLOTS, false);
	printf("round\tinstalls\telapsed_ns\tns_per_install\tsemantic_pass\tcpu\n");
	for (i = 0; i < ROUNDS; i++) {
		elapsed = run_round(&source, sentinel, SLOTS, true);
		sum += elapsed;
		printf("%u\t%u\t%" PRIu64 "\t%.6f\t1\t%u\n", i, SLOTS,
		       elapsed, (double)elapsed / SLOTS, CPU);
	}
	printf("mean\t%u\t%" PRIu64 "\t%.6f\t1\t%u\n", SLOTS, sum,
	       (double)sum / (ROUNDS * SLOTS), CPU);
out:
	unregister_files(&source);
	ring_destroy(&source);
	close(memfd);
	return EXIT_SUCCESS;
}
