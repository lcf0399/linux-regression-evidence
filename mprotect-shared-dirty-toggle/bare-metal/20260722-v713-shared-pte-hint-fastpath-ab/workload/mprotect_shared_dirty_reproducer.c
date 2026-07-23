// SPDX-License-Identifier: GPL-2.0-only
/*
 * Minimal userspace reproducer for the shared-dirty mprotect() toggle
 * workload discussed on linux-mm.
 *
 * It intentionally keeps the setup narrow:
 *
 *   - MAP_SHARED | MAP_ANONYMOUS mapping
 *   - prefault/write-dirty the whole range before timing
 *   - repeatedly mprotect(PROT_READ)
 *   - restore with mprotect(PROT_READ | PROT_WRITE)
 *   - write-touch the mapping after each protection cycle
 *
 * The metric names avoid "cycle" ambiguity: iteration_ns_per_page is wall-clock
 * nanoseconds per base page for one full protect/restore/post-touch iteration.
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

struct options {
	const char *scenario;
	size_t mapping_mb;
	unsigned long iterations;
	unsigned long external_rounds;
	unsigned long warmup;
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

static unsigned long parse_ulong(const char *arg, const char *name)
{
	char *end = NULL;
	unsigned long value;

	errno = 0;
	value = strtoul(arg, &end, 0);
	if (errno || !end || *end != '\0') {
		fprintf(stderr, "invalid %s: %s\n", name, arg);
		exit(2);
	}
	return value;
}

static void usage(const char *prog)
{
	fprintf(stderr,
		"usage: %s [SCENARIO] [EXTERNAL_ROUNDS] [--mapping-mb N] [--iterations N] [--warmup N]\n"
		"\n"
		"defaults: SCENARIO=shared_dirty_full_toggle_64m EXTERNAL_ROUNDS=9\n"
		"          --mapping-mb 64 --iterations 1000 --warmup 10\n",
		prog);
}

static int is_supported_scenario(const char *name)
{
	return strcmp(name, "shared_dirty_full_toggle_64m") == 0 ||
	       strcmp(name, "mprotect_shared_dirty_64m") == 0;
}

static void parse_args(int argc, char **argv, struct options *opts)
{
	opts->scenario = "shared_dirty_full_toggle_64m";
	opts->mapping_mb = 64;
	opts->iterations = 1000;
	opts->external_rounds = 9;
	opts->warmup = 10;

	for (int i = 1; i < argc; i++) {
		if (strcmp(argv[i], "--mapping-mb") == 0 && i + 1 < argc) {
			opts->mapping_mb = parse_ulong(argv[++i], "mapping-mb");
		} else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc) {
			opts->iterations = parse_ulong(argv[++i], "iterations");
		} else if (strcmp(argv[i], "--warmup") == 0 && i + 1 < argc) {
			opts->warmup = parse_ulong(argv[++i], "warmup");
		} else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
			usage(argv[0]);
			exit(0);
		} else if (argv[i][0] != '-' && is_supported_scenario(argv[i])) {
			opts->scenario = argv[i];
		} else if (argv[i][0] != '-') {
			opts->external_rounds = parse_ulong(argv[i], "external-rounds");
		} else {
			usage(argv[0]);
			exit(2);
		}
	}

	if (opts->mapping_mb == 0 || opts->iterations == 0 || opts->external_rounds == 0) {
		fprintf(stderr, "mapping-mb, iterations and external rounds must be non-zero\n");
		exit(2);
	}
}

static void touch_write(unsigned char *addr, size_t len, size_t page,
			uint64_t *pages_touched, uint64_t *checksum)
{
	for (size_t off = 0; off < len; off += page) {
		addr[off] = (unsigned char)(addr[off] + 1U);
		*checksum += addr[off];
		(*pages_touched)++;
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

static int one_iteration(unsigned char *addr, size_t len, size_t page,
			 uint64_t *protect_ns, uint64_t *restore_ns,
			 uint64_t *post_touch_ns, uint64_t *pages_touched,
			 uint64_t *checksum)
{
	uint64_t start, end;

	start = now_ns();
	if (mprotect(addr, len, PROT_READ) != 0) {
		perror("mprotect(PROT_READ)");
		return -1;
	}
	end = now_ns();
	*protect_ns += end - start;

	start = now_ns();
	if (mprotect(addr, len, PROT_READ | PROT_WRITE) != 0) {
		perror("mprotect(PROT_READ|PROT_WRITE)");
		return -1;
	}
	end = now_ns();
	*restore_ns += end - start;

	start = now_ns();
	touch_write(addr, len, page, pages_touched, checksum);
	end = now_ns();
	*post_touch_ns += end - start;

	return 0;
}

int main(int argc, char **argv)
{
	struct options opts;
	size_t page;
	size_t len;
	size_t pages;
	unsigned long total_iterations;
	unsigned char *addr;
	struct smaps_state before;
	struct smaps_state after;
	uint64_t prefault_ns = 0;
	uint64_t protect_ns = 0;
	uint64_t restore_ns = 0;
	uint64_t post_touch_ns = 0;
	uint64_t pages_touched = 0;
	uint64_t checksum = 0;
	uint64_t start, end;
	uint64_t expected_match_ratio;
	uint64_t unexpected_results;
	int smaps_available;
	int state_ok;

	parse_args(argc, argv, &opts);

	page = (size_t)sysconf(_SC_PAGESIZE);
	if (!page)
		page = 4096;

	len = opts.mapping_mb * 1024UL * 1024UL;
	len = (len + page - 1) & ~(page - 1);
	pages = len / page;
	total_iterations = opts.iterations * opts.external_rounds;

	addr = mmap(NULL, len, PROT_READ | PROT_WRITE,
		    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (addr == MAP_FAILED) {
		perror("mmap(MAP_SHARED|MAP_ANONYMOUS)");
		return 1;
	}

	start = now_ns();
	touch_write(addr, len, page, &pages_touched, &checksum);
	end = now_ns();
	prefault_ns = end - start;

	read_smaps_state(addr, &before);

	for (unsigned long i = 0; i < opts.warmup; i++) {
		uint64_t dummy_protect = 0;
		uint64_t dummy_restore = 0;
		uint64_t dummy_post = 0;
		if (one_iteration(addr, len, page, &dummy_protect, &dummy_restore,
				  &dummy_post, &pages_touched, &checksum) != 0) {
			munmap(addr, len);
			return 1;
		}
	}

	for (unsigned long i = 0; i < total_iterations; i++) {
		if (one_iteration(addr, len, page, &protect_ns, &restore_ns,
				  &post_touch_ns, &pages_touched, &checksum) != 0) {
			munmap(addr, len);
			return 1;
		}
	}

	read_smaps_state(addr, &after);
	munmap(addr, len);

	sink += (unsigned long)checksum;

	smaps_available = before.found && after.found;
	state_ok = smaps_available &&
		   before.kernel_page_kb == page / 1024 &&
		   after.kernel_page_kb == page / 1024 &&
		   before.mmu_page_kb == page / 1024 &&
		   after.mmu_page_kb == page / 1024 &&
		   before.anon_huge_kb == 0 &&
		   after.anon_huge_kb == 0 &&
		   before.thpeligible <= 0 &&
		   after.thpeligible <= 0;
	expected_match_ratio = !smaps_available || state_ok ? 100 : 0;
	unexpected_results = smaps_available && !state_ok ? 1 : 0;

	printf("workload=mprotect_shared_dirty_reproducer source=linux/mm/mprotect.c scenarios=1 rounds=%lu\n",
	       opts.external_rounds);
	printf("reproducer=mprotect_shared_dirty "
	       "scenario=%s pattern=shared_dirty_full_toggle mapping_mb=%zu page_size=%zu pages=%zu "
	       "external_rounds=%lu iterations_per_round=%lu total_iterations=%lu warmup=%lu "
	       "prefault_ns=%" PRIu64 " "
	       "protect_ns_per_page=%" PRIu64 " "
	       "restore_ns_per_page=%" PRIu64 " "
	       "post_touch_ns_per_page=%" PRIu64 " "
	       "iteration_ns_per_page=%" PRIu64 " "
	       "smaps_available=%d "
	       "smaps_before_found=%d smaps_before_kernel_page_kb=%" PRIu64 " "
	       "smaps_before_mmu_page_kb=%" PRIu64 " smaps_before_anon_huge_kb=%" PRIu64 " "
	       "smaps_before_thpeligible=%d "
	       "smaps_after_found=%d smaps_after_kernel_page_kb=%" PRIu64 " "
	       "smaps_after_mmu_page_kb=%" PRIu64 " smaps_after_anon_huge_kb=%" PRIu64 " "
	       "smaps_after_thpeligible=%d "
	       "expected_match_ratio=%" PRIu64 " unexpected_results=%" PRIu64 " checksum=%" PRIu64 "\n",
	       opts.scenario, opts.mapping_mb, page, pages,
	       opts.external_rounds, opts.iterations, total_iterations, opts.warmup,
	       prefault_ns,
	       protect_ns / (total_iterations * pages),
	       restore_ns / (total_iterations * pages),
	       post_touch_ns / (total_iterations * pages),
	       (protect_ns + restore_ns + post_touch_ns) / (total_iterations * pages),
	       smaps_available,
	       before.found, before.kernel_page_kb, before.mmu_page_kb,
	       before.anon_huge_kb, before.thpeligible,
	       after.found, after.kernel_page_kb, after.mmu_page_kb,
	       after.anon_huge_kb, after.thpeligible,
	       expected_match_ratio, unexpected_results, checksum);
	printf("result scenario=%s pattern=shared_dirty_full_toggle "
	       "internal_rounds=%lu mapping_mb=%zu pages=%zu "
	       "protect_ns_per_page=%" PRIu64 " restore_ns_per_page=%" PRIu64 " "
	       "post_touch_ns_per_page=%" PRIu64 " iteration_ns_per_page=%" PRIu64 " "
	       "expected_match_ratio=%" PRIu64 " unexpected_results=%" PRIu64 " "
	       "smaps_available=%d "
	       "smaps_before_kernel_page_kb=%" PRIu64 " smaps_before_mmu_page_kb=%" PRIu64 " "
	       "smaps_before_anon_huge_kb=%" PRIu64 " smaps_before_thpeligible=%d "
	       "smaps_after_kernel_page_kb=%" PRIu64 " smaps_after_mmu_page_kb=%" PRIu64 " "
	       "smaps_after_anon_huge_kb=%" PRIu64 " smaps_after_thpeligible=%d "
	       "checksum=%" PRIu64 "\n",
	       opts.scenario, total_iterations, opts.mapping_mb, pages,
	       protect_ns / (total_iterations * pages),
	       restore_ns / (total_iterations * pages),
	       post_touch_ns / (total_iterations * pages),
	       (protect_ns + restore_ns + post_touch_ns) / (total_iterations * pages),
	       expected_match_ratio, unexpected_results,
	       smaps_available,
	       before.kernel_page_kb, before.mmu_page_kb,
	       before.anon_huge_kb, before.thpeligible,
	       after.kernel_page_kb, after.mmu_page_kb,
	       after.anon_huge_kb, after.thpeligible,
	       checksum);

	return unexpected_results || sink == 0xdeadbeefUL ? 1 : 0;
}
