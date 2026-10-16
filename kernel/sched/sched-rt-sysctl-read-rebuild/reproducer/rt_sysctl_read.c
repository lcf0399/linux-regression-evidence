// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <limits.h>
#include <sched.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

/* Stable probe boundaries: exactly one pread() between each pair. */
__attribute__((noinline, noclone, used))
void tr_read_begin(unsigned seq, unsigned kind, int fd)
{
	asm volatile("" : : "r"(seq), "r"(kind), "r"(fd) : "memory");
}

__attribute__((noinline, noclone, used))
void tr_read_end(unsigned seq, unsigned kind, ssize_t result)
{
	asm volatile("" : : "r"(seq), "r"(kind), "r"(result) : "memory");
}

static void die(const char *why)
{
	fprintf(stderr, "rt-sysctl-read: %s (errno=%d)\n", why, errno);
	exit(1);
}

static void alarm_exit(int signo)
{
	(void)signo;
	static const char message[] = "rt-sysctl-read: watchdog timeout\n";
	ssize_t written = write(STDERR_FILENO, message, sizeof(message) - 1);
	(void)written;
	_exit(124);
}

static long number(const char *text, long min, long max)
{
	char *end;
	errno = 0;
	long value = strtol(text, &end, 10);
	if (errno || !*text || *end || value < min || value > max)
		die("invalid integer argument");
	return value;
}

static uint64_t clock_ns(clockid_t clock)
{
	struct timespec ts;
	if (clock_gettime(clock, &ts))
		die("clock_gettime");
	return (uint64_t)ts.tv_sec * 1000000000ULL + ts.tv_nsec;
}

int main(int argc, char **argv)
{
	if (argc != 6 && argc != 7) {
		fprintf(stderr, "usage: %s semantic|probe|timing|fixture period|runtime|rr ops cpu expected [fixture-file]\n", argv[0]);
		return 2;
	}
	int fixture = !strcmp(argv[1], "fixture");
	int probe = !strcmp(argv[1], "probe");
	int timing = !strcmp(argv[1], "timing");
	if ((!fixture && !probe && !timing && strcmp(argv[1], "semantic")) ||
	    (fixture != (argc == 7)))
		die("unknown mode or fixture path outside fixture mode");
	if (timing && (!getenv("KS_ALLOW_SYSCTL_TIMING") ||
		       strcmp(getenv("KS_ALLOW_SYSCTL_TIMING"), "1"))) {
		fprintf(stderr, "timing requires a separately authorized experiment\n");
		return 2;
	}
	const char *names[] = {"period", "runtime", "rr"};
	const char *paths[] = {"/proc/sys/kernel/sched_rt_period_us",
		"/proc/sys/kernel/sched_rt_runtime_us",
		"/proc/sys/kernel/sched_rr_timeslice_ms"};
	unsigned kind;
	for (kind = 0; kind < 3; ++kind)
		if (!strcmp(argv[2], names[kind]))
			break;
	if (kind == 3)
		die("unknown case");
	unsigned ops = number(argv[3], 1, 4096);
	int cpu = number(argv[4], 0, CPU_SETSIZE - 1);
	long expected = number(argv[5], -1, INT_MAX);
	if ((probe || !strcmp(argv[1], "semantic")) && ops > 64)
		die("qualification is limited to 64 reads per invocation");
	cpu_set_t allowed, chosen;
	if (sched_getaffinity(0, sizeof(allowed), &allowed) || !CPU_ISSET(cpu, &allowed))
		die("CPU not in initial affinity");
	CPU_ZERO(&chosen);
	CPU_SET(cpu, &chosen);
	if (sched_setaffinity(0, sizeof(chosen), &chosen))
		die("sched_setaffinity");
	struct sigaction sa = {.sa_handler = alarm_exit};
	if (sigaction(SIGALRM, &sa, NULL) || prctl(PR_SET_PDEATHSIG, SIGKILL))
		die("watchdog setup");
	alarm(10);
	char wanted[64], actual[64];
	int bytes = snprintf(wanted, sizeof(wanted), "%ld\n", expected);
	int fd = open(fixture ? argv[6] : paths[kind], O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
	if (fd < 0)
		die("open");
	uint64_t start = 0, wall = 0, cpu_start = 0, cpu_time = 0;
	if (timing) {
		cpu_start = clock_ns(CLOCK_THREAD_CPUTIME_ID);
		start = clock_ns(CLOCK_MONOTONIC_RAW);
	}
	for (unsigned seq = 0; seq < ops; ++seq) {
		if (probe)
			tr_read_begin(seq, kind + 1, fd);
		ssize_t got = pread(fd, actual, sizeof(actual) - 1, 0);
		if (probe)
			tr_read_end(seq, kind + 1, got);
		/* No retries, partial-read loop, lseek(), writes or implicit EOF read. */
		if (got != bytes || memcmp(actual, wanted, bytes))
			die("unexpected read result");
	}
	if (timing) {
		wall = clock_ns(CLOCK_MONOTONIC_RAW) - start;
		cpu_time = clock_ns(CLOCK_THREAD_CPUTIME_ID) - cpu_start;
	}
	if (sched_getcpu() != cpu || sched_getaffinity(0, sizeof(allowed), &allowed) ||
	    CPU_COUNT(&allowed) != 1 || !CPU_ISSET(cpu, &allowed) || close(fd))
		die("final affinity or close");
	alarm(0);
	printf("{\"schema\":\"rt-sysctl-read-v1\",\"mode\":\"%s\",\"case\":\"%s\","
	       "\"case_id\":%u,\"pid\":%d,\"cpu\":%d,\"reads\":%u,\"expected\":%ld,"
	       "\"bytes_per_read\":%d,\"total_bytes\":%u,\"semantic_pass\":true,"
	       "\"performance_evidence\":%s,\"wall_ns\":%" PRIu64 ",\"thread_cpu_ns\":%" PRIu64 "}\n",
	       argv[1], names[kind], kind + 1, getpid(), cpu, ops, expected, bytes,
	       ops * bytes, timing ? "true" : "false", wall, cpu_time);
	return 0;
}
