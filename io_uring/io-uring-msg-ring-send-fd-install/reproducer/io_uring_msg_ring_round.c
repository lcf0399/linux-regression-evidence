// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Fixed io_uring/msg_ring.c workloads.
 *
 * Raw UAPI only.  The three profiles are deliberately separate metrics:
 *   d0: ordinary source -> ordinary target IORING_MSG_DATA
 *   r0: ordinary source -> DEFER_TASKRUN target IORING_MSG_DATA
 *   f0: source fixed file -> target sparse fixed-file table SEND_FD
 */

#define _GNU_SOURCE
#define _FILE_OFFSET_BITS 64

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
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
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define SCHEMA_VERSION 1U
#define CONTRACT_CPU 2U
#define RING_ENTRIES 256U
#define QD 64U
#define DATA_OPS_PER_ROUND 262144U
#define FD_OPS_PER_ROUND 4096U
#define DATA_SMOKE_OPS 1024U
#define FD_SMOKE_OPS 256U
#define DATA_TRACE_OPS 512U
#define FD_TRACE_OPS 128U
#define WARMUP_ROUNDS 3U
#define MEASURED_ROUNDS 15U
#define SENTINEL_BYTES 4096U
#define ENTER_TIMEOUT_SECONDS 5LL
#define USER_DATA_SEQ_MASK ((1ULL << 48) - 1)
#define USER_DATA_TAG_MASK (~USER_DATA_SEQ_MASK)
#define SOURCE_TAG 0x5301000000000000ULL
#define TARGET_TAG 0x5401000000000000ULL
#define VERIFY_TAG 0x5601000000000000ULL

enum profile_id {
	PROFILE_UNSET,
	PROFILE_D0,
	PROFILE_R0,
	PROFILE_F0,
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
	void *sq_ptr;
	void *cq_ptr;
	void *sqes_ptr;
	size_t sq_sz;
	size_t cq_sz;
	size_t sqes_sz;
	bool single_mmap;
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

struct result {
	uint64_t logical_ops;
	uint64_t sqes;
	uint64_t source_cqes;
	uint64_t target_cqes;
	uint64_t source_enters;
	uint64_t target_enters;
	uint64_t verify_enters;
	uint64_t verify_reads;
	uint64_t slots_installed;
	uint64_t slots_verified;
	uint64_t successful_ops;
	uint64_t elapsed_ns;
	uint64_t bad_results;
	uint64_t bad_flags;
	uint64_t bad_user_data;
	uint64_t duplicate_events;
	uint64_t missing_events;
	uint64_t pair_mismatches;
	uint64_t sentinel_mismatches;
	uint64_t timeouts;
	uint64_t cleanup_failures;
	uint64_t source_dropped;
	uint64_t source_overflow;
	uint64_t target_dropped;
	uint64_t target_overflow;
	uint64_t source_outstanding;
	uint64_t target_outstanding;
	bool semantic_pass;
};

struct workload {
	struct raw_ring source;
	struct raw_ring target;
	bool target_initialized;
	int memfd;
	bool source_files_registered;
	unsigned char sentinel[SENTINEL_BYTES];
	char kernel_release[128];
	char boot_id[128];
};

struct batch_map {
	bool source_seen[QD];
	bool target_seen[QD];
	bool source_ok[QD];
	bool target_ok[QD];
	int source_res[QD];
	int target_res[QD];
};

static unsigned int load_acquire_u32(const unsigned int *ptr)
{
	return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

static void store_release_u32(unsigned int *ptr, unsigned int value)
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
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
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
		fprintf(stderr, "CPU %u is not online (online CPUs: %ld)\n",
			CONTRACT_CPU, online);
		return -1;
	}
	CPU_ZERO(&set);
	CPU_SET(CONTRACT_CPU, &set);
	if (sched_setaffinity(0, sizeof(set), &set) != 0) {
		perror("sched_setaffinity");
		return -1;
	}
	if (sched_getcpu() != (int)CONTRACT_CPU) {
		fprintf(stderr, "failed to remain on CPU %u\n", CONTRACT_CPU);
		return -1;
	}
	return 0;
}

static void raw_ring_destroy(struct raw_ring *ring)
{
	if (ring->sqes_ptr)
		munmap(ring->sqes_ptr, ring->sqes_sz);
	if (ring->sq_ptr)
		munmap(ring->sq_ptr, ring->sq_sz);
	if (!ring->single_mmap && ring->cq_ptr)
		munmap(ring->cq_ptr, ring->cq_sz);
	if (ring->fd >= 0)
		close(ring->fd);
	memset(ring, 0, sizeof(*ring));
	ring->fd = -1;
}

