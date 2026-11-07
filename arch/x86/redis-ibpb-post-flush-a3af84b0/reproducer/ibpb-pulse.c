#define _GNU_SOURCE
#include <errno.h>
#include <sched.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <unistd.h>

/* Opt IN to protection for this disposable process only. Never enable
 * speculation, touch an MSR, change a system setting or run a benchmark. */
int main(int argc, char **argv)
{
    char *end;
    cpu_set_t mask, actual;
    if (argc != 3 || (strcmp(argv[2], "sham") && strcmp(argv[2], "protect")) ||
        getuid() == 0)
        return 2;
    errno = 0;
    long cpu = strtol(argv[1], &end, 10);
    if (errno || *end || cpu < 0 || cpu >= 20)
        return 3;
    CPU_ZERO(&mask);
    CPU_SET(cpu, &mask);
    if (sched_setaffinity(0, sizeof(mask), &mask) ||
        sched_getaffinity(0, sizeof(actual), &actual) ||
        CPU_COUNT(&actual) != 1 || !CPU_ISSET(cpu, &actual) || sched_getcpu() != cpu)
        return 4;
    int before = prctl(PR_GET_SPECULATION_CTRL, PR_SPEC_INDIRECT_BRANCH, 0L, 0L, 0L);
    if (before != (PR_SPEC_PRCTL | PR_SPEC_ENABLE)) {
        fprintf(stderr, "unexpected initial speculation mask %d errno %d\n", before, errno);
        return 5;
    }
    int protect = !strcmp(argv[2], "protect");
    if (protect && prctl(PR_SET_SPECULATION_CTRL, PR_SPEC_INDIRECT_BRANCH,
                         PR_SPEC_FORCE_DISABLE, 0L, 0L)) {
        perror("protection opt-in");
        return 6;
    }
    int after = prctl(PR_GET_SPECULATION_CTRL, PR_SPEC_INDIRECT_BRANCH, 0L, 0L, 0L);
    if (after != (protect ? (int)(PR_SPEC_PRCTL | PR_SPEC_FORCE_DISABLE) : before) ||
        sched_getcpu() != cpu)
        return 7;
    printf("{\"cpu\":%ld,\"pid\":%ld,\"uid\":%ld,\"mode\":\"%s\","
           "\"before\":%d,\"after\":%d}\n",
           cpu, (long)getpid(), (long)getuid(), argv[2], before, after);
    return 0;
}
