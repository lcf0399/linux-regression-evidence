#define _GNU_SOURCE
/* Bounded extension of weighted_futex.c: fixed arithmetic work and an optional
 * same-CPU competitor. No scheduler policy/feature changes. */
#include <errno.h>
#include <inttypes.h>
#include <linux/futex.h>
#include <pthread.h>
#include <sched.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/time.h>
#include <time.h>
#include <unistd.h>

#define MULT UINT64_C(6364136223846793005)
#define ADD UINT64_C(1442695040888963407)
#define BG_BLOCK 4096U
_Static_assert(sizeof(_Atomic uint32_t) == 4 && ATOMIC_INT_LOCK_FREE == 2,
               "requires lock-free 32-bit futex words");

struct options { int cpu, competitor; unsigned int warmups, rounds, timeout_ms, work; uint64_t ops; };
struct stats { uint64_t completed, waits, wakes, woken, eagain, eintr, checksum;
               uint64_t max_gap, first_checkpoint, last_checkpoint; pid_t tid; };
struct shared {
    struct options opts;
    _Atomic uint32_t turn;
    _Atomic uint32_t phase; /* 0=not started, 1=timed work, 2=finished */
    pthread_barrier_t start, finish;
    struct stats stats[3];
    clockid_t clocks[3];
    uint64_t cpu_begin[3], cpu_end[3], begin, end, envelope_begin, envelope_end;
};
struct argument { struct shared *s; unsigned int index; };

static void fail(const char *message)
{
    fprintf(stderr, "weighted_payload: %s: %s\n", message, strerror(errno));
    exit(1);
}

static uint64_t number(const char *text, uint64_t min, uint64_t max)
{
    char *end;
    if (!*text || *text == '-' || *text == '+') fail("invalid number");
    errno = 0;
    uint64_t value = strtoull(text, &end, 10);
    if (errno || *end || value < min || value > max) fail("number out of range");
    return value;
}

static uint64_t clock_ns(clockid_t clock)
{
    struct timespec ts;
    if (clock_gettime(clock, &ts)) fail("clock_gettime");
    return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

static void barrier(pthread_barrier_t *bar)
{
    int ret = pthread_barrier_wait(bar);
    if (ret && ret != PTHREAD_BARRIER_SERIAL_THREAD) { errno = ret; fail("barrier"); }
}

static int nice_now(void)
{
    errno = 0;
    int n = getpriority(PRIO_PROCESS, 0);
    if (errno) fail("getpriority");
    return n;
}

static void state_check(struct shared *s)
{
    cpu_set_t mask;
    struct sched_param param;
    if (sched_getaffinity(0, sizeof(mask), &mask) || sched_getparam(0, &param)) fail("read policy");
    if (sched_getscheduler(0) != SCHED_OTHER || nice_now() != 0 || param.sched_priority ||
        CPU_COUNT(&mask) != 1 || !CPU_ISSET(s->opts.cpu, &mask) || sched_getcpu() != s->opts.cpu)
        fail("policy/nice/affinity mismatch");
}

__attribute__((noinline, noclone, used)) static void window_marker(unsigned int event)
{
    __asm__ volatile("" : : "r"(event) : "memory");
}

__attribute__((noinline, noclone)) static uint64_t compute(uint64_t x, unsigned int steps)
{
    for (unsigned int i = 0; i < steps; i++) {
        x = x * MULT + ADD;
        __asm__ volatile("" : "+r"(x)); /* Do not replace fixed work with a closed form. */
    }
    return x;
}

/* Independent logarithmic-time checksum, outside the measured sequence. */
static uint64_t expected(uint64_t seed, uint64_t steps)
{
    uint64_t a = MULT, c = ADD, ra = 1, rc = 0;
    while (steps) {
        if (steps & 1) { rc = rc * a + c; ra *= a; }
        c *= a + 1; a *= a; steps >>= 1;
    }
    return seed * ra + rc;
}

static void wait_turn(struct shared *s, struct stats *st, uint32_t wanted)
{
    for (;;) {
        uint32_t value = atomic_load_explicit(&s->turn, memory_order_acquire);
        if (value == wanted) return;
        st->waits++;
        long ret = syscall(SYS_futex, &s->turn, FUTEX_WAIT_PRIVATE, value, NULL, NULL, 0);
        if (!ret) continue;
        if (errno == EAGAIN) st->eagain++;
        else if (errno == EINTR) st->eintr++;
        else fail("FUTEX_WAIT_PRIVATE");
    }
}

static void pass_turn(struct shared *s, struct stats *st, uint32_t next)
{
    atomic_store_explicit(&s->turn, next, memory_order_release);
    long ret = syscall(SYS_futex, &s->turn, FUTEX_WAKE_PRIVATE, 1, NULL, NULL, 0);
    if (ret < 0 || ret > 1) fail("FUTEX_WAKE_PRIVATE");
    st->wakes++; st->woken += (uint64_t)ret;
}

static void foreground(struct shared *s, struct stats *st, unsigned int index)
{
    if (!index) {
        s->envelope_begin = clock_ns(CLOCK_MONOTONIC_RAW);
        for (int i = 0; i < 2 + s->opts.competitor; i++) s->cpu_begin[i] = clock_ns(s->clocks[i]);
        window_marker(1);
        s->begin = clock_ns(CLOCK_MONOTONIC_RAW);
        atomic_store_explicit(&s->phase, 1, memory_order_release);
        atomic_store_explicit(&s->turn, 0, memory_order_release);
    }
    for (uint64_t i = 0; i < s->opts.ops; i++) {
        wait_turn(s, st, index);
        if (s->opts.work) st->checksum = compute(st->checksum, s->opts.work);
        pass_turn(s, st, 1 - index);
        st->completed++;
    }
    if (!index) {
        wait_turn(s, st, 0);
        s->end = clock_ns(CLOCK_MONOTONIC_RAW);
        atomic_store_explicit(&s->phase, 2, memory_order_release);
        window_marker(2);
        for (int i = 0; i < 2 + s->opts.competitor; i++) s->cpu_end[i] = clock_ns(s->clocks[i]);
        s->envelope_end = clock_ns(CLOCK_MONOTONIC_RAW);
    }
}

static void background(struct shared *s, struct stats *st)
{
    uint32_t phase;
    do { phase = atomic_load_explicit(&s->phase, memory_order_acquire); }
    while (!phase);
    uint64_t last = clock_ns(CLOCK_MONOTONIC_RAW);
    st->first_checkpoint = last;
    while (phase == 1) {
        st->checksum = compute(st->checksum, BG_BLOCK);
        st->completed++;
        uint64_t now = clock_ns(CLOCK_MONOTONIC_RAW), gap = now - last;
        if (gap > st->max_gap) st->max_gap = gap;
        last = now;
        phase = atomic_load_explicit(&s->phase, memory_order_acquire);
    }
    st->last_checkpoint = last;
}

static void *worker(void *opaque)
{
    struct argument *arg = opaque;
    struct shared *s = arg->s;
    unsigned int index = arg->index;
    cpu_set_t mask;
    CPU_ZERO(&mask); CPU_SET(s->opts.cpu, &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) || setpriority(PRIO_PROCESS, 0, 0)) fail("set worker state");
    const char *names[] = {"sf-load-ping", "sf-load-pong", "sf-load-busy"};
    int ret = pthread_setname_np(pthread_self(), names[index]);
    if (ret) { errno = ret; fail("thread name"); }
    for (unsigned int round = 0; round < s->opts.warmups + s->opts.rounds; round++) {
        struct stats *st = &s->stats[index];
        *st = (struct stats){.tid = syscall(SYS_gettid), .checksum = index + 1};
        state_check(s);
        barrier(&s->start);
        if (index == 2) background(s, st); else foreground(s, st, index);
        barrier(&s->finish);
        state_check(s);
        barrier(&s->finish);
        barrier(&s->start);
    }
    return NULL;
}