static int raw_ring_init(struct raw_ring *ring, unsigned int setup_flags)
{
	struct io_uring_params p;
	size_t sq_sz, cq_sz, shared_sz;
	void *sq, *cq;

	memset(ring, 0, sizeof(*ring));
	ring->fd = -1;
	memset(&p, 0, sizeof(p));
	p.flags = setup_flags;
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
			       MAP_SHARED | MAP_POPULATE, ring->fd, IORING_OFF_SQES);
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
	if (*ring->sq_entries < QD || *ring->cq_entries < QD) {
		fprintf(stderr, "kernel returned undersized ring\n");
		goto fail;
	}
	return 0;
fail:
	raw_ring_destroy(ring);
	return -1;
}

static uint64_t ring_outstanding(const struct raw_ring *ring)
{
	return (uint64_t)(load_acquire_u32(ring->sq_tail) -
			  load_acquire_u32(ring->sq_head)) +
	       (uint64_t)(load_acquire_u32(ring->cq_tail) -
			  load_acquire_u32(ring->cq_head));
}

static int bounded_enter(struct raw_ring *ring, unsigned int to_submit,
			 unsigned int min_complete, uint64_t *timeouts)
{
	struct __kernel_timespec timeout;
	struct io_uring_getevents_arg arg;
	int ret;

	for (;;) {
		memset(&timeout, 0, sizeof(timeout));
		timeout.tv_sec = ENTER_TIMEOUT_SECONDS;
		memset(&arg, 0, sizeof(arg));
		arg.ts = (uintptr_t)&timeout;
		ret = (int)syscall(__NR_io_uring_enter, ring->fd, to_submit,
				   min_complete,
				   IORING_ENTER_GETEVENTS | IORING_ENTER_EXT_ARG,
				   &arg, sizeof(arg));
		if (ret < 0 && errno == EINTR)
			continue;
		break;
	}
	if (ret < 0) {
		if (errno == ETIME && timeouts)
			(*timeouts)++;
		perror("io_uring_enter");
		return -1;
	}
	if (ret != (int)to_submit) {
		fprintf(stderr, "io_uring_enter=%d expected submitted=%u\n",
			ret, to_submit);
		return -1;
	}
	return 0;
}

static int reserve_batch(struct raw_ring *ring, unsigned int count,
			 unsigned int *tail_out)
{
	unsigned int head = load_acquire_u32(ring->sq_head);
	unsigned int tail = load_acquire_u32(ring->sq_tail);

	if (tail - head + count > *ring->sq_entries) {
		fprintf(stderr, "SQ lacks %u free entries\n", count);
		return -1;
	}
	*tail_out = tail;
	return 0;
}

static void publish_batch(struct raw_ring *ring, unsigned int tail)
{
	store_release_u32(ring->sq_tail, tail);
}

static uint64_t tagged_user_data(uint64_t tag, uint64_t sequence)
{
	return tag | (sequence & USER_DATA_SEQ_MASK);
}

static int decode_sequence(uint64_t user_data, uint64_t tag,
			   uint64_t base, unsigned int count,
			   unsigned int *index)
{
	uint64_t sequence;

	if ((user_data & USER_DATA_TAG_MASK) != tag)
		return -1;
	sequence = user_data & USER_DATA_SEQ_MASK;
	if (sequence < base || sequence >= base + count)
		return -1;
	*index = (unsigned int)(sequence - base);
	return 0;
}

static int prepare_data_batch(struct workload *work, uint64_t base,
			      unsigned int count)
{
	struct raw_ring *ring = &work->source;
	unsigned int tail;
	unsigned int i;

	if (reserve_batch(ring, count, &tail) != 0)
		return -1;
	for (i = 0; i < count; i++) {
		uint64_t sequence = base + i;
		unsigned int index = tail & *ring->sq_mask;
		struct io_uring_sqe *sqe = &ring->sqes[index];

		memset(sqe, 0, sizeof(*sqe));
		sqe->opcode = IORING_OP_MSG_RING;
		sqe->fd = work->target.fd;
		sqe->addr = IORING_MSG_DATA;
		sqe->off = tagged_user_data(TARGET_TAG, sequence);
		sqe->len = 1U + (uint32_t)(sequence % 1000000ULL);
		sqe->user_data = tagged_user_data(SOURCE_TAG, sequence);
		ring->sq_array[index] = index;
		tail++;
	}
	publish_batch(ring, tail);
	return 0;
}

