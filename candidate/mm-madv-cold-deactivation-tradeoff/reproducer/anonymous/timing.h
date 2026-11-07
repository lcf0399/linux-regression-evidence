#ifndef KS_TIMING_H
#define KS_TIMING_H
#define _GNU_SOURCE
#include <errno.h>
#include <inttypes.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* One owner per row; shared mapping also carries a waited child's rows.
 * Histogram buckets are [2^i,2^(i+1)-1] ns, not exact tail percentiles.
 * Timestamps, counters and bookkeeping have no access to kernel structures. */
#define KS_ROWS 96
#define KS_PHASES 128
struct ks_row {
    long tid;
    unsigned phase, cpu;
    uint64_t count, cpu_ns, wall_ns, min_wall_ns, max_wall_ns, hist[64];
};
struct ks_shared { atomic_uint used; struct ks_row rows[KS_ROWS]; };
static struct ks_shared *ks_shared;
static _Thread_local struct ks_row *ks_cache[KS_PHASES], *ks_active;
static _Thread_local uint64_t ks_wall_start,ks_cpu_start;

static void ks_fail(const char *what)
{ fprintf(stderr,"timing_failure: %s errno=%d\n",what,errno);_exit(3); }
static uint64_t ks_clock(clockid_t id)
{
    struct timespec t;
    if(clock_gettime(id,&t))ks_fail("clock_gettime");
    return (uint64_t)t.tv_sec*1000000000ULL+(uint64_t)t.tv_nsec;
}
static uint64_t ks_children_cpu(void)
{
    struct rusage r;if(getrusage(RUSAGE_CHILDREN,&r))ks_fail("child usage");
    return ((uint64_t)r.ru_utime.tv_sec+r.ru_stime.tv_sec)*1000000000ULL+
        ((uint64_t)r.ru_utime.tv_usec+r.ru_stime.tv_usec)*1000ULL;
}
static void ks_begin(unsigned phase)
{
    if(phase==90)return; /* Existing writer-lifetime marker includes pacing. */
    int saved=errno;
    if(!ks_shared || !phase || phase>=KS_PHASES || ks_active)ks_fail("nested or unknown phase");
    long tid=syscall(SYS_gettid);
    struct ks_row *r=ks_cache[phase];
    if(!r || r->tid!=tid){
        unsigned slot=atomic_fetch_add_explicit(&ks_shared->used,1,memory_order_relaxed);
        if(slot>=KS_ROWS)ks_fail("bounded record capacity");
        r=&ks_shared->rows[slot];r->tid=tid;r->phase=phase;r->cpu=(unsigned)sched_getcpu();
        r->min_wall_ns=UINT64_MAX;ks_cache[phase]=r;
    }
    ks_active=r;
    ks_wall_start=ks_clock(CLOCK_MONOTONIC_RAW);
    ks_cpu_start=ks_clock(CLOCK_THREAD_CPUTIME_ID);
    errno=saved;
}
static void ks_end(unsigned phase)
{
    if(phase==90)return;
    int saved=errno;
    uint64_t cpu=ks_clock(CLOCK_THREAD_CPUTIME_ID),wall=ks_clock(CLOCK_MONOTONIC_RAW);
    struct ks_row *r=ks_active;
    if(!r || r->phase!=phase || cpu<ks_cpu_start || wall<=ks_wall_start)ks_fail("phase/clock mismatch");
    cpu-=ks_cpu_start;wall-=ks_wall_start;
    r->count++;r->cpu_ns+=cpu;r->wall_ns+=wall;
    if(wall<r->min_wall_ns)r->min_wall_ns=wall;
    if(wall>r->max_wall_ns)r->max_wall_ns=wall;
    r->hist[63-__builtin_clzll(wall)]++;
    ks_active=NULL;errno=saved;
}
static void ks_init(void)
{
    ks_shared=mmap(NULL,sizeof(*ks_shared),PROT_READ|PROT_WRITE,MAP_SHARED|MAP_ANONYMOUS,-1,0);
    if(ks_shared==MAP_FAILED)ks_fail("shared measurement rows");
    memset(ks_shared,0,sizeof(*ks_shared));atomic_init(&ks_shared->used,0);
    /* Common empty-window cost, kept separate and never subtracted. */
    for(unsigned i=0;i<256;i++){ks_begin(1);ks_end(1);}
}
static void ks_emit(uint64_t wall,uint64_t cpu,uint64_t child)
{
    if(ks_active)ks_fail("unfinished timing scope");
    unsigned used=atomic_load_explicit(&ks_shared->used,memory_order_relaxed);
    if(used>KS_ROWS)ks_fail("row count");
    printf("KS_TIMING {\"schema\":\"madvise-clean-timing-v1\",\"main_wall_ns\":%" PRIu64
           ",\"main_process_cpu_ns\":%" PRIu64 ",\"waited_children_cpu_ns\":%" PRIu64 ",\"rows\":[",wall,cpu,child);
    for(unsigned i=0;i<used;i++){
        struct ks_row *r=&ks_shared->rows[i];
        printf("%s{\"tid\":%ld,\"phase\":%u,\"cpu\":%u,\"count\":%" PRIu64
               ",\"cpu_ns\":%" PRIu64 ",\"wall_ns\":%" PRIu64 ",\"min_wall_ns\":%" PRIu64
               ",\"max_wall_ns\":%" PRIu64 ",\"wall_hist\":[",i?",":"",r->tid,r->phase,r->cpu,r->count,r->cpu_ns,r->wall_ns,r->min_wall_ns,r->max_wall_ns);
        for(unsigned j=0;j<64;j++)printf("%s%" PRIu64,j?",":"",r->hist[j]);
        printf("]}");
    }
    printf("]}\n");
    if(munmap(ks_shared,sizeof(*ks_shared)))ks_fail("measurement rows cleanup");
}
#endif
