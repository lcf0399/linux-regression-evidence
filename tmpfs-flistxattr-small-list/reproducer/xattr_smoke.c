// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/xattr.h>
#include <time.h>
#include <unistd.h>

#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif
#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

static uint64_t now_ns(void)
{
	struct timespec ts;
	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
		if (errno != EINVAL || clock_gettime(CLOCK_MONOTONIC, &ts) != 0) {
			perror("clock_gettime");
			exit(1);
		}
	}
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static unsigned long env_ulong(const char *name, unsigned long fallback)
{
	const char *value = getenv(name);
	char *end = NULL;
	unsigned long parsed;
	if (!value || !*value)
		return fallback;
	errno = 0;
	parsed = strtoul(value, &end, 0);
	if (errno || !end || *end != '\0' || parsed == 0)
		return fallback;
	return parsed;
}

static void print_result(const char *scenario, unsigned long ops, uint64_t elapsed_ns, int ok)
{
	printf("scenario=%s ops=%lu elapsed_ns=%" PRIu64
	       " ns_per_op=%.3f expected_match_ratio=%d unexpected_results=%d\n",
	       scenario, ops, elapsed_ns, ops ? (double)elapsed_ns / (double)ops : 0.0,
	       ok ? 100 : 0, ok ? 0 : 1);
}

static int want_scenario(const char *selected, const char *scenario)
{
	return !selected || !*selected || !strcmp(selected, "all") ||
	       !strcmp(selected, scenario);
}

static int make_file(const char *dir, char *path, size_t len)
{
	int ret = snprintf(path, len, "%s/xattr_smoke_%ld", dir, (long)getpid());
	int fd;
	if (ret < 0 || (size_t)ret >= len) {
		fprintf(stderr, "path too long\n");
		exit(1);
	}
	fd = open(path, O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC, 0600);
	if (fd < 0) {
		perror(path);
		exit(1);
	}
	if (write(fd, "x", 1) != 1) {
		perror("write");
		exit(1);
	}
	return fd;
}

static size_t seed_count_xattrs(int fd, unsigned long count)
{
	char name[64];
	size_t expected_len = 0;

	for (unsigned long i = 0; i < count; i++) {
		int ret = snprintf(name, sizeof(name), "user.fsregression.%03lu", i);
		if (ret < 0 || (size_t)ret >= sizeof(name)) {
			fprintf(stderr, "xattr name too long\n");
			exit(1);
		}
		if (fsetxattr(fd, name, "seed", 5, 0) != 0) {
			perror("fsetxattr count seed");
			exit(1);
		}
		expected_len += strlen(name) + 1;
	}

	return expected_len;
}