static void drain_data_cq(struct raw_ring *ring, bool source,
			  uint64_t base, unsigned int count,
			  struct batch_map *map, struct result *result)
{
	unsigned int head = load_acquire_u32(ring->cq_head);
	unsigned int tail = load_acquire_u32(ring->cq_tail);
	unsigned int available = tail - head;
	unsigned int i;

	if (available != count) {
		if (available < count)
			result->missing_events += count - available;
		else
			result->bad_user_data += available - count;
	}
	for (i = 0; i < available; i++) {
		struct io_uring_cqe *cqe =
			&ring->cqes[(head + i) & *ring->cq_mask];
		unsigned int index;
		bool *seen;
		bool *ok;
		int *res;
		uint64_t tag = source ? SOURCE_TAG : TARGET_TAG;

		if (source)
			result->source_cqes++;
		else
			result->target_cqes++;
		if (decode_sequence(cqe->user_data, tag, base, count, &index) != 0) {
			result->bad_user_data++;
			continue;
		}
		seen = source ? map->source_seen : map->target_seen;
		ok = source ? map->source_ok : map->target_ok;
		res = source ? map->source_res : map->target_res;
		if (seen[index]) {
			result->duplicate_events++;
			ok[index] = false;
			continue;
		}
		seen[index] = true;
		ok[index] = true;
		res[index] = cqe->res;
		if (cqe->flags != 0) {
			result->bad_flags++;
			ok[index] = false;
		}
		if ((source && cqe->res != 0) ||
		    (!source && cqe->res !=
		     (int)(1U + ((base + index) % 1000000ULL)))) {
			result->bad_results++;
			ok[index] = false;
		}
	}
	store_release_u32(ring->cq_head, tail);
}

static int run_data_once(struct workload *work, enum profile_id profile,
			 unsigned int operations, bool timed,
			 struct result *result)
{
	uint64_t started = 0;
	uint64_t base;

	memset(result, 0, sizeof(*result));
	result->logical_ops = operations;
	if (!operations || operations % QD)
		return -1;
	if (timed)
		started = now_ns();
	for (base = 0; base < operations; base += QD) {
		struct batch_map map;
		unsigned int i;

		memset(&map, 0, sizeof(map));
		if (prepare_data_batch(work, base, QD) != 0)
			return -1;
		result->sqes += QD;
		if (bounded_enter(&work->source, QD, QD, &result->timeouts) != 0)
			return -1;
		result->source_enters++;
		drain_data_cq(&work->source, true, base, QD, &map, result);
		if (profile == PROFILE_R0) {
			if (bounded_enter(&work->target, 0, QD,
					  &result->timeouts) != 0)
				return -1;
			result->target_enters++;
		}
		drain_data_cq(&work->target, false, base, QD, &map, result);
		for (i = 0; i < QD; i++) {
			if (!map.source_seen[i])
				result->missing_events++;
			if (!map.target_seen[i])
				result->missing_events++;
			if (map.source_seen[i] && map.target_seen[i] &&
			    map.source_ok[i] && map.target_ok[i])
				result->successful_ops++;
		}
	}
	if (timed)
		result->elapsed_ns = now_ns() - started;
	if (profile == PROFILE_R0) {
		unsigned int before = load_acquire_u32(work->target.cq_tail) -
			load_acquire_u32(work->target.cq_head);

		if (bounded_enter(&work->target, 0, 0, &result->timeouts) != 0)
			return -1;
		if (load_acquire_u32(work->target.cq_tail) -
		    load_acquire_u32(work->target.cq_head) != before)
			result->bad_results++;
	}
	result->source_dropped = *work->source.sq_dropped;
	result->source_overflow = *work->source.cq_overflow;
	result->target_dropped = *work->target.sq_dropped;
	result->target_overflow = *work->target.cq_overflow;
	result->source_outstanding = ring_outstanding(&work->source);
	result->target_outstanding = ring_outstanding(&work->target);
	result->semantic_pass = result->successful_ops == operations &&
		result->sqes == operations && result->source_cqes == operations &&
		result->target_cqes == operations && result->bad_results == 0 &&
		result->bad_flags == 0 && result->bad_user_data == 0 &&
		result->duplicate_events == 0 && result->missing_events == 0 &&
		result->timeouts == 0 && result->source_dropped == 0 &&
		result->source_overflow == 0 && result->target_dropped == 0 &&
		result->target_overflow == 0 && result->source_outstanding == 0 &&
		result->target_outstanding == 0;
	return result->semantic_pass ? 0 : -1;
}