static void report(struct shared *s, unsigned int round)
{
    struct stats *a = &s->stats[0], *b = &s->stats[1], *c = &s->stats[2];
    uint64_t elapsed = s->end - s->begin;
    if (s->end <= s->begin || atomic_load(&s->turn) != 0 || atomic_load(&s->phase) != 2 ||
        a->completed != s->opts.ops || b->completed != s->opts.ops ||
        a->wakes != s->opts.ops || b->wakes != s->opts.ops) fail("protocol mismatch");
    for (int i = 0; i < 2 + s->opts.competitor; i++) {
        uint64_t steps = i == 2 ? c->completed * BG_BLOCK : s->opts.ops * s->opts.work;
        if (s->stats[i].checksum != expected(i + 1, steps)) fail("arithmetic checksum mismatch");
        if (s->cpu_end[i] < s->cpu_begin[i]) fail("nonmonotonic CPU clock");
    }
    uint64_t extra = (s->begin - s->envelope_begin) + (s->envelope_end - s->end);
    printf("{\"schema\":\"sched-fair-payload-v1\",\"phase\":\"%s\",\"round\":%u,"
           "\"cpu\":%d,\"nice\":0,\"work_iterations\":%u,\"competitor\":%d,\"roundtrips\":%" PRIu64 ","
           "\"success\":[%" PRIu64 ",%" PRIu64 "],\"elapsed_ns\":%" PRIu64 ",\"roundtrip_ns_mean\":%.6f,"
           "\"wait_calls\":[%" PRIu64 ",%" PRIu64 "],\"wake_calls\":[%" PRIu64 ",%" PRIu64 "],"
           "\"woken\":[%" PRIu64 ",%" PRIu64 "],\"eagain\":[%" PRIu64 ",%" PRIu64 "],\"eintr\":[%" PRIu64 ",%" PRIu64 "],"
           "\"checksums\":[%" PRIu64 ",%" PRIu64 ",%" PRIu64 "],\"cpu_ns\":[%" PRIu64 ",%" PRIu64 ",%" PRIu64 "],"
           "\"cpu_clock_envelope_extra_ns\":%" PRIu64 ",\"busy_blocks\":%" PRIu64 ",\"busy_block_iterations\":%u,"
           "\"busy_max_checkpoint_gap_ns\":%" PRIu64 ",\"busy_first_checkpoint_ns\":%" PRIu64 ",\"busy_last_checkpoint_ns\":%" PRIu64 ","
           "\"begin_ns\":%" PRIu64 ",\"end_ns\":%" PRIu64 ",\"tids\":[%d,%d,%d],\"state_checks\":\"pass\"}\n",
           round < s->opts.warmups ? "warmup" : "measured", round, s->opts.cpu, s->opts.work, s->opts.competitor,
           s->opts.ops, a->completed, b->completed, elapsed, (double)elapsed / s->opts.ops,
           a->waits,b->waits,a->wakes,b->wakes,a->woken,b->woken,a->eagain,b->eagain,a->eintr,b->eintr,
           a->checksum,b->checksum,c->checksum,s->cpu_end[0]-s->cpu_begin[0],s->cpu_end[1]-s->cpu_begin[1],
           s->opts.competitor ? s->cpu_end[2]-s->cpu_begin[2] : 0, extra, c->completed, BG_BLOCK,
           c->max_gap,c->first_checkpoint,c->last_checkpoint,s->begin,s->end,a->tid,b->tid,c->tid);
    if (fflush(stdout)) fail("write result");
}

