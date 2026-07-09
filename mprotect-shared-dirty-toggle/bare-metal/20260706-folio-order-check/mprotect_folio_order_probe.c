// SPDX-License-Identifier: GPL-2.0-only
/*
 * Inspect the backing page/folio shape of the shared-dirty mprotect mapping.
 *
 * This is attribution-only.  It complements smaps' 4 KiB/no-THP checks by
 * reading pagemap/kpageflags as root and counting compound/THP flags for each
 * mapped base page.
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
#include <unistd.h>

#ifndef MADV_COLLAPSE
#define MADV_COLLAPSE 25
#endif

#define PAGEMAP_PRESENT (1ULL << 63)
#define PAGEMAP_PFN_MASK ((1ULL << 55) - 1)

#define KPF_COMPOUND_HEAD 15
#define KPF_COMPOUND_TAIL 16
#define KPF_HUGE 17
#define KPF_THP 22

struct smaps_state {
	int found;
	uint64_t kernel_page_kb;
	uint64_t mmu_page_kb;
	uint64_t anon_huge_kb;
	int thpeligible;
};

static volatile unsigned long sink;

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

static int read_u64_at(int fd, uint64_t index, uint64_t *value)
{
	off_t off;
	ssize_t ret;

	if (index > UINT64_MAX / sizeof(uint64_t)) {
		errno = EOVERFLOW;
		return -1;
	}
	off = (off_t)(index * sizeof(uint64_t));
	ret = pread(fd, value, sizeof(*value), off);
	if (ret != sizeof(*value))
		return -1;
	return 0;
}

static int flag_set(uint64_t flags, int bit)
{
	return !!(flags & (1ULL << bit));
}

int main(void)
{
	unsigned long mapping_mb = env_ulong("MAPPING_MB", 64);
	size_t page = (size_t)sysconf(_SC_PAGESIZE);
	size_t len = mapping_mb * 1024UL * 1024UL;
	size_t pages;
	unsigned char *addr;
	struct smaps_state smaps;
	uint64_t checksum = 0;
	uint64_t present = 0, pfn_zero = 0;
	uint64_t compound_head = 0, compound_tail = 0, huge = 0, thp = 0;
	uint64_t contiguous = 0, contiguous_breaks = 0;
	uint64_t first_pfn = UINT64_MAX, last_pfn = 0, prev_pfn = UINT64_MAX;
	int pagemap_fd, kpageflags_fd;

	if (!page)
		page = 4096;
	len = (len + page - 1) & ~(page - 1);
	pages = len / page;
	if (!pages)
		return 2;

	addr = mmap(NULL, len, PROT_READ | PROT_WRITE,
		    MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (addr == MAP_FAILED) {
		perror("mmap");
		return 1;
	}
	(void)madvise(addr, len, MADV_NOHUGEPAGE);
	touch_write(addr, len, page, &checksum);
	read_smaps_state(addr, &smaps);

	pagemap_fd = open("/proc/self/pagemap", O_RDONLY);
	if (pagemap_fd < 0) {
		perror("open pagemap");
		return 1;
	}
	kpageflags_fd = open("/proc/kpageflags", O_RDONLY);
	if (kpageflags_fd < 0) {
		perror("open kpageflags");
		return 1;
	}

	for (size_t i = 0; i < pages; i++) {
		uintptr_t va = (uintptr_t)addr + i * page;
		uint64_t entry, pfn, flags;

		if (read_u64_at(pagemap_fd, va / page, &entry) != 0) {
			perror("read pagemap");
			return 1;
		}
		if (!(entry & PAGEMAP_PRESENT))
			continue;
		present++;
		pfn = entry & PAGEMAP_PFN_MASK;
		if (!pfn) {
			pfn_zero++;
			continue;
		}
		if (read_u64_at(kpageflags_fd, pfn, &flags) != 0) {
			perror("read kpageflags");
			return 1;
		}

		if (first_pfn == UINT64_MAX)
			first_pfn = pfn;
		last_pfn = pfn;
		if (prev_pfn != UINT64_MAX) {
			if (pfn == prev_pfn + 1)
				contiguous++;
			else
				contiguous_breaks++;
		}
		prev_pfn = pfn;

		compound_head += flag_set(flags, KPF_COMPOUND_HEAD);
		compound_tail += flag_set(flags, KPF_COMPOUND_TAIL);
		huge += flag_set(flags, KPF_HUGE);
		thp += flag_set(flags, KPF_THP);
	}

	close(kpageflags_fd);
	close(pagemap_fd);
	if (munmap(addr, len) != 0) {
		perror("munmap");
		return 1;
	}

	sink += (unsigned long)checksum;
	printf("result scenario=mprotect_folio_order mapping_mb=%lu page_size=%zu pages=%zu "
	       "present=%" PRIu64 " pfn_zero=%" PRIu64 " compound_head=%" PRIu64 " "
	       "compound_tail=%" PRIu64 " kpf_huge=%" PRIu64 " kpf_thp=%" PRIu64 " "
	       "contiguous_edges=%" PRIu64 " contiguous_breaks=%" PRIu64 " "
	       "first_pfn=%" PRIu64 " last_pfn=%" PRIu64 " "
	       "smaps_found=%d smaps_kernel_page_kb=%" PRIu64 " smaps_mmu_page_kb=%" PRIu64 " "
	       "smaps_anon_huge_kb=%" PRIu64 " smaps_thpeligible=%d checksum=%" PRIu64 "\n",
	       mapping_mb, page, pages, present, pfn_zero, compound_head,
	       compound_tail, huge, thp, contiguous, contiguous_breaks,
	       first_pfn == UINT64_MAX ? 0 : first_pfn, last_pfn, smaps.found,
	       smaps.kernel_page_kb, smaps.mmu_page_kb, smaps.anon_huge_kb,
	       smaps.thpeligible, checksum);

	return sink == 0xdeadbeefUL ? 1 : 0;
}