static int register_source_file(struct workload *work)
{
	int fd = work->memfd;
	int ret;

	ret = (int)syscall(__NR_io_uring_register, work->source.fd,
			   IORING_REGISTER_FILES, &fd, 1);
	if (ret != 0) {
		perror("io_uring_register source file");
		return -1;
	}
	work->source_files_registered = true;
	return 0;
}

static int unregister_files(struct raw_ring *ring)
{
	int ret = (int)syscall(__NR_io_uring_register, ring->fd,
			       IORING_UNREGISTER_FILES, NULL, 0);

	if (ret != 0)
		perror("io_uring_unregister_files");
	return ret;
}

static int register_sparse_files(struct raw_ring *ring, unsigned int slots)
{
	struct io_uring_rsrc_register reg;
	int ret;

	memset(&reg, 0, sizeof(reg));
	reg.nr = slots;
	reg.flags = IORING_RSRC_REGISTER_SPARSE;
	ret = (int)syscall(__NR_io_uring_register, ring->fd,
			   IORING_REGISTER_FILES2, &reg, sizeof(reg));
	if (ret != 0)
		perror("io_uring_register sparse files");
	return ret;
}

static int setup_sentinel_file(struct workload *work)
{
	unsigned int i;
	ssize_t nr;

	work->memfd = (int)syscall(__NR_memfd_create, "msg-ring-f0",
				   MFD_CLOEXEC);
	if (work->memfd < 0) {
		perror("memfd_create");
		return -1;
	}
	for (i = 0; i < SENTINEL_BYTES; i++)
		work->sentinel[i] = (unsigned char)((i * 131U + 17U) & 0xffU);
	nr = pwrite(work->memfd, work->sentinel, sizeof(work->sentinel), 0);
	if (nr != (ssize_t)sizeof(work->sentinel)) {
		perror("pwrite sentinel");
		return -1;
	}
	return register_source_file(work);
}

static int prepare_send_fd_batch(struct workload *work, struct raw_ring *target,
				 uint64_t base, unsigned int count)
{
	struct raw_ring *ring = &work->source;
	unsigned int tail;
	unsigned int i;

	if (reserve_batch(ring, count, &tail) != 0)
		return -1;
	for (i = 0; i < count; i++) {
		uint64_t sequence = base + i;
		unsigned int index = tail & *ring->sq_mask;
		struct io_uring_sqe *sqe = &ring->sqes[index];

		memset(sqe, 0, sizeof(*sqe));
		sqe->opcode = IORING_OP_MSG_RING;
		sqe->fd = target->fd;
		sqe->addr = IORING_MSG_SEND_FD;
		sqe->off = tagged_user_data(TARGET_TAG, sequence);
		sqe->addr3 = 0;
		sqe->file_index = IORING_FILE_INDEX_ALLOC;
		sqe->user_data = tagged_user_data(SOURCE_TAG, sequence);
		ring->sq_array[index] = index;
		tail++;
	}
	publish_batch(ring, tail);
	return 0;
}

static void drain_send_fd_cq(struct raw_ring *ring, bool source,
			     uint64_t base, unsigned int count,
			     unsigned int total_slots,
			     struct batch_map *map, struct result *result)
{
	unsigned int head = load_acquire_u32(ring->cq_head);
	unsigned int tail = load_acquire_u32(ring->cq_tail);
	unsigned int available = tail - head;
	unsigned int i;

