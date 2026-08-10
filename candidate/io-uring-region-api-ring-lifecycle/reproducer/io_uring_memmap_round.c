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
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef __NR_io_uring_setup
#error "io_uring_setup syscall number is required"
#endif
#ifndef __NR_io_uring_enter
#error "io_uring_enter syscall number is required"
#endif
#ifndef IORING_SETUP_NO_MMAP
#define IORING_SETUP_NO_MMAP (1U << 14)
#endif

#define SCHEMA_VERSION 1U
#define RING_ENTRIES 64U
#define CQ_ENTRIES 128U
#define POINT_WARMUPS 3U
#define POINT_ROUNDS 15U
#define R0_EVENTS 4096U
#define LIFECYCLE_EVENTS 512U
#define SMOKE_EVENTS 8U
#define TRACE_EVENTS 16U
#define ASYNC_TEARDOWN_PAUSE_US 200U
#define INVALID_MMAP_OFFSET UINT64_C(0x70000000)
#define INPUT_SHA256 "809954870327de3d5c0d6093dd8089e1ce74b45528e5b6f40e904d28988d4665"
#define TARGET_SHA256 "b688f6bc0d9403aac283c855108a3083739781a62ba8d7be2fc3b6da48a057a9"

enum profile_id {
	PROFILE_R0_REMAP,
	PROFILE_L0_STANDARD_LIFECYCLE,
	PROFILE_N0_USERBACKED_LIFECYCLE,
	PROFILE_A0_ANON_MAP,
	PROFILE_E0_ERRORS,
};

enum run_mode {
	MODE_DESCRIBE,
	MODE_SMOKE,
	MODE_TRACE,
	MODE_POINT,
};

struct options {
	enum profile_id profile;
	enum run_mode mode;
	const char *output_path;
	unsigned int cpu;
	bool execute;
};

