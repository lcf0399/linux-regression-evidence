// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Fixed first-round io_uring/net.c workloads.
 *
 * Raw UAPI only; no liburing dependency.  Every profile uses AF_UNIX
 * SOCK_DGRAM so message boundaries and queue state can be checked exactly.
 *
 *   s0: IORING_OP_SEND with one contiguous buffer
 *   s1: IORING_OP_SENDMSG with sixteen iovecs
 *   r0: IORING_OP_RECV with one contiguous buffer
 *   r1: IORING_OP_RECVMSG with sixteen iovecs
 *   m0: multishot IORING_OP_RECV with a provided-buffer ring
 *   c0: IORING_OP_NOP control with the same queue depth
 *   u0: batched sendmmsg() control on the same AF_UNIX socketpair
 *
 * Socket setup, payload construction, peer fill/drain, payload validation,
 * pbuf publication, and multishot cancellation are outside timed regions.
 */

#define _GNU_SOURCE

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/io_uring.h>
#include <linux/sockios.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <sys/uio.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define SCHEMA_VERSION 1U
#define CONTRACT_CPU 2U
#define RING_ENTRIES 128U
#define QD 32U
#define PAYLOAD_BYTES 128U
#define IOV_COUNT 16U
#define IOV_BYTES (PAYLOAD_BYTES / IOV_COUNT)
#define PBUF_ENTRIES 64U
#define PBUF_BGID 23U
#define ORDINARY_OPS_PER_ROUND 65536U
#define MSHOT_OPS_PER_ROUND 32768U
#define SMOKE_ORDINARY_OPS 1024U
#define SMOKE_MSHOT_OPS 1024U
#define TRACE_ORDINARY_OPS 512U
#define TRACE_MSHOT_OPS 512U
#define WARMUP_ROUNDS 3U
#define MEASURED_ROUNDS 15U
#define MSHOT_TAG 0x4d00000000000000ULL
#define CANCEL_TAG 0x4300000000000000ULL

_Static_assert(PAYLOAD_BYTES % IOV_COUNT == 0, "iov shape must be exact");
_Static_assert((PBUF_ENTRIES & (PBUF_ENTRIES - 1)) == 0,
	       "pbuf entries must be a power of two");
_Static_assert(PBUF_ENTRIES >= 2 * QD, "pbuf ring must cover two batches");