	if (available != count) {
		if (available < count)
			result->missing_events += count - available;
		else
			result->bad_user_data += available - count;
	}
	for (i = 0; i < available; i++) {
		struct io_uring_cqe *cqe =
			&ring->cqes[(head + i) & *ring->cq_mask];
		unsigned int index;
		bool *seen;
		bool *ok;
		int *res;
		uint64_t tag = source ? SOURCE_TAG : TARGET_TAG;

		if (source)
			result->source_cqes++;
		else
			result->target_cqes++;
		if (decode_sequence(cqe->user_data, tag, base, count, &index) != 0) {
			result->bad_user_data++;
			continue;
		}
		seen = source ? map->source_seen : map->target_seen;
		ok = source ? map->source_ok : map->target_ok;
		res = source ? map->source_res : map->target_res;
		if (seen[index]) {
			result->duplicate_events++;
			ok[index] = false;
			continue;
		}
		seen[index] = true;
		ok[index] = true;
		res[index] = cqe->res;
		if (cqe->flags != 0) {
			result->bad_flags++;
			ok[index] = false;
		}
		if (cqe->res < 0 || (unsigned int)cqe->res >= total_slots) {
			result->bad_results++;
			ok[index] = false;
		}
	}
	store_release_u32(ring->cq_head, tail);
}

static int verify_fixed_files(struct workload *work, struct raw_ring *target,
			      unsigned int slots, struct result *result)
{
	unsigned char buffers[QD];
	uint64_t base;

	for (base = 0; base < slots; base += QD) {
		bool seen[QD];
		unsigned int tail;
		unsigned int head, cq_tail, available;
		unsigned int i;

		memset(buffers, 0, sizeof(buffers));
		memset(seen, 0, sizeof(seen));
		if (reserve_batch(target, QD, &tail) != 0)
			return -1;
		for (i = 0; i < QD; i++) {
			unsigned int slot = (unsigned int)base + i;
			unsigned int index = tail & *target->sq_mask;
			struct io_uring_sqe *sqe = &target->sqes[index];

			memset(sqe, 0, sizeof(*sqe));
			sqe->opcode = IORING_OP_READ;
			sqe->flags = IOSQE_FIXED_FILE;
			sqe->fd = (int)slot;
			sqe->off = slot % SENTINEL_BYTES;
			sqe->addr = (uintptr_t)&buffers[i];
			sqe->len = 1;
			sqe->user_data = tagged_user_data(VERIFY_TAG, slot);
			target->sq_array[index] = index;
			tail++;
		}
		publish_batch(target, tail);
		if (bounded_enter(target, QD, QD, &result->timeouts) != 0)
			return -1;
		result->verify_enters++;
		head = load_acquire_u32(target->cq_head);
		cq_tail = load_acquire_u32(target->cq_tail);
		available = cq_tail - head;
		if (available != QD) {
			result->missing_events += available < QD ? QD - available : 0;
			result->bad_user_data += available > QD ? available - QD : 0;
		}
		for (i = 0; i < available; i++) {
			struct io_uring_cqe *cqe =
				&target->cqes[(head + i) & *target->cq_mask];
			unsigned int index;

			if (decode_sequence(cqe->user_data, VERIFY_TAG, base, QD,
					    &index) != 0) {
				result->bad_user_data++;
				continue;
			}
			if (seen[index]) {
				result->duplicate_events++;
				continue;
			}
			seen[index] = true;
			if (cqe->res != 1)
				result->bad_results++;
			if (cqe->flags != 0)
				result->bad_flags++;
		}
		store_release_u32(target->cq_head, cq_tail);
		for (i = 0; i < QD; i++) {
			unsigned int slot = (unsigned int)base + i;

			if (!seen[i])
				result->missing_events++;
			else
				result->verify_reads++;
			if (buffers[i] != work->sentinel[slot % SENTINEL_BYTES])
				result->sentinel_mismatches++;
			else
				result->slots_verified++;
		}
	}
	return 0;
}