int main(void)
{
	const char *dir = getenv("TEST_DIR");
	const char *selected = getenv("XATTR_SCENARIO");
	unsigned long iterations = env_ulong("ITERATIONS", 4096);
	unsigned long xattr_count = env_ulong("XATTR_COUNT", 1);
	const char *name = "user.fsregression";
	char path[PATH_MAX], value[64], list[8192], got[128];
	int fd, ok = 1;
	uint64_t t0, t1;

	if (!dir || !*dir)
		dir = "/tmp";
	fd = make_file(dir, path, sizeof(path));

	if (want_scenario(selected, "setxattr_path")) {
		ok = 1;
		t0 = now_ns();
		for (unsigned long i = 0; i < iterations; i++) {
			snprintf(value, sizeof(value), "value-%lu", i);
			if (setxattr(path, name, value, strlen(value) + 1, 0) != 0)
				ok = 0;
		}
		t1 = now_ns();
		print_result("setxattr_path", iterations, t1 - t0, ok);
	}

	if (setxattr(path, name, "seed", 5, 0) != 0)
		ok = 0;

	if (want_scenario(selected, "getxattr_path")) {
		ok = 1;
		t0 = now_ns();
		for (unsigned long i = 0; i < iterations; i++) {
			ssize_t ret = getxattr(path, name, got, sizeof(got));
			if (ret <= 0)
				ok = 0;
		}
		t1 = now_ns();
		print_result("getxattr_path", iterations, t1 - t0, ok);
	}

	if (want_scenario(selected, "listxattr_path")) {
		ok = 1;
		t0 = now_ns();
		for (unsigned long i = 0; i < iterations; i++) {
			ssize_t ret = listxattr(path, list, sizeof(list));
			if (ret <= 0)
				ok = 0;
		}
		t1 = now_ns();
		print_result("listxattr_path", iterations, t1 - t0, ok);
	}

	if (want_scenario(selected, "fsetxattr_fd")) {
		ok = 1;
		t0 = now_ns();
		for (unsigned long i = 0; i < iterations; i++) {
			snprintf(value, sizeof(value), "fvalue-%lu", i);
			if (fsetxattr(fd, name, value, strlen(value) + 1, 0) != 0)
				ok = 0;
		}
		t1 = now_ns();
		print_result("fsetxattr_fd", iterations, t1 - t0, ok);
	}

	if (fsetxattr(fd, name, "fseed", 6, 0) != 0)
		ok = 0;

	if (want_scenario(selected, "fgetxattr_fd")) {
		ok = 1;
		t0 = now_ns();
		for (unsigned long i = 0; i < iterations; i++) {
			ssize_t ret = fgetxattr(fd, name, got, sizeof(got));
			if (ret <= 0)
				ok = 0;
		}
		t1 = now_ns();
		print_result("fgetxattr_fd", iterations, t1 - t0, ok);
	}

	if (want_scenario(selected, "flistxattr_fd")) {
		ok = 1;
		t0 = now_ns();
		for (unsigned long i = 0; i < iterations; i++) {
			ssize_t ret = flistxattr(fd, list, sizeof(list));
			if (ret <= 0)
				ok = 0;
		}
		t1 = now_ns();
		print_result("flistxattr_fd", iterations, t1 - t0, ok);
	}

	if (want_scenario(selected, "flistxattr_fd_count")) {
		size_t expected_len;

		if (xattr_count > 256) {
			fprintf(stderr, "XATTR_COUNT too large: %lu\n", xattr_count);
			exit(1);
		}
		close(fd);
		unlink(path);
		fd = make_file(dir, path, sizeof(path));
		expected_len = seed_count_xattrs(fd, xattr_count);
		if (expected_len > sizeof(list)) {
			fprintf(stderr, "list buffer too small for %lu xattrs\n", xattr_count);
			exit(1);
		}

		ok = 1;
		t0 = now_ns();
		for (unsigned long i = 0; i < iterations; i++) {
			ssize_t ret = flistxattr(fd, list, sizeof(list));
			if (ret != (ssize_t)expected_len)
				ok = 0;
		}
		t1 = now_ns();
		print_result("flistxattr_fd_count", iterations, t1 - t0, ok);
	}

	if (want_scenario(selected, "fd_set_get_listxattr")) {
		ok = 1;
		t0 = now_ns();
		for (unsigned long i = 0; i < iterations; i++) {
			snprintf(value, sizeof(value), "fvalue-%lu", i);
			if (fsetxattr(fd, name, value, strlen(value) + 1, 0) != 0)
				ok = 0;
			if (fgetxattr(fd, name, got, sizeof(got)) <= 0)
				ok = 0;
			if (flistxattr(fd, list, sizeof(list)) <= 0)
				ok = 0;
		}
		t1 = now_ns();
		print_result("fd_set_get_listxattr", iterations * 3, t1 - t0, ok);
	}

	if (want_scenario(selected, "set_remove_xattr")) {
		ok = 1;
		t0 = now_ns();
		for (unsigned long i = 0; i < iterations; i++) {
			if (setxattr(path, name, "x", 2, 0) != 0)
				ok = 0;
			if (removexattr(path, name) != 0)
				ok = 0;
		}
		t1 = now_ns();
		print_result("set_remove_xattr", iterations * 2, t1 - t0, ok);
	}

	close(fd);
	unlink(path);
	return ok ? 0 : 1;
}
