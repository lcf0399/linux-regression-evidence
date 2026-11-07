#include "timing.h"
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/uio.h>
#include <unistd.h>

#define PAGE 4096UL
#define PR_SET_VMA 0x53564d41
#define PR_SET_VMA_ANON_NAME 0

static void check(int ok, const char *what)
{
    if (!ok) {
        fprintf(stderr, "semantic_failure: %s errno=%d\n", what, errno);
        exit(2);
    }
}

__attribute__((__noinline__)) void workload_begin(unsigned phase)
{
    ks_begin(phase);
    __asm__ volatile("" : : "r"(phase) : "memory");
}

__attribute__((__noinline__)) void workload_end(unsigned phase)
{
    ks_end(phase);
    __asm__ volatile("" : : "r"(phase) : "memory");
}

struct region { unsigned char *base, *data; size_t bytes, reserved; };

static struct region allocate(size_t bytes)
{
    struct region r = {.bytes = bytes, .reserved = bytes + 2*PAGE};
    r.base = mmap(NULL, r.reserved, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    check(r.base != MAP_FAILED, "reserve");
    r.data = r.base + PAGE;
    check(mprotect(r.data, bytes, PROT_READ|PROT_WRITE) == 0, "make owned range writable");
    check(madvise(r.data, bytes, MADV_NOHUGEPAGE) == 0, "disable THP for owned range");
    return r;
}

static void release(struct region r)
{
    check(munmap(r.base, r.reserved) == 0, "unmap complete reservation");
}

static size_t resident(unsigned char *p, size_t bytes)
{
    size_t pages = bytes/PAGE, count = 0;
    unsigned char *vec = calloc(pages, 1);
    check(vec != NULL && mincore(p, bytes, vec) == 0, "mincore");
    for (size_t i = 0; i < pages; i++) count += (vec[i]&1) != 0;
    free(vec);
    return count;
}

static void contents(const unsigned char *p, size_t bytes, unsigned char value)
{
    for (size_t i = 0; i < bytes; i++) check(p[i] == value, "data contents");
}

struct state { size_t vmas, writable, named, dd, rss_kb, lazy_kb, huge_kb; };

static struct state inspect(struct region r)
{
    struct state s = {0};
    FILE *file = fopen("/proc/self/smaps", "r");
    check(file != NULL, "smaps");
    char *line = NULL; size_t cap = 0; int selected = 0;
    while (getline(&line, &cap, file) >= 0) {
        unsigned long lo, hi, value; char perm[5];
        if (sscanf(line, "%lx-%lx %4s", &lo, &hi, perm) == 3) {
            selected = lo >= (uintptr_t)r.data && hi <= (uintptr_t)r.data+r.bytes;
            if (selected && perm[0] == 'r' && perm[1] == 'w') {
                s.vmas++; s.writable++;
                s.named += strstr(line, "[anon:arena]") != NULL;
            } else selected = 0;
        } else if (selected) {
            if (sscanf(line, "Rss: %lu kB", &value) == 1) s.rss_kb += value;
            if (sscanf(line, "LazyFree: %lu kB", &value) == 1) s.lazy_kb += value;
            if (sscanf(line, "AnonHugePages: %lu kB", &value) == 1) s.huge_kb += value;
            if (strncmp(line, "VmFlags:", 8) == 0 && strstr(line, " dd ")) s.dd++;
        }
    }
    check(!ferror(file), "smaps read"); free(line); fclose(file);
    return s;
}

static void lock_case(int cross)
{
    struct region r = allocate(32*PAGE);
    memset(r.data, 0x5a, r.bytes);
    check(madvise(r.data+16*PAGE, 16*PAGE, MADV_DONTFORK) == 0, "split flags");
    struct state before = inspect(r);
    check(before.vmas == 2 && before.huge_kb == 0, "two base-page VMAs");
    unsigned char *target = r.data + (cross ? 12 : 4)*PAGE;
    check(resident(target, 8*PAGE) == 8, "populated discard range");
    workload_begin(10);
    int rc = madvise(target, 8*PAGE, MADV_DONTNEED);
    workload_end(10);
    check(rc == 0, "DONTNEED return");
    check(resident(target, 8*PAGE) == 0, "discarded before zero reads");
    contents(target, 8*PAGE, 0);
    contents(r.data, (size_t)(target-r.data), 0x5a);
    contents(target+8*PAGE, r.bytes-(size_t)(target-r.data)-8*PAGE, 0x5a);
    check(inspect(r).vmas == 2, "unchanged target VMA topology");
    release(r);
    printf("{\"case\":\"lock-%s\",\"phase\":10,\"calls\":1,\"pages\":8,\"vmas\":2,\"content_pass\":true,\"unmapped\":true}\n", cross ? "cross" : "single");
}

static void free_case(int multi)
{
    struct region r = allocate(256*PAGE);
    struct region drain_control = allocate(PAGE);
    memset(drain_control.data, 0x19, drain_control.bytes);
    if (multi) for (size_t i = 1; i < 32; i += 2)
        check(madvise(r.data+i*8*PAGE, 8*PAGE, MADV_DONTFORK) == 0, "FREE VMA split");
    memset(r.data, 0x6b, r.bytes);
    struct state before = inspect(r);
    check(before.vmas == (multi ? 32UL : 1UL) && before.huge_kb == 0, "FREE geometry");
    check(resident(r.data, r.bytes) == 256, "FREE resident pages");
    workload_begin(20);
    int rc = madvise(r.data, r.bytes, MADV_FREE);
    workload_end(20);
    check(rc == 0, "FREE return");
    struct state immediate = inspect(r);
    /* Outside the operation window: COLD drains this CPU's pending LRU
     * batches before walking a separate, owned control page. The target is
     * not advised again. Report immediate and settled state separately. */
    check(madvise(drain_control.data, drain_control.bytes, MADV_COLD) == 0,
          "owned control drains pending LRU batch");
    struct state after = inspect(r);
    if (after.vmas != before.vmas || after.lazy_kb != r.bytes/1024)
        fprintf(stderr, "FREE_state immediate_lazy_kb=%zu settled_lazy_kb=%zu vmas=%zu\n",
                immediate.lazy_kb, after.lazy_kb, after.vmas);
    check(after.vmas == before.vmas && after.lazy_kb == r.bytes/1024, "FREE state before rewrite");
    check(resident(r.data, r.bytes) == 256, "lazyfree retained resident pages");
    contents(r.data, r.bytes, 0x6b);
    memset(r.data, 0x7c, r.bytes); contents(r.data, r.bytes, 0x7c);
    contents(drain_control.data, drain_control.bytes, 0x19);
    release(drain_control); release(r);
    printf("{\"case\":\"free-%s\",\"phase\":20,\"calls\":1,\"pages\":256,\"vmas\":%zu,\"lazy_kb\":%zu,\"immediate_lazy_kb\":%zu,\"settling\":\"separate-owned-page-COLD-after-window\",\"content_pass\":true,\"unmapped\":true}\n", multi ? "multi" : "single", before.vmas, after.lazy_kb, immediate.lazy_kb);
}

static void vector_case(size_t count)
{
    struct region r = allocate(256*PAGE);
    memset(r.data, 0x3d, r.bytes);
    struct iovec iov[64];
    for (size_t i = 0; i < count; i++) {
        iov[i].iov_base = r.data+i*r.bytes/count;
        iov[i].iov_len = r.bytes/count;
    }
    int fd = (int)syscall(SYS_pidfd_open, getpid(), 0);
    check(fd >= 0, "self pidfd");
    check(inspect(r).vmas == 1 && inspect(r).huge_kb == 0, "vector geometry");
    check(resident(r.data, r.bytes) == 256, "vector resident pages");
    workload_begin(30);
    long rc = syscall(SYS_process_madvise, fd, iov, count, MADV_COLD, 0);
    workload_end(30);
    check(rc == (long)r.bytes, "complete vector return");
    check(resident(r.data, r.bytes) == 256, "COLD retained residency");
    contents(r.data, r.bytes, 0x3d);
    check(close(fd) == 0, "close pidfd"); release(r);
    printf("{\"case\":\"vector-%zu\",\"phase\":30,\"calls\":1,\"iovecs\":%zu,\"pages\":256,\"processed_bytes\":%ld,\"content_pass\":true,\"unmapped\":true}\n", count, count, rc);
}

static void name_case(int named)
{
    const size_t arena = 16*PAGE, stride = arena+PAGE, n = 32;
    struct region r = allocate(n*stride);
    for (size_t i = 0; i < n; i++) {
        check(mprotect(r.data+i*stride+arena, PAGE, PROT_NONE) == 0, "arena separator");
        memset(r.data+i*stride, 0x4e, arena);
        check(madvise(r.data+i*stride, arena, MADV_DONTDUMP) == 0, "initial dump policy");
        if (named) check(prctl(PR_SET_VMA, PR_SET_VMA_ANON_NAME,
            (unsigned long)(r.data+i*stride), arena, (unsigned long)"arena") == 0, "name setup");
    }
    struct state before = inspect(r);
    check(before.vmas == n && before.dd == n && before.named == (named ? n : 0)
        && before.huge_kb == 0, "name and flag initial state");
    workload_begin(40);
    for (size_t pass = 0; pass < 32; pass++) for (size_t i = 0; i < n; i++)
        check(madvise(r.data+i*stride, arena, MADV_DONTDUMP) == 0, "no-op advice");
    workload_end(40);
    struct state after = inspect(r);
    check(memcmp(&before, &after, sizeof(before)) == 0, "unchanged names/flags/residency");
    for (size_t i = 0; i < n; i++) contents(r.data+i*stride, arena, 0x4e);
    release(r);
    printf("{\"case\":\"name-%d\",\"phase\":40,\"calls\":1024,\"arenas\":32,\"named\":%d,\"content_pass\":true,\"unmapped\":true}\n", named, named);
}

int measured_main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--selftest") == 0) {
        check(PAGE == 4096 && sizeof(struct iovec) == 16, "native ABI");
        puts("selftest=pass runtime_changes=0"); return 0;
    }
    check(argc == 2, "one declared case required");
    const char *allow = getenv("KS_ALLOW_MADVISE_PILOT");
    check(allow && strcmp(allow, "1") == 0, "explicit pilot execution required");
    check(sysconf(_SC_PAGESIZE) == (long)PAGE && sched_getcpu() == 2, "page/CPU identity");
    struct rlimit limit = {256*1024*1024, 256*1024*1024};
    check(setrlimit(RLIMIT_AS, &limit) == 0, "address-space bound");
    alarm(15);
    if (strcmp(argv[1], "lock-single") == 0) lock_case(0);
    else if (strcmp(argv[1], "lock-cross") == 0) lock_case(1);
    else if (strcmp(argv[1], "free-single") == 0) free_case(0);
    else if (strcmp(argv[1], "free-multi") == 0) free_case(1);
    else if (strcmp(argv[1], "vector-1") == 0) vector_case(1);
    else if (strcmp(argv[1], "vector-64") == 0) vector_case(64);
    else if (strcmp(argv[1], "name-0") == 0) name_case(0);
    else if (strcmp(argv[1], "name-1") == 0) name_case(1);
    else check(0, "unknown case");
    return 0;
}

#ifndef KS_NO_MAIN_WRAPPER
int main(int argc,char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--selftest"))return measured_main(argc,argv);
    ks_init();
    uint64_t child=ks_children_cpu(),cpu=ks_clock(CLOCK_PROCESS_CPUTIME_ID),wall=ks_clock(CLOCK_MONOTONIC_RAW);
    int rc=measured_main(argc,argv);
    wall=ks_clock(CLOCK_MONOTONIC_RAW)-wall;cpu=ks_clock(CLOCK_PROCESS_CPUTIME_ID)-cpu;
    child=ks_children_cpu()-child;
    if(rc)ks_fail("nonzero workload return");
    ks_emit(wall,cpu,child);return rc;
}
#endif