static int run_send_fd_once(struct workload *work, unsigned int operations,
			    bool timed, struct result *result)
{
	struct raw_ring target;
	uint64_t *slot_bitmap = NULL;
	uint64_t started = 0;
	uint64_t base;
	int ret = -1;
	bool target_registered = false;

	memset(result, 0, sizeof(*result));
	result->logical_ops = operations;
	memset(&target, 0, sizeof(target));
	target.fd = -1;
	if (!operations || operations % QD)
		return -1;
	slot_bitmap = calloc((operations + 63U) / 64U, sizeof(*slot_bitmap));
	if (!slot_bitmap)
		return -1;
	if (raw_ring_init(&target, 0) != 0)
		goto out;
	if (register_sparse_files(&target, operations) != 0)
		goto out;
	target_registered = true;
	if (timed)
		started = now_ns();
	for (base = 0; base < operations; base += QD) {
		struct batch_map map;
		unsigned int i;

		memset(&map, 0, sizeof(map));
		if (prepare_send_fd_batch(work, &target, base, QD) != 0)
			goto out;
		result->sqes += QD;
		if (bounded_enter(&work->source, QD, QD, &result->timeouts) != 0)
			goto out;
		result->source_enters++;
		drain_send_fd_cq(&work->source, true, base, QD, operations,
				     &map, result);
		drain_send_fd_cq(&target, false, base, QD, operations,
				     &map, result);
		for (i = 0; i < QD; i++) {
			int slot;

			if (!map.source_seen[i])
				result->missing_events++;
			if (!map.target_seen[i])
				result->missing_events++;
			if (!map.source_seen[i] || !map.target_seen[i] ||
			    !map.source_ok[i] || !map.target_ok[i])
				continue;
			if (map.source_res[i] != map.target_res[i]) {
				result->pair_mismatches++;
				continue;
			}
			slot = map.source_res[i];
			if (slot_bitmap[(unsigned int)slot / 64U] &
			    (1ULL << ((unsigned int)slot % 64U))) {
				result->duplicate_events++;
				continue;
			}
			slot_bitmap[(unsigned int)slot / 64U] |=
				1ULL << ((unsigned int)slot % 64U);
			result->successful_ops++;
			result->slots_installed++;
		}
	}
	if (timed)
		result->elapsed_ns = now_ns() - started;
	for (base = 0; base < operations; base++) {
		if (!(slot_bitmap[base / 64U] & (1ULL << (base % 64U))))
			result->missing_events++;
	}
	if (result->missing_events == 0 &&
	    verify_fixed_files(work, &target, operations, result) != 0)
		goto out;
	ret = 0;
out:
	result->source_dropped = *work->source.sq_dropped;
	result->source_overflow = *work->source.cq_overflow;
	if (target.fd >= 0) {
		result->target_dropped = *target.sq_dropped;
		result->target_overflow = *target.cq_overflow;
		result->target_outstanding = ring_outstanding(&target);
	}
	result->source_outstanding = ring_outstanding(&work->source);
	if (target_registered && unregister_files(&target) != 0)
		result->cleanup_failures++;
	raw_ring_destroy(&target);
	free(slot_bitmap);
	result->semantic_pass = ret == 0 &&
		result->successful_ops == operations && result->sqes == operations &&
		result->source_cqes == operations &&
		result->target_cqes == operations &&
		result->verify_reads == operations &&
		result->slots_installed == operations &&
		result->slots_verified == operations &&
		result->bad_results == 0 && result->bad_flags == 0 &&
		result->bad_user_data == 0 && result->duplicate_events == 0 &&
		result->missing_events == 0 && result->pair_mismatches == 0 &&
		result->sentinel_mismatches == 0 && result->timeouts == 0 &&
		result->cleanup_failures == 0 && result->source_dropped == 0 &&
		result->source_overflow == 0 && result->target_dropped == 0 &&
		result->target_overflow == 0 && result->source_outstanding == 0 &&
		result->target_outstanding == 0;
	return result->semantic_pass ? 0 : -1;
}

static const char *profile_name(enum profile_id profile)
{
	switch (profile) {
	case PROFILE_D0: return "D0_DIRECT_DATA";
	case PROFILE_R0: return "R0_DEFERRED_REMOTE_DATA";
	case PROFILE_F0: return "F0_DIRECT_SEND_FD";
	default: return "unset";
	}
}

static const char *phase_name(enum phase_id phase)
{
	switch (phase) {
	case PHASE_SMOKE: return "semantic-smoke";
	case PHASE_TRACE: return "direct-hit-trace";
	case PHASE_WARMUP: return "warmup";
	case PHASE_MEASURED: return "measured";
	default: return "unknown";
	}
}

static void emit_preamble(FILE *out, const struct workload *work)
{
	fprintf(out, "# kernel_release=%s\n# boot_id=%s\n# compiler=%s\n",
		work->kernel_release, work->boot_id, __VERSION__);
	fprintf(out, "schema\tprofile\tphase\tround\tlogical_ops\tsqes"
		"\tsource_cqes\ttarget_cqes\tsource_enters\ttarget_enters"
		"\tverify_enters\tverify_reads\tslots_installed\tslots_verified"
		"\telapsed_ns\tns_per_op\tsuccessful_ops\tbad_results"
		"\tbad_flags\tbad_user_data\tduplicate_events\tmissing_events"
		"\tpair_mismatches\tsentinel_mismatches\ttimeouts"
		"\tcleanup_failures\tsource_dropped\tsource_overflow"
		"\ttarget_dropped\ttarget_overflow\tsource_outstanding"
		"\ttarget_outstanding\tsemantic_pass\tcpu\n");
}

