#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/userfaultfd.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

#define PAGE 4096UL
#define HUGE (512UL * PAGE)
#ifndef UFFD_FEATURE_MOVE
#define UFFD_FEATURE_MOVE (1ULL << 16)
struct uffdio_move { uint64_t dst, src, len, mode; int64_t move; };
#define UFFDIO_MOVE _IOWR(UFFDIO, 0x05, struct uffdio_move)
#define UFFDIO_MOVE_MODE_DONTWAKE (1ULL << 0)
#endif

static int timing, pm = -1, kf = -1;
static uint64_t ns, operations, valid, before_huge, after_huge, zero_ranges;
static uint64_t rss_before, rss_after, minor_faults, same_pfns, folios[10];
static uint64_t post_ns, cleanup_ns, kept_pages, dropped_pages;
static const char *which;
struct area { unsigned char *base, *data; size_t reserved, length; };

static void need(int ok, const char *why)
{
    if (!ok) { fprintf(stderr, "FAIL %s errno=%d (%s)\n", why, errno, strerror(errno)); exit(2); }
}
__attribute__((noinline,used)) void ks_begin(unsigned long stage, void *address, unsigned long length)
{ __asm__ volatile("" : : "r"(stage), "r"(address), "r"(length) : "memory"); }
__attribute__((noinline,used)) void ks_end(void)
{ __asm__ volatile("" : : : "memory"); }
static void cpu(unsigned id)
{
    cpu_set_t set; CPU_ZERO(&set); CPU_SET(id, &set);
    need(!sched_setaffinity(0, sizeof(set), &set), "CPU affinity");
}
static uint64_t now(void)
{
    struct timespec t;
    if (!timing) return 0;
    need(!clock_gettime(CLOCK_MONOTONIC_RAW, &t), "clock");
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
static uint64_t word(int fd, uint64_t index)
{
    uint64_t v;
    need(pread(fd, &v, sizeof(v), (off_t)(index * sizeof(v))) == sizeof(v), "page metadata");
    return v;
}
static uint64_t entry(void *p) { return word(pm, (uintptr_t)p / PAGE); }
static uint64_t pfn(void *p)
{
    uint64_t e = entry(p), n = e & ((1ULL << 55) - 1);
    need((e >> 63) && n, "owned PFN unavailable"); return n;
}
static struct area allocate(unsigned units)
{
    struct area a = { .length = units * HUGE, .reserved = (units + 3UL) * HUGE };
    a.base = mmap(NULL, a.reserved, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    need(a.base != MAP_FAILED, "guarded reserve");
    a.data = (void *)(((uintptr_t)a.base + 2 * HUGE - 1) & ~(HUGE - 1));
    need(!mprotect(a.data, a.length, PROT_READ | PROT_WRITE), "owned access");
    need(!madvise(a.data, a.length, MADV_HUGEPAGE), "THP advice");
    return a;
}
static struct area spaced(unsigned units, unsigned threads)
{
    size_t align = 1UL << 30;
    struct area a = { .length = (threads - 1UL) * align + units / threads * HUGE };
    a.reserved = a.length + 2 * align;
    a.base = mmap(NULL, a.reserved, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    need(a.base != MAP_FAILED, "spaced reserve");
    a.data = (void *)(((uintptr_t)a.base + 2 * align - 1) & ~(align - 1));
    need(!mprotect(a.data, a.length, PROT_READ | PROT_WRITE), "spaced source VMA");
    need(!madvise(a.data, a.length, MADV_HUGEPAGE), "spaced THP advice");
    return a;
}
static void release(struct area a) { need(!munmap(a.base, a.reserved), "unmap owned range"); }
static uint64_t stat_field(void *address, const char *field)
{
    FILE *f = fopen("/proc/self/smaps", "r"); char line[512]; unsigned long lo, hi, n;
    int inside = 0; uint64_t answer = UINT64_MAX;
    need(f != NULL, "smaps open");
    while (fgets(line, sizeof(line), f)) {
        if (sscanf(line, "%lx-%lx", &lo, &hi) == 2) inside = (uintptr_t)address >= lo && (uintptr_t)address < hi;
        if (inside && !strncmp(line, field, strlen(field)) && sscanf(line + strlen(field), "%lu kB", &n) == 1) { answer = n; break; }
    }
    need(!fclose(f) && answer != UINT64_MAX, "smaps field"); return answer;
}
static unsigned huge_count(struct area a)
{
    unsigned count = 0;
    for (size_t off = 0; off < a.length; off += HUGE) {
        uint64_t first = pfn(a.data + off), flags = word(kf, first);
        if ((flags & (1ULL << 15)) && (flags & (1ULL << 22)) &&
            pfn(a.data + off + HUGE - PAGE) == first + 511) count++;
    }
    return count;
}
static unsigned char value(size_t page) { return (unsigned char)(0x31 + page % 173); }
static void populate(struct area a)
{
    for (size_t j = 0; j < a.length; j += PAGE) a.data[j] = value(j / PAGE);
}
static void verify_data(struct area a)
{
    for (size_t j = 0; j < a.length; j += PAGE) need(a.data[j] == value(j / PAGE) && a.data[j + 1] == 0, "data content");
}
static void zero_write(unsigned units, int dense, int direct, int basepages)
{
    struct area a = allocate(units);
    if (basepages) need(!madvise(a.data, a.length, MADV_NOHUGEPAGE), "basepage advice");
    if (!direct) {
        volatile unsigned char sink = 0;
        ks_begin(1, a.data, a.length);
        for (unsigned i = 0; i < units; i++) sink |= *(volatile unsigned char *)(a.data + i * HUGE);
        ks_end(); need(!sink, "read zero content");
        if (!basepages) {
            need(huge_count(a) == units, "huge zero folio precondition");
            for (unsigned i = 0; i < units; i++) need(word(kf, pfn(a.data + i * HUGE)) & (1ULL << 24), "shared huge zero flag");
            zero_ranges += units;
        }
    }
    rss_before += stat_field(a.data, "Rss:");
    struct rusage r0, r1; need(!getrusage(RUSAGE_SELF, &r0), "fault before");
    ks_begin(2, a.data, a.length); uint64_t start = now();
    for (unsigned i = 0; i < units; i++)
        for (unsigned j = 0; j < (dense ? 512U : 1U); j++)
            *(volatile unsigned char *)(a.data + i * HUGE + j * PAGE) = 0x5a;
    ns += now() - start; ks_end(); need(!getrusage(RUSAGE_SELF, &r1), "fault after");
    minor_faults += r1.ru_minflt - r0.ru_minflt;
    /* Inspect before checking data: verification must not create extra sparse pages. */
    rss_after += stat_field(a.data, "Rss:");
    after_huge += stat_field(a.data, "AnonHugePages:") / 2048;
    for (unsigned i = 0; i < units; i++)
        for (unsigned j = 0; j < (dense ? 512U : 1U); j++) {
            unsigned char *p = a.data + i * HUGE + j * PAGE;
            need(p[0] == 0x5a && p[1] == 0, "written data and adjacent zero");
        }
    uint64_t cleanup = now(); release(a); cleanup_ns += now() - cleanup; operations += units; valid++;
}
static void histogram(struct area a)
{
    size_t page = 0, pages = a.length / PAGE;
    while (page < pages) {
        uint64_t first = pfn(a.data + page * PAGE), flags = word(kf, first);
        need(!(flags & (1ULL << 16)), "folio scan starts at head");
        unsigned count = 1, order = 0;
        if (flags & (1ULL << 15)) {
            while (page + count < pages && count < 512) {
                uint64_t n = pfn(a.data + (page + count) * PAGE);
                if (!(word(kf, n) & (1ULL << 16))) break;
                need(n == first + count, "compound PFN continuity"); count++;
            }
            need(count > 1 && !(count & (count - 1)), "compound length");
            while ((1U << order) < count) order++;
        }
        folios[order]++; page += count;
    }
}
static void split_folios(unsigned units, int offset, unsigned order, int zeros)
{
    struct area a = allocate(units); populate(a);
    if (zeros) memset(a.data, 0, a.length);
    need(huge_count(a) == units && stat_field(a.data, "AnonHugePages:") == units * 2048UL, "PMD split precondition");
    before_huge += units;
    uint64_t *pfns = calloc(a.length / PAGE, sizeof(*pfns)); need(pfns != NULL, "PFN vector");
    for (size_t i = 0; i < a.length / PAGE; i++) pfns[i] = pfn(a.data + i * PAGE);
    int fd = open("/sys/kernel/debug/split_huge_pages", O_WRONLY | O_CLOEXEC); need(fd >= 0, "split debugfs");
    for (unsigned i = 0; i < units; i++) {
        char cmd[128]; unsigned char *address = a.data + i * HUGE;
        /* A one-page traversal visits the original folio exactly once. A full
         * range would revisit and repeatedly split its non-uniform children. */
        int n = offset < 0 ? snprintf(cmd, sizeof(cmd), "%d,0x%lx,0x%lx,%u", getpid(), (unsigned long)address, (unsigned long)address + PAGE, order)
            : snprintf(cmd, sizeof(cmd), "%d,0x%lx,0x%lx,%u,%d", getpid(), (unsigned long)address, (unsigned long)address + PAGE, order, offset);
        need(n > 0 && (size_t)n < sizeof(cmd), "split command bound");
        ks_begin(3, address, HUGE); uint64_t start = now();
        ssize_t got = write(fd, cmd, (size_t)n); ns += now() - start; ks_end();
        need(got == n, "split write");
    }
    need(!close(fd), "split descriptor");
    rss_after += stat_field(a.data, "Rss:");
    if (!zeros) { verify_data(a); histogram(a); }
    after_huge += stat_field(a.data, "AnonHugePages:") / 2048;
    for (size_t i = 0; i < a.length / PAGE; i++) {
        if (zeros) { need(a.data[i * PAGE] == 0, "zero split contents"); if (pfns[i] == pfn(a.data + i * PAGE)) same_pfns++; }
        else { need(pfns[i] == pfn(a.data + i * PAGE), "split preserves physical pages"); same_pfns++; }
    }
    free(pfns); release(a); operations += units; valid++;
}
static void barrier(pthread_barrier_t *b)
{
    int ret = pthread_barrier_wait(b); need(!ret || ret == PTHREAD_BARRIER_SERIAL_THREAD, "barrier");
}
struct mover {
    unsigned id, units; int fd; unsigned char *src, *dst;
    pthread_barrier_t *start, *finish;
};
static void *move_worker(void *arg)
{
    struct mover *w = arg; cpu(2 + 2 * w->id);
    barrier(w->start);
    ks_begin(4, w->src, w->units * HUGE);
    for (unsigned i = 0; i < w->units; i++) {
        struct uffdio_move m = { .src = (uintptr_t)w->src + i * HUGE, .dst = (uintptr_t)w->dst + i * HUGE,
                                .len = HUGE, .mode = UFFDIO_MOVE_MODE_DONTWAKE };
        need(!ioctl(w->fd, UFFDIO_MOVE, &m) && m.move == HUGE, "UFFD move complete");
    }
    ks_end(); barrier(w->finish); return NULL;
}
static void uffd_register(int fd, struct area a)
{
    struct uffdio_register r = { .range = { .start = (uintptr_t)a.data, .len = a.length }, .mode = UFFDIO_REGISTER_MODE_MISSING };
    need(!ioctl(fd, UFFDIO_REGISTER, &r) && (r.ioctls & (1ULL << 0x05)), "UFFD move registration");
}
static void move_folios(unsigned units, unsigned threads, int independent)
{
    struct area src[4], dst[4], partsrc[4], partdst[4]; struct mover w[4]; pthread_t ids[4];
    unsigned groups = independent ? threads : 1;
    int fd = syscall(SYS_userfaultfd, O_CLOEXEC | O_NONBLOCK | UFFD_USER_MODE_ONLY);
    need(fd >= 0, "userfaultfd user-mode-only");
    struct uffdio_api api = { .api = UFFD_API, .features = UFFD_FEATURE_MOVE };
    need(!ioctl(fd, UFFDIO_API, &api) && (api.features & UFFD_FEATURE_MOVE), "UFFD move feature");
    for (unsigned g = 0; g < groups; g++) {
        src[g] = spaced(units / groups, independent ? 1 : threads);
        dst[g] = spaced(units / groups, independent ? 1 : threads);
    }
    for (unsigned i = 0; i < threads; i++) {
        unsigned g = independent ? i : 0; size_t off = independent ? 0 : i * (1UL << 30);
        partsrc[i] = (struct area){ .data = src[g].data + off, .length = units / threads * HUGE };
        partdst[i] = (struct area){ .data = dst[g].data + off, .length = units / threads * HUGE };
        /* Populate before MISSING registration; untouched virtual gaps stay empty. */
        populate(partsrc[i]); need(huge_count(partsrc[i]) == units / threads, "move huge folio precondition");
    }
    for (unsigned g = 0; g < groups; g++) {
        need(stat_field(src[g].data, "AnonHugePages:") == units / groups * 2048UL, "move PMD precondition");
        uffd_register(fd, src[g]); uffd_register(fd, dst[g]); before_huge += units / groups;
    }
    uint64_t *saved = calloc(units * 512UL, sizeof(*saved)); need(saved != NULL, "move PFNs");
    for (unsigned i = 0; i < threads; i++) for (size_t j = 0; j < partsrc[i].length / PAGE; j++)
        saved[i * (units / threads) * 512UL + j] = pfn(partsrc[i].data + j * PAGE);
    pthread_barrier_t start, finish; pthread_attr_t attr;
    need(!pthread_barrier_init(&start, NULL, threads + 1) && !pthread_barrier_init(&finish, NULL, threads + 1), "barrier init");
    need(!pthread_attr_init(&attr) && !pthread_attr_setstacksize(&attr, 128 * 1024), "thread attrs");
    for (unsigned i = 0; i < threads; i++) {
        w[i] = (struct mover){ i, units / threads, fd, partsrc[i].data, partdst[i].data, &start, &finish };
        need(!pthread_create(&ids[i], &attr, move_worker, &w[i]), "create mover");
    }
    uint64_t t = now(); barrier(&start); barrier(&finish); ns += now() - t;
    for (unsigned i = 0; i < threads; i++) need(!pthread_join(ids[i], NULL), "join mover");
    need(!pthread_attr_destroy(&attr) && !pthread_barrier_destroy(&start) && !pthread_barrier_destroy(&finish), "thread cleanup");
    for (unsigned g = 0; g < groups; g++) {
        after_huge += stat_field(dst[g].data, "AnonHugePages:") / 2048;
        need(stat_field(src[g].data, "Rss:") == 0, "source empty after move");
    }
    for (unsigned i = 0; i < threads; i++) {
        verify_data(partdst[i]);
        for (size_t j = 0; j < partsrc[i].length / PAGE; j++) {
            need(!(entry(partsrc[i].data + j * PAGE) >> 63), "source PTE absent");
            need(saved[i * (units / threads) * 512UL + j] == pfn(partdst[i].data + j * PAGE), "moved PFN retained"); same_pfns++;
        }
    }
    need(!close(fd), "close userfaultfd");
    for (unsigned g = 0; g < groups; g++) { release(src[g]); release(dst[g]); }
    free(saved); operations += units; valid++;
}
static void discard(unsigned units, unsigned kind)
{
    struct area a = allocate(units); populate(a);
    need(huge_count(a) == units, "discard THP precondition"); before_huge += units;
    if (kind == 1) for (unsigned i = 0; i < units; i++)
        need(!madvise(a.data + i * HUGE + 100 * PAGE, PAGE, MADV_DONTNEED), "first discard setup");
    ks_begin(6, a.data, a.length); uint64_t start = now();
    for (unsigned i = 0; i < units; i++) {
        size_t off = kind == 2 ? 0 : (kind == 1 ? 200 : 100) * PAGE;
        need(!madvise(a.data + i * HUGE + off, kind == 2 ? HUGE : PAGE, MADV_DONTNEED), "discard");
    }
    ns += now() - start; ks_end();
    after_huge += stat_field(a.data, "AnonHugePages:") / 2048;
    rss_after += stat_field(a.data, "Rss:");
    for (unsigned i = 0; i < units; i++) for (unsigned j = 0; j < 512; j++) {
        int lost = kind == 2 || j == 100 || (kind == 1 && j == 200);
        if (lost) { need(!(entry(a.data + i * HUGE + j * PAGE) >> 63), "discarded PTE absent"); dropped_pages++; }
        else { need(a.data[i * HUGE + j * PAGE] == value(i * 512 + j), "retained data"); kept_pages++; }
    }
    uint64_t t = now(); release(a); cleanup_ns += now() - t; operations += units; valid++;
}
static void truncate_files(unsigned units, int basepages)
{
    const char *dir = getenv("KS_HUGE_TMPDIR"); need(dir && dir[0] == '/', "private tmpfs directory");
    struct area a[32] = {0}; int fd[32];
    for (unsigned i = 0; i < units; i++) {
        char name[512]; need(snprintf(name, sizeof(name), "%s/folio-XXXXXX", dir) < (int)sizeof(name), "tmpfile path");
        fd[i] = mkstemp(name); need(fd[i] >= 0 && !unlink(name), "owned unlinked tmpfile");
        need(!ftruncate(fd[i], HUGE), "initial file length");
        a[i] = allocate(1);
        need(mmap(a[i].data, HUGE, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_FIXED, fd[i], 0) == a[i].data, "owned file mapping");
        need(!madvise(a[i].data, HUGE, basepages ? MADV_NOHUGEPAGE : MADV_HUGEPAGE), "file THP advice");
        populate(a[i]); need(huge_count(a[i]) == (basepages ? 0U : 1U), "file physical folio size");
        before_huge += huge_count(a[i]);
    }
    ks_begin(5, a[0].data, HUGE); uint64_t start = now();
    for (unsigned i = 0; i < units; i++) need(!ftruncate(fd[i], HUGE - PAGE), "partial ftruncate");
    ns += now() - start; ks_end();
    start = now();
    for (unsigned i = 0; i < units; i++) {
        struct stat st; unsigned char byte = 0;
        need(!fstat(fd[i], &st) && st.st_size == HUGE - PAGE, "truncated size");
        need(pread(fd[i], &byte, 1, HUGE - PAGE) == 0, "read EOF");
        for (size_t j = 0; j < 511; j++) need(a[i].data[j * PAGE] == value(j), "retained file bytes");
        kept_pages += 511; dropped_pages++;
    }
    post_ns += now() - start; start = now();
    for (unsigned i = 0; i < units; i++) { release(a[i]); need(!close(fd[i]), "close file"); }
    cleanup_ns += now() - start; operations += units; valid++;
}
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--selftest")) {
        need(HUGE == 2 * 1024 * 1024 && sizeof(struct uffdio_move) == 40, "ABI constants");
        puts("selftest=pass no_runtime_changes=1"); return 0;
    }
    need(argc == 5 && getenv("KS_HUGE_RUN") && !strcmp(getenv("KS_HUGE_RUN"), "1"), "explicit bounded execution");
    which = argv[1]; unsigned loops = strtoul(argv[2], NULL, 10), units = strtoul(argv[3], NULL, 10);
    timing = atoi(argv[4]);
    need(loops > 0 && loops <= 32 && units >= 4 && units <= 32 && !(units % 4) && (timing == 0 || timing == 1), "bounds");
    alarm(35); cpu(2);
    pm = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC); kf = open("/proc/kpageflags", O_RDONLY | O_CLOEXEC);
    need(pm >= 0 && kf >= 0 && sysconf(_SC_PAGESIZE) == PAGE, "page metadata access");
    struct area anchor = allocate(1);
    need(*(volatile unsigned char *)anchor.data == 0, "huge zero lifetime anchor");
    for (unsigned i = 0; i < loops; i++) {
        if (!strcmp(which, "zero_dense")) zero_write(units, 1, 0, 0);
        else if (!strcmp(which, "zero_sparse")) zero_write(units, 0, 0, 0);
        else if (!strcmp(which, "base_sparse")) zero_write(units, 0, 0, 1);
        else if (!strcmp(which, "direct_dense")) zero_write(units, 1, 1, 0);
        else if (!strcmp(which, "direct_sparse")) zero_write(units, 0, 1, 0);
        else if (!strcmp(which, "split_uniform")) split_folios(units, -1, 0, 0);
        else if (!strcmp(which, "split_order8")) split_folios(units, -1, 8, 0);
        else if (!strcmp(which, "split_zeros")) split_folios(units, -1, 0, 1);
        else if (!strcmp(which, "split_buddy_edge")) split_folios(units, 0, 0, 0);
        else if (!strcmp(which, "split_buddy_mid")) split_folios(units, 256, 0, 0);
        else if (!strcmp(which, "move_one")) move_folios(units, 1, 0);
        else if (!strcmp(which, "move_shared4")) move_folios(units, 4, 0);
        else if (!strcmp(which, "move_independent4")) move_folios(units, 4, 1);
        else if (!strcmp(which, "discard_first")) discard(units, 0);
        else if (!strcmp(which, "discard_second")) discard(units, 1);
        else if (!strcmp(which, "discard_whole")) discard(units, 2);
        else if (!strcmp(which, "truncate_thp")) truncate_files(units, 0);
        else if (!strcmp(which, "truncate_base")) truncate_files(units, 1);
        else need(0, "unknown case");
    }
    release(anchor); need(!close(pm) && !close(kf), "metadata cleanup");
    printf("RESULT case=%s loops=%u units=%u timing=%d cpu=%d valid=%"PRIu64" operations=%"PRIu64" ns=%"PRIu64
           " before_huge=%"PRIu64" after_huge=%"PRIu64" zero_ranges=%"PRIu64" rss_before_kb=%"PRIu64" rss_after_kb=%"PRIu64
           " minor_faults=%"PRIu64" same_pfns=%"PRIu64" cleanup=1", which, loops, units, timing, sched_getcpu(), valid, operations, ns,
           before_huge, after_huge, zero_ranges, rss_before, rss_after, minor_faults, same_pfns);
    for (unsigned order = 0; order < 10; order++) printf(" order%u=%"PRIu64, order, folios[order]);
    printf(" post_ns=%"PRIu64" cleanup_ns=%"PRIu64" kept_pages=%"PRIu64" dropped_pages=%"PRIu64, post_ns, cleanup_ns, kept_pages, dropped_pages);
    puts(""); return 0;
}