struct raw_ring {
	int fd;
	struct io_uring_params p;
	bool single_mmap;
	bool user_backed;
	bool mapped;
	void *sq_ptr;
	void *cq_ptr;
	void *sqes_ptr;
	size_t sq_sz;
	size_t cq_sz;
	size_t ring_sz;
	size_t sqes_sz;
	void *user_ring;
	void *user_sqes;
	size_t user_ring_sz;
	size_t user_sqes_sz;
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

struct geometry {
	unsigned int sq_entries;
	unsigned int cq_entries;
	unsigned int features;
	bool single_mmap;
	size_t sq_sz;
	size_t cq_sz;
	size_t ring_sz;
	size_t sqes_sz;
};

struct counters {
	uint64_t events;
	uint64_t successful_events;
	uint64_t setup_count;
	uint64_t close_count;
	uint64_t mmap_count;
	uint64_t munmap_count;
	uint64_t nop_count;
	uint64_t validation_failures;
	uint64_t cleanup_failures;
	uint64_t sq_dropped;
	uint64_t cq_overflow;
	uint64_t outstanding;
	uint64_t timed_ns;
	bool user_ring_addr_ok;
	bool user_sqes_addr_ok;
};

struct identity {
	char kernel_release[128];
	char boot_id[128];
};

static volatile sig_atomic_t stop_requested;

static const char result_header[] =
	"schema_version\tkernel_release\tboot_id\trun_mode\tprofile\tphase"
	"\tround\tevents\tsuccessful_events\tsetup_count\tclose_count"
	"\tmmap_count\tmunmap_count\tnop_count\tvalidation_failures"
	"\tcleanup_failures\tsq_dropped\tcq_overflow\toutstanding\ttimed_ns"
	"\tns_per_event\tcpu\tsemantic_pass\tsq_entries\tcq_entries"
	"\tfeatures\tsingle_mmap\tring_bytes\tsqes_bytes"
	"\tuser_ring_addr_ok\tuser_sqes_addr_ok\ttrace_enabled"
	"\tinput_sha256\ttarget_sha256";

static void handle_signal(int signo)
{
	(void)signo;
	stop_requested = 1;
}

static unsigned int load_acquire(const unsigned int *ptr)
{
	return __atomic_load_n(ptr, __ATOMIC_ACQUIRE);
}

static unsigned int load_relaxed(const unsigned int *ptr)
{
	return __atomic_load_n(ptr, __ATOMIC_RELAXED);
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

static size_t page_align(size_t value)
{
	size_t page = (size_t)sysconf(_SC_PAGESIZE);

	return (value + page - 1U) & ~(page - 1U);
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

static int pin_cpu(unsigned int cpu)
{
	cpu_set_t set;
	long online = sysconf(_SC_NPROCESSORS_ONLN);

	if (online <= (long)cpu)
		return -1;
	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	if (sched_setaffinity(0, sizeof(set), &set) != 0)
		return -1;
	return sched_getcpu() == (int)cpu ? 0 : -1;
}

static const char *profile_name(enum profile_id profile)
{
	switch (profile) {
	case PROFILE_R0_REMAP:
		return "r0_remap";
	case PROFILE_L0_STANDARD_LIFECYCLE:
		return "l0_standard_lifecycle";
	case PROFILE_N0_USERBACKED_LIFECYCLE:
		return "n0_userbacked_lifecycle";
	case PROFILE_A0_ANON_MAP:
		return "a0_anon_map";
	case PROFILE_E0_ERRORS:
		return "e0_errors";
	}
	return "unknown";
}

static const char *mode_name(enum run_mode mode)
{
	switch (mode) {
	case MODE_DESCRIBE:
		return "describe";
	case MODE_SMOKE:
		return "smoke";
	case MODE_TRACE:
		return "trace";
	case MODE_POINT:
		return "point";
	}
	return "unknown";
}

static int parse_profile(const char *text, enum profile_id *profile)
{
	if (!strcmp(text, "r0_remap"))
		*profile = PROFILE_R0_REMAP;
	else if (!strcmp(text, "l0_standard_lifecycle"))
		*profile = PROFILE_L0_STANDARD_LIFECYCLE;
	else if (!strcmp(text, "n0_userbacked_lifecycle"))
		*profile = PROFILE_N0_USERBACKED_LIFECYCLE;
	else if (!strcmp(text, "a0_anon_map"))
		*profile = PROFILE_A0_ANON_MAP;
	else if (!strcmp(text, "e0_errors"))
		*profile = PROFILE_E0_ERRORS;
	else
		return -1;
	return 0;
}

static int parse_mode(const char *text, enum run_mode *mode)
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

static void usage(const char *program)
{
	fprintf(stderr,
		"Usage: %s --profile r0_remap|l0_standard_lifecycle|"
		"n0_userbacked_lifecycle|a0_anon_map|e0_errors "
		"--mode describe|smoke|trace|point [--cpu N] "
		"[--output FILE] [--execute]\n",
		program);
}

static int parse_options(int argc, char **argv, struct options *options)
{
	int i;
	bool have_profile = false, have_mode = false;

	memset(options, 0, sizeof(*options));
	options->cpu = 2U;
	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--profile") && i + 1 < argc) {
			if (parse_profile(argv[++i], &options->profile) != 0)
				return -1;
			have_profile = true;
		} else if (!strcmp(argv[i], "--mode") && i + 1 < argc) {
			if (parse_mode(argv[++i], &options->mode) != 0)
				return -1;
			have_mode = true;
		} else if (!strcmp(argv[i], "--cpu") && i + 1 < argc) {
			char *end = NULL;
			unsigned long value = strtoul(argv[++i], &end, 10);

			if (!end || *end || value > UINT32_MAX)
				return -1;
			options->cpu = (unsigned int)value;
		} else if (!strcmp(argv[i], "--output") && i + 1 < argc) {
			options->output_path = argv[++i];
		} else if (!strcmp(argv[i], "--execute")) {
			options->execute = true;
		} else {
			return -1;
		}
	}
	if (!have_profile || !have_mode)
		return -1;
	if (options->mode != MODE_DESCRIBE && !options->output_path)
		return -1;
	if (options->profile == PROFILE_E0_ERRORS &&
	    options->mode != MODE_DESCRIBE && options->mode != MODE_SMOKE)
		return -1;
	return 0;
}

static void raw_ring_reset(struct raw_ring *ring)
{
	memset(ring, 0, sizeof(*ring));
	ring->fd = -1;
}

static void geometry_from_params(const struct io_uring_params *p,
				 struct geometry *geometry)
{
	memset(geometry, 0, sizeof(*geometry));
	geometry->sq_entries = p->sq_entries;
	geometry->cq_entries = p->cq_entries;
	geometry->features = p->features;
	geometry->single_mmap = !!(p->features & IORING_FEAT_SINGLE_MMAP);
	geometry->sq_sz = p->sq_off.array +
		p->sq_entries * sizeof(unsigned int);
	geometry->cq_sz = p->cq_off.cqes +
		p->cq_entries * sizeof(struct io_uring_cqe);
	geometry->ring_sz = geometry->single_mmap ?
		(geometry->sq_sz > geometry->cq_sz ?
		 geometry->sq_sz : geometry->cq_sz) : geometry->sq_sz;
	geometry->sqes_sz = p->sq_entries * sizeof(struct io_uring_sqe);
	geometry->sq_sz = page_align(geometry->sq_sz);
	geometry->cq_sz = page_align(geometry->cq_sz);
	geometry->ring_sz = page_align(geometry->ring_sz);
	geometry->sqes_sz = page_align(geometry->sqes_sz);
}

static bool geometry_valid(const struct io_uring_params *p,
			   const struct geometry *geometry)
{
	if (p->sq_entries != RING_ENTRIES || p->cq_entries != CQ_ENTRIES)
		return false;
	if (!geometry->single_mmap)
		return false;
	if (!p->sq_entries || !p->cq_entries ||
	    (p->sq_entries & (p->sq_entries - 1U)) ||
	    (p->cq_entries & (p->cq_entries - 1U)))
		return false;
	if ((size_t)p->sq_off.array +
	    (size_t)p->sq_entries * sizeof(unsigned int) > geometry->ring_sz)
		return false;
	if ((size_t)p->cq_off.cqes +
	    (size_t)p->cq_entries * sizeof(struct io_uring_cqe) >
	    geometry->ring_sz)
		return false;
	return true;
}

static void bind_ring(struct raw_ring *ring)
{
	void *sq = ring->sq_ptr;
	void *cq = ring->cq_ptr;
	struct io_uring_params *p = &ring->p;

	ring->sq_head = (unsigned int *)((char *)sq + p->sq_off.head);
	ring->sq_tail = (unsigned int *)((char *)sq + p->sq_off.tail);
	ring->sq_mask = (unsigned int *)((char *)sq + p->sq_off.ring_mask);
	ring->sq_entries = (unsigned int *)((char *)sq + p->sq_off.ring_entries);
	ring->sq_dropped = (unsigned int *)((char *)sq + p->sq_off.dropped);
	ring->sq_array = (unsigned int *)((char *)sq + p->sq_off.array);
	ring->sqes = ring->sqes_ptr;
	ring->cq_head = (unsigned int *)((char *)cq + p->cq_off.head);
	ring->cq_tail = (unsigned int *)((char *)cq + p->cq_off.tail);
	ring->cq_mask = (unsigned int *)((char *)cq + p->cq_off.ring_mask);
	ring->cq_entries = (unsigned int *)((char *)cq + p->cq_off.ring_entries);
	ring->cq_overflow = (unsigned int *)((char *)cq + p->cq_off.overflow);
	ring->cqes = (struct io_uring_cqe *)((char *)cq + p->cq_off.cqes);
}

static int map_standard_ring(struct raw_ring *ring, struct counters *counters)
{
	struct geometry geometry;
	void *shared, *sqes;

	if (ring->mapped || ring->user_backed)
		return -1;
	geometry_from_params(&ring->p, &geometry);
	shared = mmap(NULL, geometry.ring_sz, PROT_READ | PROT_WRITE,
		      MAP_SHARED | MAP_POPULATE, ring->fd, IORING_OFF_SQ_RING);
	if (shared == MAP_FAILED)
		return -1;
	if (counters)
		counters->mmap_count++;
	sqes = mmap(NULL, geometry.sqes_sz, PROT_READ | PROT_WRITE,
		    MAP_SHARED | MAP_POPULATE, ring->fd, IORING_OFF_SQES);
	if (sqes == MAP_FAILED) {
		if (munmap(shared, geometry.ring_sz) != 0 && counters)
			counters->cleanup_failures++;
		else if (counters)
			counters->munmap_count++;
		return -1;
	}
	if (counters)
		counters->mmap_count++;
	ring->single_mmap = true;
	ring->sq_ptr = shared;
	ring->cq_ptr = shared;
	ring->sqes_ptr = sqes;
	ring->sq_sz = ring->cq_sz = ring->ring_sz = geometry.ring_sz;
	ring->sqes_sz = geometry.sqes_sz;
	ring->mapped = true;
	bind_ring(ring);
	return 0;
}

static int unmap_standard_ring(struct raw_ring *ring, struct counters *counters)
{
	int failed = 0;

	if (!ring->mapped || ring->user_backed)
		return -1;
	if (munmap(ring->sqes_ptr, ring->sqes_sz) != 0)
		failed = 1;
	else if (counters)
		counters->munmap_count++;
	if (munmap(ring->sq_ptr, ring->ring_sz) != 0)
		failed = 1;
	else if (counters)
		counters->munmap_count++;
	ring->sq_ptr = ring->cq_ptr = ring->sqes_ptr = NULL;
	ring->mapped = false;
	return failed ? -1 : 0;
}

static int setup_standard_ring(struct raw_ring *ring, bool map,
			       struct counters *counters)
{
	struct geometry geometry;