static void emit_result(FILE *out, enum profile_id profile,
			struct result *result, enum phase_id phase,
			unsigned int round)
{
	double ns_per_op = result->elapsed_ns && result->logical_ops ?
		(double)result->elapsed_ns / (double)result->logical_ops : 0.0;

	fprintf(out, "%u\t%s\t%s\t%u\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%.6f\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%u\t%u\n",
		SCHEMA_VERSION, profile_name(profile), phase_name(phase), round,
		result->logical_ops, result->sqes, result->source_cqes,
		result->target_cqes, result->source_enters, result->target_enters,
		result->verify_enters, result->verify_reads,
		result->slots_installed, result->slots_verified,
		result->elapsed_ns, ns_per_op, result->successful_ops,
		result->bad_results, result->bad_flags, result->bad_user_data,
		result->duplicate_events, result->missing_events,
		result->pair_mismatches, result->sentinel_mismatches,
		result->timeouts, result->cleanup_failures,
		result->source_dropped, result->source_overflow,
		result->target_dropped, result->target_overflow,
		result->source_outstanding, result->target_outstanding,
		result->semantic_pass ? 1U : 0U, CONTRACT_CPU);
	fflush(out);
}

static int execute_once(struct workload *work, enum profile_id profile,
			unsigned int operations, bool timed,
			struct result *result)
{
	if (profile == PROFILE_F0)
		return run_send_fd_once(work, operations, timed, result);
	return run_data_once(work, profile, operations, timed, result);
}

static unsigned int operations_for(enum profile_id profile, enum mode_id mode)
{
	if (profile == PROFILE_F0) {
		if (mode == MODE_SMOKE)
			return FD_SMOKE_OPS;
		if (mode == MODE_TRACE)
			return FD_TRACE_OPS;
		return FD_OPS_PER_ROUND;
	}
	if (mode == MODE_SMOKE)
		return DATA_SMOKE_OPS;
	if (mode == MODE_TRACE)
		return DATA_TRACE_OPS;
	return DATA_OPS_PER_ROUND;
}

static int run_mode(struct workload *work, const struct options *opt, FILE *out)
{
	struct result result;
	unsigned int operations = operations_for(opt->profile, opt->mode);
	unsigned int round;
	enum phase_id phase;

	if (opt->mode == MODE_SMOKE || opt->mode == MODE_TRACE) {
		phase = opt->mode == MODE_SMOKE ? PHASE_SMOKE : PHASE_TRACE;
		if (execute_once(work, opt->profile, operations, false, &result) != 0) {
			emit_result(out, opt->profile, &result, phase, 0);
			return -1;
		}
		emit_result(out, opt->profile, &result, phase, 0);
		return 0;
	}
	if (execute_once(work, opt->profile,
			 operations_for(opt->profile, MODE_SMOKE), false,
			 &result) != 0)
		return -1;
	for (round = 0; round < WARMUP_ROUNDS; round++) {
		if (execute_once(work, opt->profile, operations, false, &result) != 0)
			return -1;
	}
	for (round = 0; round < MEASURED_ROUNDS; round++) {
		if (execute_once(work, opt->profile, operations, true, &result) != 0) {
			emit_result(out, opt->profile, &result, PHASE_MEASURED, round);
			return -1;
		}
		emit_result(out, opt->profile, &result, PHASE_MEASURED, round);
	}
	return 0;
}

static void describe(enum profile_id profile)
{
	printf("profile=%s\n", profile_name(profile));
	printf("old_release=6.12.95-bm-6.12.95\n");
	printf("new_release=7.1.3-bm-7.1.3\n");
	printf("cpu=%u\nring_entries=%u\nqueue_depth=%u\n",
		CONTRACT_CPU, RING_ENTRIES, QD);
	printf("data_ops_per_round=%u\nfd_ops_per_round=%u\n",
		DATA_OPS_PER_ROUND, FD_OPS_PER_ROUND);
	printf("warmup_rounds=%u\nmeasured_rounds=%u\n",
		WARMUP_ROUNDS, MEASURED_ROUNDS);
	printf("enter_timeout_seconds=%lld\n", ENTER_TIMEOUT_SECONDS);
	printf("execution_started=false\n");
}