static void timeout_handler(int sig)
{
    (void)sig;
    static const char text[] = "weighted_payload: watchdog timeout\n";
    ssize_t ret = write(2, text, sizeof(text)-1); (void)ret;
    _exit(124);
}

int main(int argc, char **argv)
{
    struct shared s = {.opts = {.cpu = -1, .competitor = -1, .ops = 100000,
                      .warmups = 1, .rounds = 3, .timeout_ms = 120000}};
    for (int i = 1; i < argc; i += 2) {
        if (!strcmp(argv[i], "--help")) {
            puts("weighted_payload --cpu N --competitor 0|1 --work-iterations N [--ops N --warmups N --rounds N --timeout-ms N]");
            return 0;
        }
        if (i+1 == argc) fail("missing value");
        const char *v = argv[i+1];
        if (!strcmp(argv[i], "--cpu")) s.opts.cpu = number(v,0,CPU_SETSIZE-1);
        else if (!strcmp(argv[i], "--competitor")) s.opts.competitor = number(v,0,1);
        else if (!strcmp(argv[i], "--work-iterations")) s.opts.work = number(v,0,1000000);
        else if (!strcmp(argv[i], "--ops")) s.opts.ops = number(v,1,10000000);
        else if (!strcmp(argv[i], "--warmups")) s.opts.warmups = number(v,0,100);
        else if (!strcmp(argv[i], "--rounds")) s.opts.rounds = number(v,1,100);
        else if (!strcmp(argv[i], "--timeout-ms")) s.opts.timeout_ms = number(v,1,600000);
        else fail("unknown option");
    }
    cpu_set_t mask;
    if (sched_getaffinity(0,sizeof(mask),&mask)) fail("read affinity");
    if (s.opts.cpu < 0 || s.opts.competitor < 0 || !CPU_ISSET(s.opts.cpu,&mask) ||
        nice_now() != 0 || sched_getscheduler(0) != SCHED_OTHER) fail("explicit allowed CPU/competitor and normal parent required");
    struct sigaction action = {.sa_handler = timeout_handler};
    if (sigemptyset(&action.sa_mask) || sigaction(SIGALRM,&action,NULL)) fail("watchdog handler");
    struct itimerval timer = {.it_value = {.tv_sec = s.opts.timeout_ms/1000, .tv_usec = (s.opts.timeout_ms%1000)*1000}};
    if (setitimer(ITIMER_REAL,&timer,NULL)) fail("watchdog timer");
    int count = 2 + s.opts.competitor;
    int ret = pthread_barrier_init(&s.start,NULL,count+1);
    if (!ret) ret = pthread_barrier_init(&s.finish,NULL,count+1);
    if (ret) { errno=ret; fail("barriers"); }
    pthread_t threads[3]; struct argument args[3];
    atomic_init(&s.turn,2); atomic_init(&s.phase,0);
    for (int i=0; i<count; i++) {
        args[i] = (struct argument){.s=&s,.index=i};
        ret = pthread_create(&threads[i],NULL,worker,&args[i]);
        if (!ret) ret = pthread_getcpuclockid(threads[i],&s.clocks[i]);
        if (ret) { errno=ret; fail("create thread/clock"); }
    }
    for (unsigned int round=0; round<s.opts.warmups+s.opts.rounds; round++) {
        atomic_store(&s.turn,2); atomic_store(&s.phase,0);
        barrier(&s.start); barrier(&s.finish); barrier(&s.finish);
        report(&s,round); barrier(&s.start);
    }
    for (int i=0; i<count; i++) { ret=pthread_join(threads[i],NULL); if(ret) { errno=ret; fail("join"); } }
    timer=(struct itimerval){0}; if(setitimer(ITIMER_REAL,&timer,NULL)) fail("stop timer");
    if (nice_now()!=0) fail("parent nice changed");
    pthread_barrier_destroy(&s.start); pthread_barrier_destroy(&s.finish);
    return 0;
}