enum profile_id {
	PROFILE_UNSET,
	PROFILE_S0,
	PROFILE_S1,
	PROFILE_R0,
	PROFILE_R1,
	PROFILE_M0,
	PROFILE_C0,
	PROFILE_U0,
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

struct workload {
	struct raw_ring ring;
	int sockets[2];
	unsigned char send_buf[QD][PAYLOAD_BYTES];
	unsigned char recv_buf[QD][PAYLOAD_BYTES];
	struct iovec iov[QD][IOV_COUNT];
	struct msghdr msg[QD];
	struct io_uring_cqe cqe_copy[QD + 2];
	struct io_uring_buf_ring *pbuf_ring;
	void *pbuf[PBUF_ENTRIES];
	uint16_t pbuf_tail;
	bool pbuf_registered;
	char release[128];
	char boot_id[128];
};

struct result {
	uint64_t logical_ops;
	uint64_t logical_bytes;
	uint64_t sqes;
	uint64_t cqes;
	uint64_t timed_enters;
	uint64_t total_enters;
	uint64_t elapsed_ns;
	uint64_t successful_results;
	uint64_t bad_results;
	uint64_t bad_flags;
	uint64_t bad_user_data;
	uint64_t duplicate_bids;
	uint64_t content_mismatches;
	uint64_t eagain_results;
	uint64_t enobufs_results;
	uint64_t cancel_cqes;
	uint64_t pbuf_head_delta;
	uint64_t outstanding;
	uint64_t sq_dropped;
	uint64_t cq_overflow;
	bool semantic_pass;
};

static unsigned int load_acquire_u32(const unsigned int *ptr)
{
	return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

static void store_release_u32(unsigned int *ptr, unsigned int value)
{
	__atomic_store_n(ptr, value, __ATOMIC_RELEASE);
}

static void store_release_u16(uint16_t *ptr, uint16_t value)
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

	CPU_ZERO(&set);
	CPU_SET(CONTRACT_CPU, &set);
	if (sched_setaffinity(0, sizeof(set), &set) != 0) {
		perror("sched_setaffinity");
		return -1;
	}
	return sched_getcpu() == (int)CONTRACT_CPU ? 0 : -1;
}

static int raw_ring_init(struct raw_ring *ring)
{
	struct io_uring_params p;
	size_t sq_sz, cq_sz, shared_sz;
	void *sq, *cq;

	memset(ring, 0, sizeof(*ring));
	ring->fd = -1;
	memset(&p, 0, sizeof(p));
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
			return -1;
		cq = sq;
		ring->sq_sz = shared_sz;
		ring->cq_sz = shared_sz;
	} else {
		sq = mmap(NULL, sq_sz, PROT_READ | PROT_WRITE,
			  MAP_SHARED | MAP_POPULATE, ring->fd, IORING_OFF_SQ_RING);
		if (sq == MAP_FAILED)
			return -1;
		cq = mmap(NULL, cq_sz, PROT_READ | PROT_WRITE,
			  MAP_SHARED | MAP_POPULATE, ring->fd, IORING_OFF_CQ_RING);
		if (cq == MAP_FAILED) {
			munmap(sq, sq_sz);
			return -1;
		}
		ring->sq_sz = sq_sz;
		ring->cq_sz = cq_sz;
	}
	ring->sq_ptr = sq;
	ring->cq_ptr = cq;
	ring->sqes_sz = p.sq_entries * sizeof(struct io_uring_sqe);
	ring->sqes_ptr = mmap(NULL, ring->sqes_sz, PROT_READ | PROT_WRITE,
			       MAP_SHARED | MAP_POPULATE, ring->fd, IORING_OFF_SQES);
	if (ring->sqes_ptr == MAP_FAILED) {
		ring->sqes_ptr = NULL;
		return -1;
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
	if (*ring->sq_entries < QD || *ring->cq_entries < QD + 2)
		return -1;
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

static uint64_t outstanding(const struct raw_ring *ring)
{
	return (uint64_t)(load_acquire_u32(ring->sq_tail) -
			  load_acquire_u32(ring->sq_head)) +
	       (uint64_t)(load_acquire_u32(ring->cq_tail) -
			  load_acquire_u32(ring->cq_head));
}

static int ring_enter(struct raw_ring *ring, unsigned int submit,
		      unsigned int wait, struct result *result, bool timed)
{
	int ret;

	do {
		ret = (int)syscall(__NR_io_uring_enter, ring->fd, submit, wait,
				   wait ? IORING_ENTER_GETEVENTS : 0, NULL, 0);
	} while (ret < 0 && errno == EINTR);
	result->total_enters++;
	if (timed)
		result->timed_enters++;
	if (ret != (int)submit) {
		if (ret < 0)
			perror("io_uring_enter");
		else
			fprintf(stderr, "io_uring_enter=%d expected=%u\n", ret, submit);
		return -1;
	}
	return 0;
}

static struct io_uring_sqe *reserve_sqe(struct raw_ring *ring)
{
	unsigned int head = load_acquire_u32(ring->sq_head);
	unsigned int tail = load_acquire_u32(ring->sq_tail);
	unsigned int index;

	if (tail - head >= *ring->sq_entries)
		return NULL;
	index = tail & *ring->sq_mask;
	memset(&ring->sqes[index], 0, sizeof(ring->sqes[index]));
	ring->sq_array[index] = index;
	store_release_u32(ring->sq_tail, tail + 1);
	return &ring->sqes[index];
}

static int copy_cqes(struct workload *work, unsigned int expected)
{
	struct raw_ring *ring = &work->ring;
	unsigned int head = load_acquire_u32(ring->cq_head);
	unsigned int tail = load_acquire_u32(ring->cq_tail);
	unsigned int i;

	if (tail - head != expected) {
		fprintf(stderr, "CQ count=%u expected=%u\n", tail - head, expected);
		return -1;
	}
	for (i = 0; i < expected; i++)
		work->cqe_copy[i] = ring->cqes[(head + i) & *ring->cq_mask];
	store_release_u32(ring->cq_head, head + expected);
	return 0;
}

static void pattern_fill(void *buffer, uint64_t seq)
{
	unsigned char *p = buffer;
	unsigned int i;

	for (i = 0; i < PAYLOAD_BYTES; i++)
		p[i] = (unsigned char)((seq * 131U + i * 17U + 0x5aU) & 0xffU);
}

static bool pattern_matches(const void *buffer, uint64_t seq)
{
	unsigned char expected[PAYLOAD_BYTES];

	pattern_fill(expected, seq);
	return memcmp(buffer, expected, sizeof(expected)) == 0;
}

static int set_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL);

