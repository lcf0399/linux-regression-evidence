#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <time.h>
#include <unistd.h>

/* Non-timing semantic test. Never inspect the target before the first open.
 * No tasks, controllers, watches, or file handles are installed in the leaf. */
struct trial {
    char leaf[256], file[288];
    int round, mode, delta, fd, open_errno, remove_errno, op_tid, rm_tid;
    unsigned long ino, nlink;
    uint64_t epoch, os, oe, rs, re;
    atomic_int ready, opened, removed;
};
static uint64_t now(void) {
    struct timespec t;
    if (clock_gettime(CLOCK_MONOTONIC, &t)) abort();
    return (uint64_t)t.tv_sec * 1000000000 + t.tv_nsec;
}
static void pin(int cpu) {
    cpu_set_t set;
    CPU_ZERO(&set); CPU_SET(cpu, &set);
    if (pthread_setaffinity_np(pthread_self(), sizeof(set), &set)) abort();
}
static void spin(uint64_t end) { while (now() < end) __asm__ volatile("pause"); }
static void mark(int round) { syscall(SYS_getpgid, 10000 + round); }
static void *opener(void *arg) {
    struct trial *t = arg;
    pin(2); t->op_tid = (int)syscall(SYS_gettid); mark(t->round);
    atomic_fetch_add(&t->ready, 1);
    while (atomic_load(&t->ready) != 3) __asm__ volatile("pause");
    spin(t->epoch + (t->delta < 0 ? -t->delta : 0));
    if (t->mode == 2) while (!atomic_load(&t->removed)) __asm__ volatile("pause");
    t->os = now();
    t->fd = open(t->file, O_RDONLY | O_CLOEXEC);
    t->open_errno = t->fd < 0 ? errno : 0;
    t->oe = now();
    atomic_store(&t->opened, 1);
    while (!atomic_load(&t->removed)) __asm__ volatile("pause");
    if (t->fd >= 0) {
        struct stat st;
        if (fstat(t->fd, &st)) abort();
        t->ino = st.st_ino; t->nlink = st.st_nlink;
        if (close(t->fd)) abort();
    }
    return NULL;
}
static void *remover(void *arg) {
    struct trial *t = arg;
    pin(4); t->rm_tid = (int)syscall(SYS_gettid); mark(t->round);
    atomic_fetch_add(&t->ready, 1);
    while (atomic_load(&t->ready) != 3) __asm__ volatile("pause");
    spin(t->epoch + (t->delta > 0 ? t->delta : 0));
    if (t->mode == 1) while (!atomic_load(&t->opened)) __asm__ volatile("pause");
    t->rs = now();
    int rc = rmdir(t->leaf);
    t->remove_errno = rc ? errno : 0;
    t->re = now(); atomic_store(&t->removed, 1);
    return NULL;
}
int main(void) {
    char root[160];
    const int deltas[] = {-100000, -20000, -5000, -1000, 0, 1000, 5000, 20000, 100000};
    snprintf(root, sizeof(root), "/sys/fs/cgroup/kernel-study-first-race-%d", getpid());
    setvbuf(stdout, NULL, _IOLBF, 0);
    /* Readiness before mkdir: probes observe every creation in this process. */
    printf("{\"ready_pid\":%d,\"root\":\"%s\"}\n", getpid(), root);
    if (getchar() != 'G') return 2;
    alarm(60);
    pin(0);
    if (mkdir(root, 0755)) { perror("root mkdir"); return 3; }
    int round = 0, failed = 0;
    for (int group = 0; group < 11 && !failed; ++group) {
        for (int i = 0; i < 16; ++i) {
            struct trial t = {.round = ++round, .mode = group < 2 ? group + 1 : 0,
                              .delta = group < 2 ? 0 : deltas[group - 2], .fd = -1};
            snprintf(t.leaf, sizeof(t.leaf), "%s/first-%05d", root, round);
            snprintf(t.file, sizeof(t.file), "%s/cgroup.events", t.leaf);
            mark(round);
            if (mkdir(t.leaf, 0755)) { perror("leaf mkdir"); failed = 1; break; }
            pthread_t op, rm;
            if (pthread_create(&op, NULL, opener, &t) || pthread_create(&rm, NULL, remover, &t)) abort();
            while (atomic_load(&t.ready) != 2) __asm__ volatile("pause");
            t.epoch = now() + 1000000;
            atomic_store(&t.ready, 3);
            if (pthread_join(op, NULL) || pthread_join(rm, NULL)) abort();
            int good = !t.remove_errno && (t.fd >= 0 ? !t.nlink :
                       (t.open_errno == ENOENT || t.open_errno == ENODEV));
            if (t.mode == 1 && t.fd < 0) good = 0;
            if (t.mode == 2 && t.open_errno != ENOENT) good = 0;
            printf("{\"round\":%d,\"mode\":%d,\"delta_ns\":%d,\"open_tid\":%d,\"remove_tid\":%d,"
                   "\"opened\":%s,\"open_errno\":%d,\"remove_errno\":%d,\"ino\":%lu,\"after_nlink\":%lu,"
                   "\"open_start\":%llu,\"open_end\":%llu,\"remove_start\":%llu,\"remove_end\":%llu,\"pass\":%s}\n",
                   round, t.mode, t.delta, t.op_tid, t.rm_tid, t.fd >= 0 ? "true" : "false", t.open_errno,
                   t.remove_errno, t.ino, t.nlink, (unsigned long long)t.os, (unsigned long long)t.oe,
                   (unsigned long long)t.rs, (unsigned long long)t.re, good ? "true" : "false");
            if (!good) { failed = 1; if (t.remove_errno) rmdir(t.leaf); break; }
        }
    }
    int clean = !rmdir(root);
    printf("{\"completed\":%d,\"cleanup\":%s,\"pass\":%s}\n", round,
           clean ? "true" : "false", !failed && clean && round == 176 ? "true" : "false");
    return failed || !clean || round != 176;
}
