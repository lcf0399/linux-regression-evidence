// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <sched.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* These entry points enclose exactly one write, not the readback/restoration. */
__attribute__((noinline, noclone, used))
void tr_write_begin(unsigned seq, unsigned kind)
{
	asm volatile("" : : "r"(seq), "r"(kind) : "memory");
}
__attribute__((noinline, noclone, used))
void tr_write_end(unsigned seq, int error)
{
	asm volatile("" : : "r"(seq), "r"(error) : "memory");
}

static const char *paths[] = {"/proc/sys/kernel/sched_rt_period_us",
	"/proc/sys/kernel/sched_rt_runtime_us"};
static int saved[2], armed, tracing;
static void die(const char *s) { perror(s); exit(1); }
static int value(unsigned kind)
{
	char b[64];
	int fd = open(paths[kind], O_RDONLY | O_CLOEXEC);
	if (fd < 0) die("open read");
	ssize_t n = read(fd, b, sizeof(b) - 1);
	if (n < 1 || close(fd)) die("read/close");
	b[n] = 0;
	int v; char trailing;
	if (sscanf(b, "%d %c", &v, &trailing) != 1) die("parse value");
	return v;
}
static int set(unsigned kind, int v)
{
	char b[32];
	int n = snprintf(b, sizeof(b), "%d\n", v);
	int fd = open(paths[kind], O_WRONLY | O_CLOEXEC);
	if (fd < 0) return -1;
	int rc = write(fd, b, n) == n ? 0 : -1;
	if (close(fd)) rc = -1;
	return rc;
}
static void restore(void)
{
	if (!armed) return;
	/* Increase the period before restoring the runtime, then the period. */
	int p = value(0);
	if ((p < saved[0] && set(0, saved[0])) || set(1, saved[1]) ||
	    (p > saved[0] && set(0, saved[0]))) {
		fprintf(stderr, "sysctl restore failed; outer controller must restore\n");
		_exit(2);
	}
}
static void timeout_exit(int signo)
{
	(void)signo;
	/* Do not attempt unsafe stdio in a signal handler; outer finally restores. */
	_exit(124);
}

struct test { const char *name; unsigned kind; const char *text; int error, p, r; };
static const struct test tests[] = {
	{"same-period", 0, "1000000\n", 0, 1000000, 950000},
	{"same-runtime", 1, "950000\n", 0, 1000000, 950000},
	{"change-period", 0, "1100000\n", 0, 1100000, 950000},
	{"restore-period", 0, "1000000\n", 0, 1000000, 950000},
	{"change-runtime", 1, "900000\n", 0, 1000000, 900000},
	{"restore-runtime", 1, "950000\n", 0, 1000000, 950000},
	{"period-minimum", 0, "0\n", EINVAL, 1000000, 950000},
	{"runtime-minimum", 1, "-2\n", EINVAL, 1000000, 950000},
	{"runtime-over-period", 1, "1000001\n", EINVAL, 1000000, 950000},
	{"period-under-runtime", 0, "900000\n", EINVAL, 1000000, 950000},
	{"runtime-invalid-text", 1, "junk\n", EINVAL, 1000000, 950000},
	{"period-invalid-text", 0, "junk\n", EINVAL, 1000000, 950000},
};

int main(int argc, char **argv)
{
	if (argc != 2 || (strcmp(argv[1], "semantic") && strcmp(argv[1], "probe")) ||
	    !getenv("KS_ALLOW_RT_WRITE_CHECKS") || strcmp(getenv("KS_ALLOW_RT_WRITE_CHECKS"), "1"))
		return 2;
	tracing = !strcmp(argv[1], "probe");
	cpu_set_t cpus;
	CPU_ZERO(&cpus); CPU_SET(2, &cpus);
	if (sched_setaffinity(0, sizeof(cpus), &cpus)) die("affinity");
	if (signal(SIGALRM, timeout_exit) == SIG_ERR) die("signal");
	alarm(15);
	saved[0] = value(0); saved[1] = value(1);
	if (saved[0] != 1000000 || saved[1] != 950000) die("initial controls");
	if (atexit(restore)) die("atexit");
	armed = 1;
	for (unsigned i = 0; i < sizeof(tests) / sizeof(tests[0]); ++i) {
		const struct test *t = &tests[i];
		int fd = open(paths[t->kind], O_WRONLY | O_CLOEXEC);
		if (fd < 0) die("open write");
		if (tracing) tr_write_begin(i, t->kind + 1);
		errno = 0;
		ssize_t n = write(fd, t->text, strlen(t->text));
		int error = n < 0 ? errno : 0;
		if (tracing) tr_write_end(i, error);
		if (close(fd)) die("close write");
		int p = value(0), r = value(1);
		if (error != t->error || (error ? n != -1 : n != (ssize_t)strlen(t->text)) || p != t->p || r != t->r)
			die(t->name);
		printf("{\"seq\":%u,\"name\":\"%s\",\"kind\":%u,\"bytes\":%zd,\"errno\":%d,\"period\":%d,\"runtime\":%d}\n",
		       i, t->name, t->kind + 1, n, error, p, r);
	}
	if (value(0) != saved[0] || value(1) != saved[1]) die("final state");
	armed = 0;
	alarm(0);
	return 0;
}