	return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int setup_sockets(struct workload *work)
{
	int size = 1 << 20;

	if (socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0, work->sockets) != 0)
		return -1;
	if (set_nonblock(work->sockets[0]) != 0 ||
	    set_nonblock(work->sockets[1]) != 0)
		return -1;
	(void)setsockopt(work->sockets[0], SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
	(void)setsockopt(work->sockets[0], SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
	(void)setsockopt(work->sockets[1], SOL_SOCKET, SO_SNDBUF, &size, sizeof(size));
	(void)setsockopt(work->sockets[1], SOL_SOCKET, SO_RCVBUF, &size, sizeof(size));
	return 0;
}

static void prepare_iov(struct workload *work, bool receive)
{
	unsigned int slot, seg;

	for (slot = 0; slot < QD; slot++) {
		memset(&work->msg[slot], 0, sizeof(work->msg[slot]));
		for (seg = 0; seg < IOV_COUNT; seg++) {
			work->iov[slot][seg].iov_base =
				(receive ? (void *)work->recv_buf[slot] :
				 (void *)work->send_buf[slot]) + seg * IOV_BYTES;
			work->iov[slot][seg].iov_len = IOV_BYTES;
		}
		work->msg[slot].msg_iov = work->iov[slot];
		work->msg[slot].msg_iovlen = IOV_COUNT;
	}
}

static int peer_fill(struct workload *work, uint64_t base)
{
	unsigned int i;

	for (i = 0; i < QD; i++) {
		pattern_fill(work->send_buf[i], base + i);
		if (send(work->sockets[1], work->send_buf[i], PAYLOAD_BYTES,
			 MSG_DONTWAIT | MSG_NOSIGNAL) != (ssize_t)PAYLOAD_BYTES)
			return -1;
	}
	return 0;
}

static int peer_drain(struct workload *work, uint64_t base,
		      struct result *result)
{
	unsigned char buffer[PAYLOAD_BYTES];
	unsigned int i;

	for (i = 0; i < QD; i++) {
		ssize_t nr = recv(work->sockets[1], buffer, sizeof(buffer), MSG_DONTWAIT);

		if (nr != (ssize_t)sizeof(buffer))
			return -1;
		if (!pattern_matches(buffer, base + i))
			result->content_mismatches++;
	}
	errno = 0;
	if (recv(work->sockets[1], buffer, sizeof(buffer), MSG_DONTWAIT) >= 0 ||
	    errno != EAGAIN)
		return -1;
	return 0;
}

static int validate_receive_buffers(struct workload *work, uint64_t base,
				    struct result *result)
{
	unsigned int i;

	for (i = 0; i < QD; i++) {
		if (!pattern_matches(work->recv_buf[i], base + i))
			result->content_mismatches++;
	}
	return result->content_mismatches ? -1 : 0;
}

static void account_result(struct result *result, const struct io_uring_cqe *cqe,
			   uint64_t expected_ud, bool allow_mshot_flags)
{
	result->cqes++;
	if (cqe->user_data != expected_ud)
		result->bad_user_data++;
	if (cqe->res == -EAGAIN)
		result->eagain_results++;
	if (cqe->res == -ENOBUFS)
		result->enobufs_results++;
	if (cqe->res != (int)PAYLOAD_BYTES)
		result->bad_results++;
	else
		result->successful_results++;
	if (!allow_mshot_flags && cqe->flags != 0)
		result->bad_flags++;
}

static int run_ordinary(struct workload *work, enum profile_id profile,
			unsigned int operations, bool timed,
			struct result *result)
{
	struct raw_ring *ring = &work->ring;
	bool send_path = profile == PROFILE_S0 || profile == PROFILE_S1;
	bool msg_path = profile == PROFILE_S1 || profile == PROFILE_R1;
	unsigned int base;

	memset(result, 0, sizeof(*result));
	result->logical_ops = operations;
	result->logical_bytes = (uint64_t)operations * PAYLOAD_BYTES;
	if (!operations || operations % QD)
		return -1;
	prepare_iov(work, !send_path);
	for (base = 0; base < operations; base += QD) {
		unsigned int sq_head, sq_tail, cq_head, cq_tail, i;
		uint64_t started = 0;

		memset(work->recv_buf, 0, sizeof(work->recv_buf));
		for (i = 0; i < QD; i++)
			pattern_fill(work->send_buf[i], (uint64_t)base + i);
		if (!send_path && peer_fill(work, base) != 0)
			return -1;
		sq_head = load_acquire_u32(ring->sq_head);
		sq_tail = load_acquire_u32(ring->sq_tail);
		if (sq_tail - sq_head + QD > *ring->sq_entries)
			return -1;
		if (timed)
			started = now_ns();
		for (i = 0; i < QD; i++) {
			unsigned int index = sq_tail & *ring->sq_mask;
			struct io_uring_sqe *sqe = &ring->sqes[index];

			memset(sqe, 0, sizeof(*sqe));
			sqe->opcode = profile == PROFILE_S0 ? IORING_OP_SEND :
				      profile == PROFILE_S1 ? IORING_OP_SENDMSG :
				      profile == PROFILE_R0 ? IORING_OP_RECV :
				      IORING_OP_RECVMSG;
			sqe->fd = work->sockets[0];
			sqe->addr = msg_path ? (uintptr_t)&work->msg[i] :
				(send_path ? (uintptr_t)work->send_buf[i] :
				 (uintptr_t)work->recv_buf[i]);
			sqe->len = msg_path ? 1U : PAYLOAD_BYTES;
			sqe->msg_flags = MSG_DONTWAIT | (send_path ? MSG_NOSIGNAL : 0);
			sqe->user_data = (uint64_t)base + i;
			ring->sq_array[index] = index;
			sq_tail++;
		}
		store_release_u32(ring->sq_tail, sq_tail);
		result->sqes += QD;
		if (ring_enter(ring, QD, QD, result, timed) != 0)
			return -1;
		cq_head = load_acquire_u32(ring->cq_head);
		cq_tail = load_acquire_u32(ring->cq_tail);
		if (cq_tail - cq_head != QD)
			return -1;
		for (i = 0; i < QD; i++)
			work->cqe_copy[i] = ring->cqes[(cq_head + i) & *ring->cq_mask];
		store_release_u32(ring->cq_head, cq_head + QD);
		if (timed)
			result->elapsed_ns += now_ns() - started;
		for (i = 0; i < QD; i++)
			account_result(result, &work->cqe_copy[i],
				       (uint64_t)base + i, false);
		if (send_path) {
			if (peer_drain(work, base, result) != 0)
				return -1;
		} else if (validate_receive_buffers(work, base, result) != 0) {
			return -1;
		}
	}
	result->outstanding = outstanding(ring);
	result->sq_dropped = *ring->sq_dropped;
	result->cq_overflow = *ring->cq_overflow;
	result->semantic_pass = result->sqes == operations &&
		result->cqes == operations &&
		result->successful_results == operations &&
		result->bad_results == 0 && result->bad_flags == 0 &&
		result->bad_user_data == 0 && result->content_mismatches == 0 &&
		result->eagain_results == 0 && result->outstanding == 0 &&
		result->sq_dropped == 0 && result->cq_overflow == 0;
	return result->semantic_pass ? 0 : -1;
}

/* Keep SQ preparation, one enter, and CQ consumption, but remove networking. */
static int run_nop_control(struct workload *work, unsigned int operations,
			   bool timed, struct result *result)
{
	struct raw_ring *ring = &work->ring;
	unsigned int base;

	memset(result, 0, sizeof(*result));
	result->logical_ops = operations;
	if (!operations || operations % QD)
		return -1;
	for (base = 0; base < operations; base += QD) {
		unsigned int sq_head, sq_tail, cq_head, cq_tail, i;
		uint64_t started = 0;

		sq_head = load_acquire_u32(ring->sq_head);
		sq_tail = load_acquire_u32(ring->sq_tail);
		if (sq_tail - sq_head + QD > *ring->sq_entries)
			return -1;
		if (timed)
			started = now_ns();
		for (i = 0; i < QD; i++) {
			unsigned int index = sq_tail & *ring->sq_mask;
			struct io_uring_sqe *sqe = &ring->sqes[index];

			memset(sqe, 0, sizeof(*sqe));
			sqe->opcode = IORING_OP_NOP;
			sqe->fd = -1;
			sqe->user_data = (uint64_t)base + i;
			ring->sq_array[index] = index;
			sq_tail++;
		}
		store_release_u32(ring->sq_tail, sq_tail);
		result->sqes += QD;
		if (ring_enter(ring, QD, QD, result, timed) != 0)
			return -1;
		cq_head = load_acquire_u32(ring->cq_head);
		cq_tail = load_acquire_u32(ring->cq_tail);
		if (cq_tail - cq_head != QD)
			return -1;
		for (i = 0; i < QD; i++)
			work->cqe_copy[i] = ring->cqes[(cq_head + i) & *ring->cq_mask];
		store_release_u32(ring->cq_head, cq_head + QD);
		if (timed)
			result->elapsed_ns += now_ns() - started;
		for (i = 0; i < QD; i++) {
			const struct io_uring_cqe *cqe = &work->cqe_copy[i];

			result->cqes++;
			if (cqe->user_data != (uint64_t)base + i)
				result->bad_user_data++;
			if (cqe->res != 0)
				result->bad_results++;
			else
				result->successful_results++;
			if (cqe->flags != 0)
				result->bad_flags++;
		}
	}
	result->outstanding = outstanding(ring);
	result->sq_dropped = *ring->sq_dropped;
	result->cq_overflow = *ring->cq_overflow;
	result->semantic_pass = result->sqes == operations &&
		result->cqes == operations &&
		result->successful_results == operations &&
		result->bad_results == 0 && result->bad_flags == 0 &&
		result->bad_user_data == 0 && result->outstanding == 0 &&
		result->sq_dropped == 0 && result->cq_overflow == 0;
	return result->semantic_pass ? 0 : -1;
}

/* Match the 32-message batch shape while bypassing io_uring itself. */
static int run_sys_control(struct workload *work, unsigned int operations,
			   bool timed, struct result *result)
{
	struct mmsghdr messages[QD];
	struct iovec iov[QD];
	unsigned int base, i;

	memset(result, 0, sizeof(*result));
	memset(messages, 0, sizeof(messages));
	result->logical_ops = operations;
	result->logical_bytes = (uint64_t)operations * PAYLOAD_BYTES;
	if (!operations || operations % QD)
		return -1;
	for (i = 0; i < QD; i++) {
		iov[i].iov_base = work->send_buf[i];
		iov[i].iov_len = PAYLOAD_BYTES;
		messages[i].msg_hdr.msg_iov = &iov[i];
		messages[i].msg_hdr.msg_iovlen = 1;
	}
	for (base = 0; base < operations; base += QD) {
		uint64_t started = 0;
		int sent;

		for (i = 0; i < QD; i++) {
			pattern_fill(work->send_buf[i], (uint64_t)base + i);
			messages[i].msg_len = 0;
		}
		if (timed)
			started = now_ns();
		sent = sendmmsg(work->sockets[0], messages, QD,
				MSG_DONTWAIT | MSG_NOSIGNAL);
		result->total_enters++;
		if (timed) {
			result->timed_enters++;
			result->elapsed_ns += now_ns() - started;
		}
		if (sent != (int)QD) {
			if (sent < 0 && errno == EAGAIN)
				result->eagain_results++;
			result->bad_results++;
			return -1;
		}
		for (i = 0; i < QD; i++) {
			if (messages[i].msg_len != PAYLOAD_BYTES)
				result->bad_results++;
			else
				result->successful_results++;
		}
		if (peer_drain(work, base, result) != 0)
			return -1;
	}
	result->outstanding = outstanding(&work->ring);
	result->sq_dropped = *work->ring.sq_dropped;
	result->cq_overflow = *work->ring.cq_overflow;
	result->semantic_pass = result->successful_results == operations &&
		result->bad_results == 0 && result->content_mismatches == 0 &&
		result->eagain_results == 0 && result->outstanding == 0 &&
		result->sq_dropped == 0 && result->cq_overflow == 0;
	return result->semantic_pass ? 0 : -1;
}

static int register_pbuf(struct workload *work)
{
	struct io_uring_buf_reg reg;
	int ret;

	memset(&reg, 0, sizeof(reg));
	reg.ring_addr = (uintptr_t)work->pbuf_ring;
	reg.ring_entries = PBUF_ENTRIES;
	reg.bgid = PBUF_BGID;
	ret = (int)syscall(__NR_io_uring_register, work->ring.fd,
			   IORING_REGISTER_PBUF_RING, &reg, 1);
	if (ret == 0)
		work->pbuf_registered = true;
	return ret;
}

static int unregister_pbuf(struct workload *work)
{
	struct io_uring_buf_reg reg;
	int ret;

	memset(&reg, 0, sizeof(reg));
	reg.bgid = PBUF_BGID;
	ret = (int)syscall(__NR_io_uring_register, work->ring.fd,
			   IORING_UNREGISTER_PBUF_RING, &reg, 1);
	if (ret == 0)
		work->pbuf_registered = false;
	return ret;
}

static int pbuf_status(struct workload *work, uint16_t *head)
{
	struct io_uring_buf_status status;
	int ret;

	memset(&status, 0, sizeof(status));
	status.buf_group = PBUF_BGID;
	ret = (int)syscall(__NR_io_uring_register, work->ring.fd,
			   IORING_REGISTER_PBUF_STATUS, &status, 1);
	if (ret == 0)
		*head = (uint16_t)status.head;
	return ret;
}

static void publish_buffer(struct workload *work, uint16_t bid)
{
	unsigned int index = work->pbuf_tail & (PBUF_ENTRIES - 1);
	struct io_uring_buf *buf = &work->pbuf_ring->bufs[index];

	buf->addr = (uintptr_t)work->pbuf[bid];
	buf->len = PAYLOAD_BYTES;
	buf->bid = bid;
	work->pbuf_tail++;
}

static int setup_pbuf(struct workload *work)
{
	unsigned int i;
	uint16_t head;

	if (posix_memalign((void **)&work->pbuf_ring, 4096, 4096) != 0)
		return -1;
	memset(work->pbuf_ring, 0, 4096);
	for (i = 0; i < PBUF_ENTRIES; i++) {
		if (posix_memalign(&work->pbuf[i], 64, PAYLOAD_BYTES) != 0)
			return -1;
		memset(work->pbuf[i], 0, PAYLOAD_BYTES);
	}
	if (register_pbuf(work) != 0)
		return -1;
	work->pbuf_tail = 0;
	for (i = 0; i < PBUF_ENTRIES; i++)
		publish_buffer(work, (uint16_t)i);
	store_release_u16(&work->pbuf_ring->tail, work->pbuf_tail);
	return pbuf_status(work, &head) == 0 && head == 0 ? 0 : -1;
}

static int cancel_multishot(struct workload *work, uint64_t request_ud,
			    struct result *result)
{
	struct io_uring_sqe *sqe = reserve_sqe(&work->ring);
	unsigned int i;
	bool saw_cancel = false, saw_terminal = false;

	if (!sqe)
		return -1;
	sqe->opcode = IORING_OP_ASYNC_CANCEL;
	sqe->addr = request_ud;
	sqe->user_data = CANCEL_TAG | (request_ud & 0xffffffffULL);
	result->sqes++;
	if (ring_enter(&work->ring, 1, 2, result, false) != 0 ||
	    copy_cqes(work, 2) != 0)
		return -1;
	for (i = 0; i < 2; i++) {
		const struct io_uring_cqe *cqe = &work->cqe_copy[i];

		result->cqes++;
		result->cancel_cqes++;
		if (cqe->user_data == request_ud && cqe->res == -ECANCELED &&
		    !(cqe->flags & IORING_CQE_F_MORE))
			saw_terminal = true;
		else if (cqe->user_data == (CANCEL_TAG | (request_ud & 0xffffffffULL)) &&
			 cqe->res == 0 && cqe->flags == 0)
			saw_cancel = true;
		else
			result->bad_results++;
	}
	return saw_cancel && saw_terminal ? 0 : -1;
}

static int run_multishot(struct workload *work, unsigned int operations,
			 bool timed, struct result *result)
{
	uint16_t head_before, head_after;
	unsigned int base;

	memset(result, 0, sizeof(*result));
	result->logical_ops = operations;
	result->logical_bytes = (uint64_t)operations * PAYLOAD_BYTES;
	if (!operations || operations % QD ||
	    pbuf_status(work, &head_before) != 0)
		return -1;
	for (base = 0; base < operations; base += QD) {
		struct io_uring_sqe *sqe;
		uint64_t request_ud = MSHOT_TAG | (uint64_t)(base / QD);
		uint64_t seen_bids = 0;
		uint64_t started = 0;
		unsigned int i;

		if (peer_fill(work, base) != 0)
			return -1;
		sqe = reserve_sqe(&work->ring);
		if (!sqe)
			return -1;
		sqe->opcode = IORING_OP_RECV;
		sqe->fd = work->sockets[0];
		sqe->len = 0;
		sqe->ioprio = IORING_RECV_MULTISHOT;
		sqe->flags = IOSQE_BUFFER_SELECT;
		sqe->buf_group = PBUF_BGID;
		/*
		 * Do not set MSG_DONTWAIT explicitly here.  io_uring performs its
		 * nonblocking probe via issue_flags and then arms poll on EAGAIN.
		 * An explicit flag would turn queue exhaustion into a terminal
		 * -EAGAIN CQE instead of leaving the multishot request cancellable.
		 */
		sqe->msg_flags = 0;
		sqe->user_data = request_ud;
		result->sqes++;
		if (timed)
			started = now_ns();
		if (ring_enter(&work->ring, 1, QD, result, timed) != 0 ||
		    copy_cqes(work, QD) != 0)
			return -1;
		if (timed)
			result->elapsed_ns += now_ns() - started;
		for (i = 0; i < QD; i++) {
			const struct io_uring_cqe *cqe = &work->cqe_copy[i];
			uint16_t bid = (uint16_t)(cqe->flags >> IORING_CQE_BUFFER_SHIFT);

			account_result(result, cqe, request_ud, true);
			if (!(cqe->flags & IORING_CQE_F_MORE) ||
			    !(cqe->flags & IORING_CQE_F_BUFFER) ||
			    (cqe->flags & ~(IORING_CQE_F_MORE | IORING_CQE_F_BUFFER |
					    0xffff0000U)) || bid >= PBUF_ENTRIES) {
				result->bad_flags++;
				continue;
			}
			if (seen_bids & (1ULL << bid))
				result->duplicate_bids++;
			seen_bids |= 1ULL << bid;
			if (!pattern_matches(work->pbuf[bid], (uint64_t)base + i))
				result->content_mismatches++;
			publish_buffer(work, bid);
		}
		store_release_u16(&work->pbuf_ring->tail, work->pbuf_tail);
		if (cancel_multishot(work, request_ud, result) != 0)
			return -1;
	}
	if (pbuf_status(work, &head_after) != 0)
		return -1;
	result->pbuf_head_delta = (uint16_t)(head_after - head_before);
	result->outstanding = outstanding(&work->ring);
	result->sq_dropped = *work->ring.sq_dropped;
	result->cq_overflow = *work->ring.cq_overflow;
	result->semantic_pass = result->successful_results == operations &&
		result->pbuf_head_delta == operations &&
		result->bad_results == 0 && result->bad_flags == 0 &&
		result->bad_user_data == 0 && result->duplicate_bids == 0 &&
		result->content_mismatches == 0 && result->eagain_results == 0 &&
		result->enobufs_results == 0 && result->outstanding == 0 &&
		result->sq_dropped == 0 && result->cq_overflow == 0;
	return result->semantic_pass ? 0 : -1;
}

static const char *profile_name(enum profile_id profile)
{
	switch (profile) {
	case PROFILE_S0: return "S0_SEND_UBUF";
	case PROFILE_S1: return "S1_SENDMSG_IOV16";
	case PROFILE_R0: return "R0_RECV_UBUF";
	case PROFILE_R1: return "R1_RECVMSG_IOV16";
	case PROFILE_M0: return "M0_RECV_MULTISHOT_PBUF";
	case PROFILE_C0: return "C0_IOURING_NOP";
	case PROFILE_U0: return "U0_SENDMMSG_UBUF32";
	default: return "unset";
	}
}

static const char *phase_name(enum phase_id phase)
{
	switch (phase) {
	case PHASE_SMOKE: return "semantic-smoke";
	case PHASE_TRACE: return "direct-hit-trace";
	case PHASE_MEASURED: return "measured";
	default: return "unknown";
	}
}

static void preamble(FILE *out, const struct workload *work)
{
	fprintf(out, "# kernel_release=%s\n# boot_id=%s\n# compiler=%s\n",
		work->release, work->boot_id, __VERSION__);
	fprintf(out, "schema\tprofile\tphase\tround\tlogical_ops\tlogical_bytes"
		"\tsqes\tcqes\ttimed_enters\ttotal_enters\telapsed_ns\tns_per_op"
		"\tsuccessful_results\tbad_results\tbad_flags\tbad_user_data"
		"\tduplicate_bids\tcontent_mismatches\teagain_results"
		"\tenobufs_results\tcancel_cqes\tpbuf_head_delta\toutstanding"
		"\tsq_dropped\tcq_overflow\tsemantic_pass\tcpu\n");
}

static void emit(FILE *out, enum profile_id profile, enum phase_id phase,
		 unsigned int round, const struct result *r)
{
	double ns_per_op = r->elapsed_ns && r->logical_ops ?
		(double)r->elapsed_ns / (double)r->logical_ops : 0.0;

	fprintf(out, "%u\t%s\t%s\t%u\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%.6f\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%u\t%u\n",
		SCHEMA_VERSION, profile_name(profile), phase_name(phase), round,
		r->logical_ops, r->logical_bytes, r->sqes, r->cqes,
		r->timed_enters, r->total_enters, r->elapsed_ns, ns_per_op,
		r->successful_results, r->bad_results, r->bad_flags,
		r->bad_user_data, r->duplicate_bids, r->content_mismatches,
		r->eagain_results, r->enobufs_results, r->cancel_cqes,
		r->pbuf_head_delta, r->outstanding, r->sq_dropped,
		r->cq_overflow, r->semantic_pass ? 1U : 0U, CONTRACT_CPU);
	fflush(out);
}

static int execute_once(struct workload *work, enum profile_id profile,
			unsigned int operations, bool timed,
			struct result *result)
{
	if (profile == PROFILE_M0)
		return run_multishot(work, operations, timed, result);
	if (profile == PROFILE_C0)
		return run_nop_control(work, operations, timed, result);
	if (profile == PROFILE_U0)
		return run_sys_control(work, operations, timed, result);
	return run_ordinary(work, profile, operations, timed, result);
}

static int run_mode(struct workload *work, const struct options *opt, FILE *out)
{
	struct result result;
	unsigned int operations, round;
	enum phase_id phase;

	if (opt->mode == MODE_SMOKE || opt->mode == MODE_TRACE) {
		bool trace = opt->mode == MODE_TRACE;

		operations = opt->profile == PROFILE_M0 ?
			(trace ? TRACE_MSHOT_OPS : SMOKE_MSHOT_OPS) :
			(trace ? TRACE_ORDINARY_OPS : SMOKE_ORDINARY_OPS);
		phase = trace ? PHASE_TRACE : PHASE_SMOKE;
		if (execute_once(work, opt->profile, operations, false, &result) != 0) {
			emit(out, opt->profile, phase, 0, &result);
			return -1;
		}
		emit(out, opt->profile, phase, 0, &result);
		return 0;
	}
	operations = opt->profile == PROFILE_M0 ?
		MSHOT_OPS_PER_ROUND : ORDINARY_OPS_PER_ROUND;
	if (execute_once(work, opt->profile,
			 opt->profile == PROFILE_M0 ? SMOKE_MSHOT_OPS :
			 SMOKE_ORDINARY_OPS, false, &result) != 0)
		return -1;
	for (round = 0; round < WARMUP_ROUNDS; round++) {
		if (execute_once(work, opt->profile, operations, false, &result) != 0)
			return -1;
	}
	for (round = 0; round < MEASURED_ROUNDS; round++) {
		if (execute_once(work, opt->profile, operations, true, &result) != 0) {
			emit(out, opt->profile, PHASE_MEASURED, round, &result);
			return -1;
		}
		emit(out, opt->profile, PHASE_MEASURED, round, &result);
	}
	return 0;
}

static void describe(enum profile_id profile)
{
	printf("profile=%s\n", profile_name(profile));
	printf("old_release=6.12.95-bm-6.12.95\nnew_release=7.1.3-bm-7.1.3\n");
	printf("cpu=%u\nring_entries=%u\nqueue_depth=%u\n", CONTRACT_CPU,
	       RING_ENTRIES, QD);
	printf("payload_bytes=%u\niov_count=%u\npbuf_entries=%u\n",
	       PAYLOAD_BYTES, IOV_COUNT, PBUF_ENTRIES);
	printf("ordinary_ops_per_round=%u\nmultishot_ops_per_round=%u\n",
	       ORDINARY_OPS_PER_ROUND, MSHOT_OPS_PER_ROUND);
	printf("warmup_rounds=%u\nmeasured_rounds=%u\nexecution_started=false\n",
	       WARMUP_ROUNDS, MEASURED_ROUNDS);
}

static int parse_profile(const char *text, enum profile_id *profile)
{
	if (!strcmp(text, "s0")) *profile = PROFILE_S0;
	else if (!strcmp(text, "s1")) *profile = PROFILE_S1;
	else if (!strcmp(text, "r0")) *profile = PROFILE_R0;
	else if (!strcmp(text, "r1")) *profile = PROFILE_R1;
	else if (!strcmp(text, "m0")) *profile = PROFILE_M0;
	else if (!strcmp(text, "c0")) *profile = PROFILE_C0;
	else if (!strcmp(text, "u0")) *profile = PROFILE_U0;
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

static int parse_options(int argc, char **argv, struct options *opt)
{
	int i;

	memset(opt, 0, sizeof(*opt));
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--execute")) opt->execute = true;
		else if (!strcmp(argv[i], "--profile") && i + 1 < argc) {
			if (parse_profile(argv[++i], &opt->profile) != 0) return -1;
		} else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
			if (parse_mode(argv[++i], &opt->mode) != 0) return -1;
		} else if (!strcmp(argv[i], "--output") && i + 1 < argc)
			opt->output_path = argv[++i];
		else return -1;
	}
	if (opt->profile == PROFILE_UNSET || opt->mode == MODE_UNSET)
		return -1;
	if (opt->mode != MODE_DESCRIBE && !opt->execute)
		return -1;
	return 0;
}

static void cleanup(struct workload *work)
{
	unsigned int i;

	if (work->pbuf_registered)
		unregister_pbuf(work);
	for (i = 0; i < PBUF_ENTRIES; i++)
		free(work->pbuf[i]);
	free(work->pbuf_ring);
	raw_ring_destroy(&work->ring);
	if (work->sockets[0] >= 0)
		close(work->sockets[0]);
	if (work->sockets[1] >= 0)
		close(work->sockets[1]);
}

int main(int argc, char **argv)
{
	struct options opt;
	struct workload work;
	struct utsname uts;
	FILE *out = stdout;
	int ret = EXIT_FAILURE;

	if (parse_options(argc, argv, &opt) != 0) {
		fprintf(stderr, "usage: %s --profile s0|s1|r0|r1|m0|c0|u0 --mode describe|smoke|trace|point [--execute] [--output PATH]\n", argv[0]);
		return EXIT_FAILURE;
	}
	if (opt.mode == MODE_DESCRIBE) {
		describe(opt.profile);
		return EXIT_SUCCESS;
	}
	memset(&work, 0, sizeof(work));
	work.ring.fd = -1;
	work.sockets[0] = -1;
	work.sockets[1] = -1;
	if (uname(&uts) != 0)
		return EXIT_FAILURE;
	strncpy(work.release, uts.release, sizeof(work.release) - 1);
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
	preamble(out, &work);
	if (pin_cpu() != 0 || raw_ring_init(&work.ring) != 0 ||
	    setup_sockets(&work) != 0)
		goto out;
	if (opt.profile == PROFILE_M0 && setup_pbuf(&work) != 0)
		goto out;
	ret = run_mode(&work, &opt, out) == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
out:
	if (out != stdout)
		fclose(out);
	cleanup(&work);
	return ret;
}
