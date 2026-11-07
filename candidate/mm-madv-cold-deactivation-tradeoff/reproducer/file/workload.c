#define main prior_pilot_main
#include "madvise_pilot.c"
#undef main
#include <time.h>
#include <inttypes.h>

/* Same preparation as the admitted private-reclaim program. Clocks surround
 * the COLD calls, not state queries or content verification. */
__attribute__((__noinline__)) void scope_begin(unsigned phase)
{ __asm__ volatile("" : : "r"(phase) : "memory"); }
__attribute__((__noinline__)) void scope_end(unsigned phase)
{ __asm__ volatile("" : : "r"(phase) : "memory"); }

struct clocks { uint64_t cpu, wall; };
static struct clocks elapsed[32];
static uint64_t now(clockid_t id)
{
    struct timespec t;
    check(clock_gettime(id, &t) == 0, "clock");
    return (uint64_t)t.tv_sec*1000000000ULL+t.tv_nsec;
}
static struct clocks start(void)
{
    return (struct clocks){.wall=now(CLOCK_MONOTONIC_RAW),
                           .cpu=now(CLOCK_THREAD_CPUTIME_ID)};
}
static struct clocks stop(struct clocks a)
{
    uint64_t cpu=now(CLOCK_THREAD_CPUTIME_ID), wall=now(CLOCK_MONOTONIC_RAW);
    check(cpu >= a.cpu && wall >= a.wall, "clock monotonicity");
    return (struct clocks){.cpu=cpu-a.cpu, .wall=wall-a.wall};
}
static void cold(void *p, size_t bytes, unsigned phase)
{
    check(phase < 32, "phase");
    struct clocks a=start();
    scope_begin(phase);
    int rc=madvise(p, bytes, MADV_COLD);
    scope_end(phase);
    elapsed[phase]=stop(a);
    check(rc == 0, "COLD");
}
static int cg_write(const char *file, const char *value)
{
    const char *cg=getenv("KS_OWNED_CGROUP");
    char path[512];
    const char *prefix="/sys/fs/cgroup/ks-cold-private-20261020-";
    check(cg && !strncmp(cg, prefix, strlen(prefix)), "private cgroup path");
    check(snprintf(path, sizeof(path), "%s/%s", cg, file) < (int)sizeof(path),
          "cgroup filename");
    int fd=open(path, O_WRONLY|O_CLOEXEC);
    check(fd >= 0, "open cgroup file");
    ssize_t rc=write(fd, value, strlen(value));
    int saved=rc == (ssize_t)strlen(value) ? 0 : errno;
    check(close(fd) == 0, "close cgroup file");
    return saved;
}
static struct region file_region(const char *dir, int *fd)
{
    const size_t bytes=16UL*1024*1024, align=2UL*1024*1024;
    struct region r={.bytes=bytes, .reserved=bytes+2*align};
    r.base=mmap(NULL, r.reserved, PROT_NONE, MAP_PRIVATE|MAP_ANONYMOUS, -1, 0);
    check(r.base != MAP_FAILED, "aligned reservation");
    r.data=(unsigned char *)(((uintptr_t)r.base+align)&~(align-1));
    char path[512];
    check(dir && snprintf(path, sizeof(path), "%s/cold-XXXXXX", dir) < (int)sizeof(path),
          "owned file directory");
    *fd=mkstemp(path); check(*fd >= 0, "file creation");
    check(unlink(path) == 0 && ftruncate(*fd, bytes) == 0, "unlinked sized file");
    check(mmap(r.data, bytes, PROT_READ|PROT_WRITE, MAP_SHARED|MAP_FIXED, *fd, 0)
          == r.data, "file mapping");
    check(madvise(r.data, bytes, MADV_NOHUGEPAGE) == 0, "base-page policy");
    memset(r.data, 0x3d, bytes);
    check(fdatasync(*fd) == 0, "writeback completed");
    contents(r.data, bytes, 0x3d);
    return r;
}
int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--selftest")) {
        check(PAGE == 4096 && MADV_COLD == 20, "constants");
        struct clocks a=start(), delta=stop(a);
        check(delta.cpu < 1000000000ULL && delta.wall < 1000000000ULL, "clocks");
        puts("selftest=pass runtime_changes=0"); return 0;
    }
    struct clocks whole=start();
    check(argc == 2 && !strcmp(argv[1], "cold-file-private-reclaim")
          && getenv("KS_ALLOW_COLD_FILE") && !strcmp(getenv("KS_ALLOW_COLD_FILE"), "1"),
          "declared launch");
    check(getuid() == 1000 && geteuid() == 1000 && sched_getcpu() == 2
          && sysconf(_SC_PAGESIZE) == PAGE, "ordinary user and CPU");
    struct rlimit lim={256UL*1024*1024, 256UL*1024*1024}, core={0, 0};
    check(setrlimit(RLIMIT_AS, &lim) == 0 && setrlimit(RLIMIT_CORE, &core) == 0,
          "bounded process");
    alarm(25);
    const char *cg=getenv("KS_OWNED_CGROUP");
    char expected[512], line[512];
    check(cg && !strncmp(cg, "/sys/fs/cgroup/", strlen("/sys/fs/cgroup/"))
          && snprintf(expected, sizeof(expected), "0::%s\n",
          cg+strlen("/sys/fs/cgroup")) < (int)sizeof(expected), "declared group");
    FILE *membership=fopen("/proc/self/cgroup", "r");
    check(membership && fgets(line, sizeof(line), membership)
          && !strcmp(line, expected) && !fgets(line, sizeof(line), membership),
          "launcher joined only the declared private group");
    check(fclose(membership) == 0, "membership check");
    int aux_fd, target_fd;
    struct region aux=file_region(getenv("KS_OWNED_FILE_DIR"), &aux_fd);
    struct region target=file_region(getenv("KS_OWNED_FILE_DIR"), &target_fd);
    struct region control=allocate(PAGE); memset(control.data, 0x19, PAGE);
    cold(control.data, PAGE, 9);
    cold(aux.data, aux.bytes, 6); cold(control.data, PAGE, 7);
    cold(target.data, target.bytes, 10); cold(control.data, PAGE, 11);
    check(resident(target.data, target.bytes) == 4096 && resident(aux.data, aux.bytes) == 4096,
          "initial complete residency");
    scope_begin(20);
    int reclaim_error=cg_write("memory.reclaim", "4096");
    scope_end(20);
    size_t kept=resident(target.data, target.bytes), aux_kept=resident(aux.data, aux.bytes);
    struct state s=inspect(target);
    int ready=!reclaim_error && kept == 4096 && s.rss_kb == 16384
              && s.vmas == 1 && s.huge_kb == 0;
    if (ready) {
        cold(target.data, target.bytes, 30); cold(control.data, PAGE, 31);
        check(resident(target.data, target.bytes) == 4096 && inspect(target).rss_kb == 16384,
              "retained target after COLD");
        contents(target.data, target.bytes, 0x3d);
    }
    contents(aux.data, aux.bytes, 0x3d); contents(control.data, PAGE, 0x19);
    release(aux); release(target); release(control);
    check(close(aux_fd) == 0 && close(target_fd) == 0, "close owned files");
    whole=stop(whole);
    printf("{\"schema\":\"cold-file-private-reclaim-timing-v1\","
           "\"case\":\"cold-file-private-reclaim\",\"bytes\":16777216,"
           "\"reclaim_requests\":1,\"reclaim_bytes\":4096,\"reclaim_errno\":%d,"
           "\"target_resident\":%zu,\"target_rss_kb\":%zu,\"aux_resident\":%zu,"
           "\"target_executed\":%s,\"content_pass\":%s,\"cleanup\":true,\"timing\":true,"
           "\"whole_worker_cpu_ns\":%" PRIu64 ",\"whole_worker_wall_ns\":%" PRIu64 ","
           "\"phases\":[", reclaim_error, kept, s.rss_kb, aux_kept,
           ready ? "true" : "false", ready ? "true" : "false", whole.cpu, whole.wall);
    const unsigned phases[]={10, 11, 30, 31};
    for (size_t i=0; i<sizeof(phases)/sizeof(phases[0]); i++) {
        unsigned phase=phases[i];
        printf("%s{\"phase\":%u,\"cpu_ns\":%" PRIu64 ",\"wall_ns\":%" PRIu64 "}",
               i ? "," : "", phase, elapsed[phase].cpu, elapsed[phase].wall);
    }
    puts("]}");
    return 0;
}