	raw_ring_reset(ring);
	ring->p.flags = IORING_SETUP_CQSIZE;
	ring->p.cq_entries = CQ_ENTRIES;
	ring->fd = (int)syscall(__NR_io_uring_setup, RING_ENTRIES, &ring->p);
	if (ring->fd < 0)
		return -1;
	if (counters)
		counters->setup_count++;
	geometry_from_params(&ring->p, &geometry);
	if (!geometry_valid(&ring->p, &geometry))
		return -1;
	ring->single_mmap = geometry.single_mmap;
	ring->sq_sz = geometry.sq_sz;
	ring->cq_sz = geometry.cq_sz;
	ring->ring_sz = geometry.ring_sz;
	ring->sqes_sz = geometry.sqes_sz;
	if (map && map_standard_ring(ring, counters) != 0)
		return -1;
	return 0;
}

static int setup_user_ring(struct raw_ring *ring, void *user_ring,
			   size_t user_ring_sz, void *user_sqes,
			   size_t user_sqes_sz, struct counters *counters)
{
	struct geometry geometry;
	uintptr_t ring_addr = (uintptr_t)user_ring;
	uintptr_t sqes_addr = (uintptr_t)user_sqes;

	raw_ring_reset(ring);
	ring->p.flags = IORING_SETUP_CQSIZE | IORING_SETUP_NO_MMAP;
	ring->p.cq_entries = CQ_ENTRIES;
	ring->p.cq_off.user_addr = ring_addr;
	ring->p.sq_off.user_addr = sqes_addr;
	ring->fd = (int)syscall(__NR_io_uring_setup, RING_ENTRIES, &ring->p);
	if (ring->fd < 0)
		return -1;
	if (counters)
		counters->setup_count++;
	geometry_from_params(&ring->p, &geometry);
	if (!geometry_valid(&ring->p, &geometry) ||
	    geometry.ring_sz > user_ring_sz || geometry.sqes_sz > user_sqes_sz)
		return -1;
	ring->user_backed = true;
	ring->mapped = true;
	ring->single_mmap = true;
	ring->user_ring = user_ring;
	ring->user_sqes = user_sqes;
	ring->user_ring_sz = user_ring_sz;
	ring->user_sqes_sz = user_sqes_sz;
	ring->sq_ptr = user_ring;
	ring->cq_ptr = user_ring;
	ring->sqes_ptr = user_sqes;
	ring->sq_sz = ring->cq_sz = ring->ring_sz = geometry.ring_sz;
	ring->sqes_sz = geometry.sqes_sz;
	if (counters) {
		counters->user_ring_addr_ok =
			ring->p.cq_off.user_addr == ring_addr;
		counters->user_sqes_addr_ok =
			ring->p.sq_off.user_addr == sqes_addr;
	}
	bind_ring(ring);
	return 0;
}

static int close_ring(struct raw_ring *ring, struct counters *counters)
{
	int ret = 0;

	if (ring->mapped && !ring->user_backed &&
	    unmap_standard_ring(ring, counters) != 0)
		ret = -1;
	if (ring->fd >= 0) {
		if (close(ring->fd) != 0)
			ret = -1;
		else if (counters)
			counters->close_count++;
	}
	ring->fd = -1;
	ring->mapped = false;
	return ret;
}

static uint64_t ring_outstanding(const struct raw_ring *ring)
{
	if (!ring->mapped)
		return 0;
	return (uint64_t)(load_acquire(ring->sq_tail) -
			  load_acquire(ring->sq_head)) +
	       (uint64_t)(load_acquire(ring->cq_tail) -
			  load_relaxed(ring->cq_head));
}

static int submit_nop(struct raw_ring *ring, uint64_t user_data,
		      struct counters *counters)
{
	unsigned int head, tail, index, cq_head;
	struct io_uring_sqe *sqe;
	struct io_uring_cqe cqe;
	int ret;