static int parse_profile(const char *text, enum profile_id *profile)
{
	if (!strcmp(text, "d0"))
		*profile = PROFILE_D0;
	else if (!strcmp(text, "r0"))
		*profile = PROFILE_R0;
	else if (!strcmp(text, "f0"))
		*profile = PROFILE_F0;
	else
		return -1;
	return 0;
}

static int parse_mode(const char *text, enum mode_id *mode)
{
	if (!strcmp(text, "describe"))
		*mode = MODE_DESCRIBE;
	else if (!strcmp(text, "smoke"))
		*mode = MODE_SMOKE;
	else if (!strcmp(text, "trace"))
		*mode = MODE_TRACE;
	else if (!strcmp(text, "point"))
		*mode = MODE_POINT;
	else
		return -1;
	return 0;
}

static int parse_options(int argc, char **argv, struct options *opt)
{
	int i;

	memset(opt, 0, sizeof(*opt));
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--execute"))
			opt->execute = true;
		else if (!strcmp(argv[i], "--profile") && i + 1 < argc) {
			if (parse_profile(argv[++i], &opt->profile) != 0)
				return -1;
		} else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
			if (parse_mode(argv[++i], &opt->mode) != 0)
				return -1;
		} else if (!strcmp(argv[i], "--output") && i + 1 < argc) {
			opt->output_path = argv[++i];
		} else {
			return -1;
		}
	}
	if (opt->profile == PROFILE_UNSET || opt->mode == MODE_UNSET)
		return -1;
	if (opt->mode == MODE_DESCRIBE)
		return 0;
	return opt->execute ? 0 : -1;
}

static int setup_workload(struct workload *work, enum profile_id profile)
{
	unsigned int target_flags = 0;

	if (pin_cpu() != 0 || raw_ring_init(&work->source, 0) != 0)
		return -1;
	if (profile == PROFILE_F0)
		return setup_sentinel_file(work);
	if (profile == PROFILE_R0)
		target_flags = IORING_SETUP_SINGLE_ISSUER |
			       IORING_SETUP_DEFER_TASKRUN;
	if (raw_ring_init(&work->target, target_flags) != 0)
		return -1;
	work->target_initialized = true;
	return 0;
}

static int cleanup_workload(struct workload *work)
{
	int failures = 0;

	if (work->source_files_registered) {
		if (unregister_files(&work->source) != 0)
			failures++;
		work->source_files_registered = false;
	}
	if (work->target_initialized)
		raw_ring_destroy(&work->target);
	raw_ring_destroy(&work->source);
	if (work->memfd >= 0)
		close(work->memfd);
	return failures ? -1 : 0;
}

int main(int argc, char **argv)
{
	struct options opt;
	struct workload work;
	struct utsname uts;
	FILE *out = stdout;
	int run_ret;
	int cleanup_ret;

	if (parse_options(argc, argv, &opt) != 0) {
		fprintf(stderr, "usage: %s --profile d0|r0|f0 --mode describe|smoke|trace|point [--execute] [--output PATH]\n",
			argv[0]);
		return EXIT_FAILURE;
	}
	if (opt.mode == MODE_DESCRIBE) {
		describe(opt.profile);
		return EXIT_SUCCESS;
	}
	memset(&work, 0, sizeof(work));
	work.source.fd = -1;
	work.target.fd = -1;
	work.memfd = -1;
	if (uname(&uts) != 0)
		return EXIT_FAILURE;
	strncpy(work.kernel_release, uts.release,
		sizeof(work.kernel_release) - 1);
	if (read_text("/proc/sys/kernel/random/boot_id", work.boot_id,
		      sizeof(work.boot_id)) != 0)
		strcpy(work.boot_id, "unknown");
	if (opt.output_path) {
		out = fopen(opt.output_path, "wx");
		if (!out) {
			perror("output (refuses overwrite)");
			return EXIT_FAILURE;
		}
	}
	emit_preamble(out, &work);
	if (setup_workload(&work, opt.profile) != 0) {
		run_ret = -1;
		goto out;
	}
	run_ret = run_mode(&work, &opt, out);
out:
	cleanup_ret = cleanup_workload(&work);
	if (out != stdout)
		fclose(out);
	return run_ret == 0 && cleanup_ret == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
