/* Original lazy_ptes operation order, with explicit whole-lifecycle clocks.
 * base_probe.c is the pinned original helper source, not a kernel change.
 * Keep diagnostic reads separate from the useful-operation subtotal so that
 * procfs/PFN checking cannot silently dilute the apparent cost of PAGEOUT.
 */
#define main inherited_main
#include "base_probe.c"
#undef main

static uint64_t whole_ns, work_ns, checks_ns, prepare_ns, reuse_ns;

static void whole_lazy_ptes(unsigned units)
{
    uint64_t begin = now();
    struct area a = allocate(units);
    populate(a);
    uint64_t prepared = now();
    need(huge_count(a) == units, "lazy THP precondition");
    before_huge += units;
    uint64_t checked_before = now();
    ks_begin(10, a.data, a.length);
    need(!madvise(a.data, a.length, MADV_FREE), "lazyfree mark");
    ks_end();
    for (unsigned i = 0; i < units; i++)
        *(volatile unsigned char *)(a.data + i * HUGE) = 0x7e;
    /* Original order: rewrite first, then split only the PMD mapping. */
    for (unsigned i = 0; i < units; i++) {
        need(!mprotect(a.data + i * HUGE + PAGE, PAGE, PROT_READ), "PTE control split");
        need(!mprotect(a.data + i * HUGE + PAGE, PAGE, PROT_READ | PROT_WRITE), "PTE control restore");
    }
    uint64_t reused = now();
    need(stat_field(a.data, "AnonHugePages:") == 0, "PTE mapping control");
    ks_begin(11, a.data, a.length);
    uint64_t start = now();
    need(!madvise(a.data, a.length, MADV_PAGEOUT), "bounded own lazy pageout");
    uint64_t finish = now();
    ks_end();
    after_huge += stat_field(a.data, "AnonHugePages:") / 2048;
    uint64_t rss = stat_field(a.data, "Rss:");
    rss_after += rss;
    need(rss == units * 2048UL, "redirty retained resident shape");
    need(stat_field(a.data, "Swap:") == 0, "no swapout in original case");
    for (unsigned i = 0; i < units; i++) {
        need(a.data[i * HUGE] == 0x7e, "redirty sentinel retained");
        kept_pages++;
    }
    uint64_t verified = now();
    release(a);
    uint64_t end = now();
    uint64_t local_prepare = prepared - begin;
    uint64_t local_reuse = reused - checked_before;
    uint64_t local_pageout = finish - start;
    uint64_t local_cleanup = end - verified;
    uint64_t local_work = local_prepare + local_reuse + local_pageout + local_cleanup;
    uint64_t local_whole = end - begin;
    need(local_work <= local_whole, "nested timing intervals");
    prepare_ns += local_prepare;
    reuse_ns += local_reuse;
    ns += local_pageout;
    cleanup_ns += local_cleanup;
    work_ns += local_work;
    whole_ns += local_whole;
    checks_ns += local_whole - local_work;
    operations += units;
    valid++;
}

int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--selftest"))
        return inherited_main(argc, argv);
    need(argc == 5 && getenv("KS_HUGE_RUN") && !strcmp(getenv("KS_HUGE_RUN"), "1"),
         "explicit bounded run");
    which = argv[1];
    unsigned loops = strtoul(argv[2], NULL, 10), units = strtoul(argv[3], NULL, 10);
    timing = atoi(argv[4]);
    need(!strcmp(which, "lazy_ptes") && loops > 0 && loops <= 4 &&
         units >= 4 && units <= 16 && !(units % 4) &&
         (timing == 0 || timing == 1), "original case and bounds");
    alarm(35);
    cpu(2);
    pm = open("/proc/self/pagemap", O_RDONLY | O_CLOEXEC);
    kf = open("/proc/kpageflags", O_RDONLY | O_CLOEXEC);
    need(pm >= 0 && kf >= 0 && sysconf(_SC_PAGESIZE) == PAGE, "own metadata");
    for (unsigned i = 0; i < loops; i++) whole_lazy_ptes(units);
    need(!close(pm) && !close(kf), "metadata close");
    need(work_ns == prepare_ns + reuse_ns + ns + cleanup_ns &&
         whole_ns == work_ns + checks_ns, "complete accounting");
    printf("RESULT case=%s loops=%u units=%u timing=%d cpu=%d valid=%" PRIu64
           " operations=%" PRIu64 " ns=%" PRIu64 " before_huge=%" PRIu64
           " after_huge=%" PRIu64 " rss_after_kb=%" PRIu64 " cleanup_ns=%" PRIu64
           " kept_pages=%" PRIu64 " dropped_pages=0 reclaim_ok=0 reclaim_eagain=0 post_ns=0"
           " whole_ns=%" PRIu64 " work_ns=%" PRIu64 " checks_ns=%" PRIu64
           " prepare_ns=%" PRIu64 " reuse_ns=%" PRIu64 " cleanup=1\n",
           which, loops, units, timing, sched_getcpu(), valid, operations, ns,
           before_huge, after_huge, rss_after, cleanup_ns, kept_pages,
           whole_ns, work_ns, checks_ns, prepare_ns, reuse_ns);
    return 0;
}
