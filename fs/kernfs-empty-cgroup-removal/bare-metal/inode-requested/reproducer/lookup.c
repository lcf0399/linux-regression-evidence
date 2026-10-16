/* SPDX-License-Identifier: GPL-2.0-only */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

/* Auxiliary first-touch and cached lookup, not the original removal benchmark. */
static const char *names[] = {"cgroup.events", "cgroup.procs", "cgroup.threads", "cgroup.stat"};
struct row { uint64_t first, cached, ino, dirino, before, after; };
static uint64_t now(void)
{
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC_RAW, &t)) abort();
    return (uint64_t)t.tv_sec * 1000000000ULL + t.tv_nsec;
}
static int mark(int fd, const char *side, int op)
{
    char line[100];
    int n = snprintf(line, sizeof(line), "CG %s actor=%d op=%d family=1\n", side, getpid(), op);
    return fd < 0 || write(fd, line, n) == n ? 0 : -1;
}
int main(int argc, char **argv)
{
    if (argc != 5) return 2;
    int rounds = atoi(argv[1]), warmup = atoi(argv[2]), repeats = atoi(argv[3]);
    const char *phase = argv[4];
    bool trace = strcmp(phase, "none") != 0;
    if (rounds < 1 || rounds > 128 || warmup < 0 || warmup > 16 || repeats < 1 || repeats > 1024 ||
        (trace && (rounds != 4 || warmup || repeats != 1)) ||
        (strcmp(phase, "none") && strcmp(phase, "first") && strcmp(phase, "cached") && strcmp(phase, "remove"))) return 2;
    alarm(45);
    cpu_set_t cpus;
    CPU_ZERO(&cpus); CPU_SET(0, &cpus);
    if (sched_setaffinity(0, sizeof(cpus), &cpus) || sched_getscheduler(0) != SCHED_OTHER) return 1;
    char state[32], root[160], leaf[200], path[256];
    FILE *f = fopen("/sys/kernel/sched_ext/state", "r");
    if (!f) return 1;
    int scanned = fscanf(f, "%31s", state); fclose(f);
    if (scanned != 1 || strcmp(state, "disabled")) return 1;
    snprintf(root, sizeof(root), "/sys/fs/cgroup/kernel-study-lookup-%d", getpid());
    snprintf(leaf, sizeof(leaf), "%s/leaf", root);
    if (mkdir(root, 0755)) return 1;
    fprintf(stderr, "LOOKUP_ROOT=%s\n", root);
    int ret = 1, held = -1, marker = -1;
    bool exists = false;
    struct row rows[144] = {0};
    snprintf(path, sizeof(path), "%s/cgroup.subtree_control", root);
    int control = open(path, O_WRONLY | O_CLOEXEC);
    if (control < 0) goto out;
    ssize_t written = write(control, "+cpu", 4);
    close(control);
    if (written != 4) goto out;
    if (trace) {
        marker = open("/sys/kernel/tracing/trace_marker", O_WRONLY | O_CLOEXEC);
        if (marker < 0) goto out;
    }
    for (int i = 0; i < rounds + warmup; i++) {
        struct stat st;
        struct row *r = &rows[i];
        if (mkdir(leaf, 0755)) goto out;
        exists = true;
        /* Instantiate the parent outside the file-lookup measurement. */
        if (stat(leaf, &st) || !S_ISDIR(st.st_mode) || !st.st_nlink) goto out;
        r->dirino = st.st_ino;
        snprintf(path, sizeof(path), "%s/%s", leaf, names[i % 4]);
        int m = !strcmp(phase, "first") ? marker : -1;
        if (mark(m, "BEGIN", i + 1)) goto out;
        uint64_t begin = now();
        int rc = stat(path, &st);
        r->first = now() - begin;
        if (mark(m, "END", i + 1) || rc || !S_ISREG(st.st_mode) || st.st_nlink != 1) goto out;
        r->ino = st.st_ino;
        m = !strcmp(phase, "cached") ? marker : -1;
        if (mark(m, "BEGIN", i + 1)) goto out;
        begin = now();
        for (int j = 0; j < repeats; j++) {
            if (stat(path, &st) || st.st_ino != r->ino || st.st_nlink != 1) goto out;
        }
        r->cached = now() - begin;
        if (mark(m, "END", i + 1)) goto out;
        held = open(path, O_RDONLY | O_CLOEXEC);
        if (held < 0 || fstat(held, &st) || st.st_ino != r->ino || st.st_nlink != 1) goto out;
        r->before = st.st_nlink;
        m = !strcmp(phase, "remove") ? marker : -1;
        if (mark(m, "BEGIN", i + 1)) goto out;
        rc = rmdir(leaf);
        if (rc) goto out;
        exists = false;
        if (mark(m, "END", i + 1) || fstat(held, &st) || st.st_ino != r->ino || st.st_nlink != 0) goto out;
        r->after = st.st_nlink;
        close(held); held = -1;
        errno = 0;
        if (stat(leaf, &st) != -1 || errno != ENOENT) goto out;
    }
    ret = 0;
out:
    if (ret) fprintf(stderr, "LOOKUP_FAIL errno=%d\n", errno);
    if (held >= 0) close(held);
    if (marker >= 0) close(marker);
    if (exists && rmdir(leaf)) ret = 1;
    if (rmdir(root)) ret = 1;
    if (ret) return ret;
    printf("{\"schema\":\"kernfs-lookup-v1\",\"family\":1,\"actor\":%d,\"root\":\"%s\",\"trace\":%s,\"phase\":\"%s\",\"rounds\":%d,\"warmup\":%d,\"repeats\":%d,\"cleanup\":true,\"rows\":[",
           getpid(), root, trace ? "true" : "false", phase, rounds, warmup, repeats);
    for (int i = 0; i < rounds + warmup; i++) {
        struct row *r = &rows[i];
        printf("%s{\"op\":%d,\"file\":\"%s\",\"warmup\":%s,\"init\":0,\"exit\":0,\"first_ns\":%llu,\"cached_batch_ns\":%llu,\"ino\":%llu,\"dirino\":%llu,\"before_nlink\":%llu,\"after_nlink\":%llu}",
               i ? "," : "", i + 1, names[i % 4], i < warmup ? "true" : "false",
               (unsigned long long)r->first, (unsigned long long)r->cached,
               (unsigned long long)r->ino, (unsigned long long)r->dirino,
               (unsigned long long)r->before, (unsigned long long)r->after);
    }
    printf("]}\n");
    return 0;
}
