// SPDX-License-Identifier: GPL-2.0-only
/*
 * Reverse guard for the shared mprotect PTE-hint candidate.
 *
 * Create a 2 MiB-aligned MAP_SHARED anonymous mapping, ask the kernel to
 * collapse it into a THP, split only the PMD mapping into PTEs, and retain the
 * large compound folio underneath.  Then time the same read-only/restore/write
 * sequence as the base-page workload.  Run as root so pagemap exposes PFNs.
 */

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>

#ifndef MADV_COLLAPSE
#define MADV_COLLAPSE 25
#endif
#ifndef MAP_FIXED_NOREPLACE
#define MAP_FIXED_NOREPLACE 0x100000
#endif
#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif

#define HPAGE_SIZE (2UL * 1024UL * 1024UL)
#define PAGEMAP_PRESENT (1ULL << 63)
#define PAGEMAP_PFN_MASK ((1ULL << 55) - 1)
#define KPF_COMPOUND_HEAD 15
#define KPF_COMPOUND_TAIL 16
#define KPF_HUGE 17
#define KPF_THP 22

struct smaps_state {
	unsigned int vmas;
	uint64_t kernel_page_min_kb;
	uint64_t kernel_page_max_kb;
	uint64_t mmu_page_min_kb;
	uint64_t mmu_page_max_kb;
	uint64_t anon_huge_kb;
	uint64_t shmem_pmd_kb;
	uint64_t file_pmd_kb;
	int thpeligible;
};

struct folio_state {
	uint64_t present;
	uint64_t pfn_zero;
	uint64_t compound_head;
	uint64_t compound_tail;
	uint64_t huge;
	uint64_t thp;
};

static volatile uint64_t sink;

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
	if (errno || !end || *end != '\0') {
		fprintf(stderr, "invalid %s=%s\n", name, value);
		exit(2);
	}
	return parsed;
}