	if (!ring->mapped)
		return -1;
	head = load_acquire(ring->sq_head);
	tail = load_relaxed(ring->sq_tail);
	if (tail - head >= *ring->sq_entries)
		return -1;
	index = tail & *ring->sq_mask;
	sqe = &ring->sqes[index];
	memset(sqe, 0, sizeof(*sqe));
	sqe->opcode = IORING_OP_NOP;
	sqe->user_data = user_data;
	ring->sq_array[index] = index;
	store_release(ring->sq_tail, tail + 1U);
	do {
		ret = (int)syscall(__NR_io_uring_enter, ring->fd, 1U, 1U,
				   IORING_ENTER_GETEVENTS, NULL, 0U);
	} while (ret < 0 && errno == EINTR && !stop_requested);
	if (ret != 1)
		return -1;
	if (load_acquire(ring->cq_tail) == load_relaxed(ring->cq_head))
		return -1;
	cq_head = load_relaxed(ring->cq_head);
	cqe = ring->cqes[cq_head & *ring->cq_mask];
	store_release(ring->cq_head, cq_head + 1U);
	if (counters)
		counters->nop_count++;
	if (cqe.user_data != user_data || cqe.res != 0 || cqe.flags != 0)
		return -1;
	return 0;
}

static int discover_geometry(struct geometry *geometry)
{
	struct raw_ring ring;
	int ret;

	ret = setup_standard_ring(&ring, false, NULL);
	if (ret != 0) {
		if (ring.fd >= 0)
			close(ring.fd);
		return -1;
	}
	geometry_from_params(&ring.p, geometry);
	close(ring.fd);
	return 0;
}

static void copy_ring_health(const struct raw_ring *ring,
			     struct counters *counters)
{
	if (!ring->mapped)
		return;
	counters->sq_dropped += load_relaxed(ring->sq_dropped);
	counters->cq_overflow += load_relaxed(ring->cq_overflow);
	counters->outstanding += ring_outstanding(ring);
}

static int run_r0(unsigned int events, struct counters *counters)
{
	struct raw_ring ring;
	uint64_t start, end;
	unsigned int i;
	int ret = -1;

	if (setup_standard_ring(&ring, true, counters) != 0)
		goto out;
	if (submit_nop(&ring, UINT64_C(0x1000000000000001), counters) != 0)
		goto out;
	copy_ring_health(&ring, counters);
	if (counters->outstanding || counters->sq_dropped || counters->cq_overflow)
		goto out;
	if (unmap_standard_ring(&ring, counters) != 0)
		goto out;
	start = now_ns();
	for (i = 0; i < events && !stop_requested; i++) {
		if (map_standard_ring(&ring, counters) != 0)
			goto out;
		if (unmap_standard_ring(&ring, counters) != 0)
			goto out;
		counters->successful_events++;
	}
	end = now_ns();
	counters->timed_ns += end - start;
	if (i != events)
		goto out;
	if (map_standard_ring(&ring, counters) != 0)
		goto out;
	if (submit_nop(&ring, UINT64_C(0x1000000000000002), counters) != 0)
		goto out;
	copy_ring_health(&ring, counters);
	if (counters->outstanding || counters->sq_dropped || counters->cq_overflow)
		goto out;
	ret = 0;
out:
	counters->events = events;
	if (ring.fd >= 0 && close_ring(&ring, counters) != 0)
		counters->cleanup_failures++;
	if (ret != 0)
		counters->validation_failures++;
	return ret;
}

static int run_l0(unsigned int events, struct counters *counters)
{
	struct raw_ring ring;
	unsigned int i;

	raw_ring_reset(&ring);
	for (i = 0; i < events && !stop_requested; i++) {
		uint64_t start = now_ns();

		raw_ring_reset(&ring);
		if (setup_standard_ring(&ring, true, counters) != 0)
			goto fail_close;
		if (submit_nop(&ring, UINT64_C(0x2000000000000000) + i,
			       counters) != 0)
			goto fail_close;
		copy_ring_health(&ring, counters);
		if (ring_outstanding(&ring) || load_relaxed(ring.sq_dropped) ||
		    load_relaxed(ring.cq_overflow))
			goto fail_close;
		if (close_ring(&ring, counters) != 0)
			goto fail;
		counters->timed_ns += now_ns() - start;
		counters->successful_events++;
		usleep(ASYNC_TEARDOWN_PAUSE_US);
	}
	counters->events = events;
	if (i == events)
		return 0;
fail:
	counters->events = events;
	counters->validation_failures++;
	return -1;
fail_close:
	if (close_ring(&ring, counters) != 0)
		counters->cleanup_failures++;
	goto fail;
}

static int allocate_user_regions(const struct geometry *geometry,
				 void **user_ring, void **user_sqes)
{
	*user_ring = mmap(NULL, geometry->ring_sz, PROT_READ | PROT_WRITE,
			  MAP_SHARED | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
	if (*user_ring == MAP_FAILED)
		return -1;
	*user_sqes = mmap(NULL, geometry->sqes_sz, PROT_READ | PROT_WRITE,
			  MAP_SHARED | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
	if (*user_sqes == MAP_FAILED) {
		munmap(*user_ring, geometry->ring_sz);
		*user_ring = NULL;
		return -1;
	}
	memset(*user_ring, 0, geometry->ring_sz);
	memset(*user_sqes, 0, geometry->sqes_sz);
	return 0;
}

static int run_n0(unsigned int events, const struct geometry *geometry,
		  struct counters *counters)
{
	struct raw_ring ring;
	void *user_ring = NULL, *user_sqes = NULL;
	size_t user_ring_bytes = geometry->ring_sz * events;
	size_t user_sqes_bytes = geometry->sqes_sz * events;
	unsigned int i;
	int ret = -1;

	raw_ring_reset(&ring);
	user_ring = mmap(NULL, user_ring_bytes, PROT_READ | PROT_WRITE,
			 MAP_SHARED | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
	if (user_ring == MAP_FAILED) {
		user_ring = NULL;
		goto out;
	}
	user_sqes = mmap(NULL, user_sqes_bytes, PROT_READ | PROT_WRITE,
			  MAP_SHARED | MAP_ANONYMOUS | MAP_POPULATE, -1, 0);
	if (user_sqes == MAP_FAILED) {
		user_sqes = NULL;
		goto out;
	}
	memset(user_ring, 0, user_ring_bytes);
	memset(user_sqes, 0, user_sqes_bytes);
	for (i = 0; i < events && !stop_requested; i++) {
		void *event_ring = (char *)user_ring + i * geometry->ring_sz;
		void *event_sqes = (char *)user_sqes + i * geometry->sqes_sz;
		uint64_t start;

		raw_ring_reset(&ring);
		start = now_ns();
		if (setup_user_ring(&ring, event_ring, geometry->ring_sz,
				    event_sqes, geometry->sqes_sz, counters) != 0)
			goto fail_close;
		if (!counters->user_ring_addr_ok || !counters->user_sqes_addr_ok)
			goto fail_close;
		if (submit_nop(&ring, UINT64_C(0x3000000000000000) + i,
			       counters) != 0)
			goto fail_close;
		copy_ring_health(&ring, counters);
		if (ring_outstanding(&ring) || load_relaxed(ring.sq_dropped) ||
		    load_relaxed(ring.cq_overflow))
			goto fail_close;
		if (close_ring(&ring, counters) != 0)
			goto fail;
		counters->timed_ns += now_ns() - start;
		counters->successful_events++;
		usleep(ASYNC_TEARDOWN_PAUSE_US);
	}
	if (i == events)
		ret = 0;
	goto out;
fail_close:
	if (ring.fd >= 0 && close_ring(&ring, counters) != 0)
		counters->cleanup_failures++;
fail:
	counters->validation_failures++;
out:
	counters->events = events;
	if (user_ring && munmap(user_ring, user_ring_bytes) != 0)
		counters->cleanup_failures++;
	if (user_sqes && munmap(user_sqes, user_sqes_bytes) != 0)
		counters->cleanup_failures++;
	return ret;
}

static int run_a0(unsigned int events, const struct geometry *geometry,
		  struct counters *counters)
{
	unsigned int i;
	uint64_t start = now_ns();

	for (i = 0; i < events && !stop_requested; i++) {
		void *ring = mmap(NULL, geometry->ring_sz,
				  PROT_READ | PROT_WRITE,
				  MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		void *sqes;

		if (ring == MAP_FAILED)
			goto fail;
		counters->mmap_count++;
		sqes = mmap(NULL, geometry->sqes_sz, PROT_READ | PROT_WRITE,
			    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		if (sqes == MAP_FAILED) {
			munmap(ring, geometry->ring_sz);
			goto fail;
		}
		counters->mmap_count++;
		if (munmap(sqes, geometry->sqes_sz) != 0 ||
		    munmap(ring, geometry->ring_sz) != 0)
			goto fail;
		counters->munmap_count += 2U;
		counters->successful_events++;
	}
	counters->timed_ns = now_ns() - start;
	counters->events = events;
	return i == events ? 0 : -1;
fail:
	counters->timed_ns = now_ns() - start;
	counters->events = events;
	counters->validation_failures++;
	return -1;
}

static int run_e0(const struct geometry *geometry, struct counters *counters)
{
	struct raw_ring standard, user;
	void *bad, *user_ring = NULL, *user_sqes = NULL;
	int ret = -1;

	raw_ring_reset(&standard);
	raw_ring_reset(&user);
	if (setup_standard_ring(&standard, false, counters) != 0)
		goto out;
	errno = 0;
	bad = mmap(NULL, geometry->ring_sz, PROT_READ | PROT_WRITE, MAP_SHARED,
		   standard.fd, (off_t)INVALID_MMAP_OFFSET);
	if (bad != MAP_FAILED) {
		munmap(bad, geometry->ring_sz);
		goto out_standard;
	}
	if (errno != EINVAL && errno != ENXIO && errno != ENOMEM)
		goto out_standard;
	if (allocate_user_regions(geometry, &user_ring, &user_sqes) != 0)
		goto out_standard;
	if (setup_user_ring(&user, user_ring, geometry->ring_sz,
			    user_sqes, geometry->sqes_sz, counters) != 0)
		goto out_user;
	errno = 0;
	bad = mmap(NULL, geometry->ring_sz, PROT_READ | PROT_WRITE,
		   MAP_SHARED, user.fd, IORING_OFF_SQ_RING);
	if (bad != MAP_FAILED) {
		munmap(bad, geometry->ring_sz);
		goto out_user;
	}
	/* get_unmapped_area failures may surface to mmap(2) as ENOMEM. */
	if (errno != EINVAL && errno != ENXIO && errno != ENOMEM)
		goto out_user;
	if (submit_nop(&user, UINT64_C(0x4000000000000001), counters) != 0)
		goto out_user;
	copy_ring_health(&user, counters);
	if (counters->outstanding || counters->sq_dropped || counters->cq_overflow)
		goto out_user;
	counters->successful_events = 2U;
	ret = 0;
out_user:
	if (close_ring(&user, counters) != 0)
		counters->cleanup_failures++;
out_standard:
	if (close_ring(&standard, counters) != 0)
		counters->cleanup_failures++;
out:
	counters->events = 2U;
	if (user_ring && munmap(user_ring, geometry->ring_sz) != 0)
		counters->cleanup_failures++;
	if (user_sqes && munmap(user_sqes, geometry->sqes_sz) != 0)
		counters->cleanup_failures++;
	if (ret != 0)
		counters->validation_failures++;
	return ret;
}

static int run_profile(enum profile_id profile, unsigned int events,
		       const struct geometry *geometry,
		       struct counters *counters)
{
	memset(counters, 0, sizeof(*counters));
	counters->user_ring_addr_ok = profile != PROFILE_N0_USERBACKED_LIFECYCLE;
	counters->user_sqes_addr_ok = profile != PROFILE_N0_USERBACKED_LIFECYCLE;
	switch (profile) {
	case PROFILE_R0_REMAP:
		return run_r0(events, counters);
	case PROFILE_L0_STANDARD_LIFECYCLE:
		return run_l0(events, counters);
	case PROFILE_N0_USERBACKED_LIFECYCLE:
		return run_n0(events, geometry, counters);
	case PROFILE_A0_ANON_MAP:
		return run_a0(events, geometry, counters);
	case PROFILE_E0_ERRORS:
		return run_e0(geometry, counters);
	}
	return -1;
}

static bool counters_pass(const struct counters *counters,
			  enum profile_id profile)
{
	if (counters->successful_events != counters->events ||
	    counters->validation_failures || counters->cleanup_failures ||
	    counters->sq_dropped || counters->cq_overflow ||
	    counters->outstanding)
		return false;
	if (profile == PROFILE_R0_REMAP &&
	    (counters->mmap_count != counters->munmap_count ||
	     counters->setup_count != 1U || counters->close_count != 1U ||
	     counters->nop_count != 2U))
		return false;
	if (profile == PROFILE_L0_STANDARD_LIFECYCLE &&
	    (counters->setup_count != counters->events ||
	     counters->close_count != counters->events ||
	     counters->nop_count != counters->events ||
	     counters->mmap_count != counters->munmap_count))
		return false;
	if (profile == PROFILE_N0_USERBACKED_LIFECYCLE &&
	    (counters->setup_count != counters->events ||
	     counters->close_count != counters->events ||
	     counters->nop_count != counters->events ||
	     counters->mmap_count || counters->munmap_count ||
	     !counters->user_ring_addr_ok || !counters->user_sqes_addr_ok))
		return false;
	if (profile == PROFILE_A0_ANON_MAP &&
	    (counters->mmap_count != counters->events * 2U ||
	     counters->munmap_count != counters->events * 2U))
		return false;
	return true;
}

static int write_header(FILE *output)
{
	return fprintf(output, "%s\n", result_header) < 0 ? -1 : 0;
}

static int write_result(FILE *output, const struct options *options,
			const struct identity *identity,
			const struct geometry *geometry,
			const char *phase, unsigned int round,
			const struct counters *counters, bool trace_enabled)
{
	double ns_per_event = counters->events ?
		(double)counters->timed_ns / (double)counters->events : 0.0;
	bool pass = counters_pass(counters, options->profile);

	return fprintf(output,
		"%u\t%s\t%s\t%s\t%s\t%s\t%u\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		"\t%.6f\t%u\t%s\t%u\t%u\t0x%x\t%s\t%zu\t%zu"
		"\t%s\t%s\t%s\t%s\t%s\n",
		SCHEMA_VERSION, identity->kernel_release, identity->boot_id,
		mode_name(options->mode), profile_name(options->profile), phase,
		round, counters->events, counters->successful_events,
		counters->setup_count, counters->close_count,
		counters->mmap_count, counters->munmap_count, counters->nop_count,
		counters->validation_failures, counters->cleanup_failures,
		counters->sq_dropped, counters->cq_overflow,
		counters->outstanding, counters->timed_ns, ns_per_event,
		options->cpu, pass ? "true" : "false", geometry->sq_entries,
		geometry->cq_entries, geometry->features,
		geometry->single_mmap ? "true" : "false", geometry->ring_sz,
		geometry->sqes_sz,
		counters->user_ring_addr_ok ? "true" : "false",
		counters->user_sqes_addr_ok ? "true" : "false",
		trace_enabled ? "true" : "false", INPUT_SHA256, TARGET_SHA256) < 0 ?
		-1 : 0;
}

static void describe(const struct options *options)
{
	printf("profile=%s\n", profile_name(options->profile));
	printf("ring_entries=%u\n", RING_ENTRIES);
	printf("cq_entries=%u\n", CQ_ENTRIES);
	printf("point_warmups=%u\n", POINT_WARMUPS);
	printf("point_rounds=%u\n", POINT_ROUNDS);
	printf("r0_events=%u\n", R0_EVENTS);
	printf("lifecycle_events=%u\n", LIFECYCLE_EVENTS);
	printf("cpu=%u\n", options->cpu);
	printf("input_sha256=%s\n", INPUT_SHA256);
	printf("target_sha256=%s\n", TARGET_SHA256);
}

int main(int argc, char **argv)
{
	struct options options;
	struct identity identity;
	struct geometry geometry;
	const char *trace_env;
	bool trace_enabled;
	FILE *output;
	unsigned int rounds, events, round;
	int status = EXIT_SUCCESS;

	if (parse_options(argc, argv, &options) != 0) {
		usage(argv[0]);
		return EXIT_FAILURE;
	}
	if (options.mode == MODE_DESCRIBE) {
		describe(&options);
		return EXIT_SUCCESS;
	}
	if (!options.execute) {
		fprintf(stderr, "Execution requires --execute\n");
		return EXIT_FAILURE;
	}
	if (pin_cpu(options.cpu) != 0) {
		perror("sched_setaffinity");
		return EXIT_FAILURE;
	}
	signal(SIGINT, handle_signal);
	signal(SIGTERM, handle_signal);
	memset(&identity, 0, sizeof(identity));
	if (read_text("/proc/sys/kernel/osrelease", identity.kernel_release,
		      sizeof(identity.kernel_release)) != 0 ||
	    read_text("/proc/sys/kernel/random/boot_id", identity.boot_id,
		      sizeof(identity.boot_id)) != 0)
		return EXIT_FAILURE;
	if (discover_geometry(&geometry) != 0) {
		perror("io_uring geometry discovery");
		return EXIT_FAILURE;
	}
	trace_env = getenv("MEMMAP_TRACE_ENABLED");
	trace_enabled = trace_env && !strcmp(trace_env, "1");
	output = fopen(options.output_path, "w");
	if (!output) {
		perror("fopen output");
		return EXIT_FAILURE;
	}
	if (write_header(output) != 0) {
		fclose(output);
		return EXIT_FAILURE;
	}
	if (options.mode == MODE_POINT) {
		rounds = POINT_WARMUPS + POINT_ROUNDS;
		events = options.profile == PROFILE_R0_REMAP ||
			 options.profile == PROFILE_A0_ANON_MAP ?
			 R0_EVENTS : LIFECYCLE_EVENTS;
	} else {
		rounds = 1U;
		events = options.mode == MODE_TRACE ? TRACE_EVENTS : SMOKE_EVENTS;
	}
	if (options.profile == PROFILE_E0_ERRORS)
		events = 2U;
	for (round = 0; round < rounds && !stop_requested; round++) {
		struct counters counters;
		const char *phase = options.mode == MODE_POINT &&
			round < POINT_WARMUPS ? "warmup" :
			(options.mode == MODE_POINT ? "measured" :
			 (options.mode == MODE_TRACE ? "trace" : "smoke"));
		unsigned int visible_round = options.mode == MODE_POINT ?
			(round < POINT_WARMUPS ? round : round - POINT_WARMUPS) : 0U;

		if (run_profile(options.profile, events, &geometry, &counters) != 0 ||
		    !counters_pass(&counters, options.profile))
			status = EXIT_FAILURE;
		if (write_result(output, &options, &identity, &geometry, phase,
				 visible_round, &counters, trace_enabled) != 0)
			status = EXIT_FAILURE;
		fflush(output);
		if (status != EXIT_SUCCESS)
			break;
	}
	if (stop_requested)
		status = EXIT_FAILURE;
	if (fclose(output) != 0)
		status = EXIT_FAILURE;
	return status;
}
