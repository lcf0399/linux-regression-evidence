// SPDX-License-Identifier: GPL-2.0-only
/*
 * Single-shot shared-dirty mprotect(PROT_READ) probe.
 *
 * This is a follow-up for the shared-dirty mprotect toggle regression.  Unlike
 * the toggle reproducer, each timed iteration creates a fresh
 * MAP_SHARED|MAP_ANONYMOUS mapping, write-prefaults it, performs exactly one
 * mprotect(PROT_READ), and then unmaps it.  The primary timing metric is
 * single_protect_ns_per_page.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif

struct smaps_state {
	int found;
	uint64_t kernel_page_kb;
	uint64_t mmu_page_kb;
	uint64_t anon_huge_kb;
	int thpeligible;
};

static volatile unsigned long sink;

static uint64_t now_ns(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
		if (errno != EINVAL || clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
			perror("clock_gettime");
			exit(1);
		}
	}
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static unsigned long env_ulong(const char *name, unsigned long fallback)
{
	const char *value = getenv(name);
	char *end = NULL;
	unsigned long parsed;

	if (!value || !*value)
		return fallback;
	errno = 0;
	parsed = strtoul(value, &end, 0);
	if (errno || !end || *end != '\0' || parsed == 0)
		return fallback;
	return parsed;
}

static void touch_write(unsigned char *addr, size_t len, size_t page,
			uint64_t *checksum)
{
	for (size_t off = 0; off < len; off += page) {
		addr[off] = (unsigned char)(addr[off] + 1U);
		*checksum += addr[off];
	}
}

static void read_smaps_state(void *addr, struct smaps_state *state)
{
	FILE *fp;
	char line[512];
	uintptr_t target = (uintptr_t)addr;
	int in_vma = 0;

	memset(state, 0, sizeof(*state));
	state->thpeligible = -1;

	fp = fopen("/proc/self/smaps", "r");
	if (!fp)
		return;

	while (fgets(line, sizeof(line), fp)) {
		unsigned long start = 0, end = 0;
		unsigned long value = 0;

		if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
			if (in_vma)
				break;
			if (target >= start && target < end) {
				in_vma = 1;
				state->found = 1;
			}
			continue;
		}

		if (!in_vma)
			continue;

		if (sscanf(line, "KernelPageSize: %lu kB", &value) == 1)
			state->kernel_page_kb = value;
		else if (sscanf(line, "MMUPageSize: %lu kB", &value) == 1)
			state->mmu_page_kb = value;
		else if (sscanf(line, "AnonHugePages: %lu kB", &value) == 1)
			state->anon_huge_kb = value;
		else if (sscanf(line, "THPeligible: %lu", &value) == 1)
			state->thpeligible = (int)value;
	}

	fclose(fp);
}

static int state_ok(const struct smaps_state *state, size_t page)
{
	if (!state->found)
		return 1;
	return state->kernel_page_kb == page / 1024 &&
	       state->mmu_page_kb == page / 1024 &&
	       state->anon_huge_kb == 0 &&
	       state->thpeligible <= 0;
}

static int one_single_protect(size_t len, size_t page, uint64_t *setup_ns,
			      uint64_t *protect_ns, uint64_t *total_ns,
			      uint64_t *checksum, uint64_t *state_checks,
			      uint64_t *state_failures)
{
	unsigned char *addr;
	struct smaps_state before;
	uint64_t t0, t1, t2, t3;

	t0 = now_ns();
	addr = mmap(NULL, len, PROT_READ | PROT_WRITE,
		    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (addr == MAP_FAILED) {
		perror("mmap");
		return -1;
	}
	(void)madvise(addr, len, MADV_NOHUGEPAGE);
	touch_write(addr, len, page, checksum);
	read_smaps_state(addr, &before);
	(*state_checks)++;
	if (!state_ok(&before, page))
		(*state_failures)++;
	t1 = now_ns();

	if (mprotect(addr, len, PROT_READ) != 0) {
		perror("mprotect(PROT_READ)");
		munmap(addr, len);
		return -1;
	}
	t2 = now_ns();

	if (munmap(addr, len) != 0) {
		perror("munmap");
		return -1;
	}
	t3 = now_ns();

	*setup_ns += t1 - t0;
	*protect_ns += t2 - t1;
	*total_ns += t3 - t0;
	return 0;
}

int main(void)
{
	unsigned long mapping_mb = env_ulong("MAPPING_MB", 64);
	unsigned long iterations = env_ulong("ITERATIONS", 200);
	unsigned long warmup = env_ulong("WARMUP", 5);
	unsigned long external_rounds = env_ulong("EXTERNAL_ROUNDS", 1);
	size_t page = (size_t)sysconf(_SC_PAGESIZE);
	size_t len;
	size_t pages;
	uint64_t setup_ns = 0;
	uint64_t protect_ns = 0;
	uint64_t total_ns = 0;
	uint64_t checksum = 0;
	uint64_t state_checks = 0;
	uint64_t state_failures = 0;
	uint64_t total_iterations;
	uint64_t expected_match_ratio;
	uint64_t unexpected_results;

	if (!page)
		page = 4096;
	len = mapping_mb * 1024UL * 1024UL;
	len = (len + page - 1) & ~(page - 1);
	pages = len / page;
	total_iterations = iterations * external_rounds;
	if (!pages || !total_iterations)
		return 2;

	for (unsigned long i = 0; i < warmup; i++) {
		uint64_t dummy_setup = 0, dummy_protect = 0, dummy_total = 0;
		if (one_single_protect(len, page, &dummy_setup, &dummy_protect,
				       &dummy_total, &checksum, &state_checks,
				       &state_failures) != 0)
			return 1;
	}

	for (uint64_t i = 0; i < total_iterations; i++) {
		if (one_single_protect(len, page, &setup_ns, &protect_ns,
				       &total_ns, &checksum, &state_checks,
				       &state_failures) != 0)
			return 1;
	}

	sink += (unsigned long)checksum;
	unexpected_results = state_failures;
	expected_match_ratio = state_checks ?
		((state_checks - state_failures) * 100) / state_checks : 0;

	printf("result scenario=shared_dirty_single_protect "
	       "pattern=single_protect mapping_mb=%lu page_size=%zu pages=%zu "
	       "external_rounds=%lu iterations_per_round=%lu total_iterations=%" PRIu64 " warmup=%lu "
	       "setup_ns_per_page=%" PRIu64 " single_protect_ns_per_page=%" PRIu64 " "
	       "total_ns_per_page=%" PRIu64 " expected_match_ratio=%" PRIu64 " "
	       "unexpected_results=%" PRIu64 " state_checks=%" PRIu64 " checksum=%" PRIu64 "\n",
	       mapping_mb, page, pages, external_rounds, iterations,
	       total_iterations, warmup,
	       setup_ns / (total_iterations * pages),
	       protect_ns / (total_iterations * pages),
	       total_ns / (total_iterations * pages),
	       expected_match_ratio, unexpected_results, state_checks, checksum);

	return unexpected_results || sink == 0xdeadbeefUL ? 1 : 0;
}