static void *map_aligned_shared(size_t len)
{
	for (int attempt = 0; attempt < 16; attempt++) {
		size_t reserve_len = len + HPAGE_SIZE;
		void *reserve = mmap(NULL, reserve_len, PROT_NONE,
				MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
		uintptr_t base, aligned;
		void *mapped;

		if (reserve == MAP_FAILED)
			return MAP_FAILED;
		base = (uintptr_t)reserve;
		aligned = (base + HPAGE_SIZE - 1) & ~(HPAGE_SIZE - 1);
		if (munmap(reserve, reserve_len) != 0)
			return MAP_FAILED;

		mapped = mmap((void *)aligned, len, PROT_READ | PROT_WRITE,
				MAP_SHARED | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE,
				-1, 0);
		if (mapped != MAP_FAILED)
			return mapped;
		if (errno != EEXIST)
			return MAP_FAILED;
	}
	errno = EEXIST;
	return MAP_FAILED;
}

static void touch_write(unsigned char *addr, size_t len, size_t page,
			uint64_t *checksum)
{
	for (size_t off = 0; off < len; off += page) {
		addr[off] = (unsigned char)(addr[off] + 1U);
		*checksum += addr[off];
	}
}

static void touch_read(const unsigned char *addr, size_t len, size_t page,
		       uint64_t *checksum)
{
	for (size_t off = 0; off < len; off += page)
		*checksum += addr[off];
}

static void update_minmax(uint64_t value, uint64_t *min, uint64_t *max)
{
	if (*min == 0 || value < *min)
		*min = value;
	if (value > *max)
		*max = value;
}

static void read_smaps_range(void *addr, size_t len, struct smaps_state *state)
{
	FILE *fp;
	char line[512];
	uintptr_t wanted_start = (uintptr_t)addr;
	uintptr_t wanted_end = wanted_start + len;
	int in_range = 0;

	memset(state, 0, sizeof(*state));
	state->thpeligible = -1;
	fp = fopen("/proc/self/smaps", "r");
	if (!fp)
		return;

	while (fgets(line, sizeof(line), fp)) {
		unsigned long start = 0, end = 0, value = 0;

		if (sscanf(line, "%lx-%lx", &start, &end) == 2) {
			in_range = start < wanted_end && end > wanted_start;
			if (in_range)
				state->vmas++;
			continue;
		}
		if (!in_range)
			continue;
		if (sscanf(line, "KernelPageSize: %lu kB", &value) == 1)
			update_minmax(value, &state->kernel_page_min_kb,
					&state->kernel_page_max_kb);
		else if (sscanf(line, "MMUPageSize: %lu kB", &value) == 1)
			update_minmax(value, &state->mmu_page_min_kb,
					&state->mmu_page_max_kb);
		else if (sscanf(line, "AnonHugePages: %lu kB", &value) == 1)
			state->anon_huge_kb += value;
		else if (sscanf(line, "ShmemPmdMapped: %lu kB", &value) == 1)
			state->shmem_pmd_kb += value;
		else if (sscanf(line, "FilePmdMapped: %lu kB", &value) == 1)
			state->file_pmd_kb += value;
		else if (sscanf(line, "THPeligible: %lu", &value) == 1 && value)
			state->thpeligible = 1;
	}
	fclose(fp);
}

static int read_u64_at(int fd, uint64_t index, uint64_t *value)
{
	off_t off = (off_t)(index * sizeof(uint64_t));
	return pread(fd, value, sizeof(*value), off) == sizeof(*value) ? 0 : -1;
}

static int flag_set(uint64_t flags, unsigned int bit)
{
	return !!(flags & (1ULL << bit));
}

static int read_folio_state(void *addr, size_t len, size_t page,
			    struct folio_state *state)
{
	int pagemap_fd, kpageflags_fd;

	memset(state, 0, sizeof(*state));
	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	if (pagemap_fd < 0)
		return -1;
	kpageflags_fd = open("/proc/kpageflags", O_RDONLY);
	if (kpageflags_fd < 0) {
		close(pagemap_fd);
		return -1;
	}

	for (size_t off = 0; off < len; off += page) {
		uintptr_t va = (uintptr_t)addr + off;
		uint64_t entry, pfn, flags;

		if (read_u64_at(pagemap_fd, va / page, &entry) != 0)
			goto fail;
		if (!(entry & PAGEMAP_PRESENT))
			continue;
		state->present++;
		pfn = entry & PAGEMAP_PFN_MASK;
		if (!pfn) {
			state->pfn_zero++;
			continue;
		}
		if (read_u64_at(kpageflags_fd, pfn, &flags) != 0)
			goto fail;
		state->compound_head += flag_set(flags, KPF_COMPOUND_HEAD);
		state->compound_tail += flag_set(flags, KPF_COMPOUND_TAIL);
		state->huge += flag_set(flags, KPF_HUGE);
		state->thp += flag_set(flags, KPF_THP);
	}
	close(kpageflags_fd);
	close(pagemap_fd);
	return 0;

fail:
	close(kpageflags_fd);
	close(pagemap_fd);
	return -1;
}

static int one_iteration(unsigned char *addr, size_t len, size_t page,
			 uint64_t *protect_ns, uint64_t *restore_ns,
			 uint64_t *touch_ns, uint64_t *checksum)
{
	uint64_t start, end;

	start = now_ns();
	if (mprotect(addr, len, PROT_READ) != 0)
		return -1;
	end = now_ns();
	*protect_ns += end - start;

	start = now_ns();
	if (mprotect(addr, len, PROT_READ | PROT_WRITE) != 0)
		return -1;
	end = now_ns();
	*restore_ns += end - start;

	start = now_ns();
	touch_write(addr, len, page, checksum);
	end = now_ns();
	*touch_ns += end - start;
	return 0;
}

int main(void)
{
	unsigned long iterations = env_ulong("ITERATIONS", 200);
	unsigned long warmup = env_ulong("WARMUP", 5);
	size_t page = (size_t)sysconf(_SC_PAGESIZE);
	size_t len = HPAGE_SIZE;
	size_t pages;
	unsigned char *addr;
	struct smaps_state collapsed, split, after;
	struct folio_state split_folio, after_folio;
	uint64_t checksum = 0, protect_ns = 0, restore_ns = 0, touch_ns = 0;
	int collapse_errno = 0;
	int shape_ok;

	if (!page)
		page = 4096;
	pages = len / page;
	addr = map_aligned_shared(len);
	if (addr == MAP_FAILED) {
		perror("aligned MAP_SHARED mmap");
		return 1;
	}
	if (madvise(addr, len, MADV_HUGEPAGE) != 0) {
		perror("madvise(MADV_HUGEPAGE)");
		return 1;
	}
	touch_write(addr, len, page, &checksum);
	if (madvise(addr, len, MADV_COLLAPSE) != 0)
		collapse_errno = errno;
	read_smaps_range(addr, len, &collapsed);
	if (collapse_errno) {
		printf("unavailable reason=madvise_collapse errno=%d error=%s\n",
		       collapse_errno, strerror(collapse_errno));
		munmap(addr, len);
		return 4;
	}

	/* A partial protection change forces a PMD mapping down to PTEs. */
	if (mprotect(addr + page, page, PROT_READ) != 0 ||
	    mprotect(addr + page, page, PROT_READ | PROT_WRITE) != 0) {
		perror("partial mprotect split");
		return 1;
	}
	if (madvise(addr, len, MADV_NOHUGEPAGE) != 0) {
		perror("madvise(MADV_NOHUGEPAGE)");
		return 1;
	}
	/* The partial protection change may leave the split PTEs non-present.
	 * Fault them back in outside the timed region without modifying the folio.
	 */
	touch_read(addr, len, page, &checksum);
	read_smaps_range(addr, len, &split);
	if (read_folio_state(addr, len, page, &split_folio) != 0) {
		perror("read split folio state");
		return 1;
	}

	for (unsigned long i = 0; i < warmup; i++) {
		uint64_t p = 0, r = 0, t = 0;
		if (one_iteration(addr, len, page, &p, &r, &t, &checksum) != 0) {
			perror("warmup mprotect");
			return 1;
		}
	}
	for (unsigned long i = 0; i < iterations; i++) {
		if (one_iteration(addr, len, page, &protect_ns, &restore_ns,
				  &touch_ns, &checksum) != 0) {
			perror("measured mprotect");
			return 1;
		}
	}
	read_smaps_range(addr, len, &after);
	if (read_folio_state(addr, len, page, &after_folio) != 0) {
		perror("read final folio state");
		return 1;
	}

	shape_ok = collapsed.vmas > 0 &&
		(collapsed.anon_huge_kb + collapsed.shmem_pmd_kb +
		 collapsed.file_pmd_kb) >= len / 1024 &&
		split.vmas > 0 && split.kernel_page_min_kb == page / 1024 &&
		split.mmu_page_min_kb == page / 1024 &&
		split.shmem_pmd_kb == 0 && split.file_pmd_kb == 0 &&
		split_folio.present == pages && split_folio.pfn_zero == 0 &&
		split_folio.compound_head > 0 && split_folio.compound_tail > 0 &&
		(split_folio.thp > 0 || split_folio.huge > 0) &&
		after_folio.present == pages && after_folio.pfn_zero == 0 &&
		after_folio.compound_head > 0 && after_folio.compound_tail > 0 &&
		(after_folio.thp > 0 || after_folio.huge > 0);

	sink += checksum;
	printf("result scenario=shared_pte_mapped_thp mapping_mb=2 pages=%zu "
	       "iterations=%lu warmup=%lu protect_ns_per_page=%" PRIu64 " "
	       "restore_ns_per_page=%" PRIu64 " touch_ns_per_page=%" PRIu64 " "
	       "iteration_ns_per_page=%" PRIu64 " expected_match_ratio=%d "
	       "unexpected_results=%d collapsed_anon_huge_kb=%" PRIu64 " "
	       "collapsed_shmem_pmd_kb=%" PRIu64 " collapsed_file_pmd_kb=%" PRIu64 " "
	       "collapsed_large_kb=%" PRIu64 " "
	       "split_kernel_page_min_kb=%" PRIu64 " split_mmu_page_min_kb=%" PRIu64 " "
	       "split_shmem_pmd_kb=%" PRIu64 " split_file_pmd_kb=%" PRIu64 " "
	       "split_present=%" PRIu64 " split_pfn_zero=%" PRIu64 " "
	       "split_compound_head=%" PRIu64 " split_compound_tail=%" PRIu64 " "
	       "split_thp=%" PRIu64 " split_huge=%" PRIu64 " "
	       "after_compound_head=%" PRIu64 " after_compound_tail=%" PRIu64 " "
	       "after_thp=%" PRIu64 " after_huge=%" PRIu64 " checksum=%" PRIu64 "\n",
	       pages, iterations, warmup,
	       protect_ns / (iterations * pages),
	       restore_ns / (iterations * pages),
	       touch_ns / (iterations * pages),
	       (protect_ns + restore_ns + touch_ns) / (iterations * pages),
	       shape_ok ? 100 : 0, shape_ok ? 0 : 1,
	       collapsed.anon_huge_kb, collapsed.shmem_pmd_kb,
	       collapsed.file_pmd_kb,
	       collapsed.anon_huge_kb + collapsed.shmem_pmd_kb +
	       collapsed.file_pmd_kb, split.kernel_page_min_kb,
	       split.mmu_page_min_kb, split.shmem_pmd_kb, split.file_pmd_kb,
	       split_folio.present, split_folio.pfn_zero,
	       split_folio.compound_head, split_folio.compound_tail,
	       split_folio.thp, split_folio.huge,
	       after_folio.compound_head, after_folio.compound_tail,
	       after_folio.thp, after_folio.huge, checksum);

	munmap(addr, len);
	return shape_ok ? (sink == UINT64_MAX ? 1 : 0) : 3;
}
