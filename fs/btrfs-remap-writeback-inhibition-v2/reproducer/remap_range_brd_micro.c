// SPDX-License-Identifier: GPL-2.0-only
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/fs.h>
#include <limits.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif

#define PERF_ACK_TIMEOUT_MS 10000

struct perf_control {
	int ctl_fd;
	int ack_fd;
};

struct block_stats {
	uint64_t write_ios;
	uint64_t sectors_written;
};

static struct perf_control perf_control = {
	.ctl_fd = -1,
	.ack_fd = -1,
};

static void perf_control_close(void)
{
	if (perf_control.ack_fd >= 0)
		close(perf_control.ack_fd);
	if (perf_control.ctl_fd >= 0)
		close(perf_control.ctl_fd);
	perf_control.ack_fd = -1;
	perf_control.ctl_fd = -1;
}

static void perf_control_init(void)
{
	const char *ctl_path = getenv("REMAP_RANGE_PERF_CTL_FIFO");
	const char *ack_path = getenv("REMAP_RANGE_PERF_ACK_FIFO");

	if ((!ctl_path || !*ctl_path) && (!ack_path || !*ack_path))
		return;
	if (!ctl_path || !*ctl_path || !ack_path || !*ack_path) {
		fprintf(stderr, "both REMAP_RANGE_PERF_CTL_FIFO and "
			"REMAP_RANGE_PERF_ACK_FIFO are required\n");
		exit(2);
	}

	perf_control.ctl_fd = open(ctl_path, O_WRONLY | O_NONBLOCK | O_CLOEXEC);
	if (perf_control.ctl_fd < 0) {
		perror(ctl_path);
		exit(1);
	}
	perf_control.ack_fd = open(ack_path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
	if (perf_control.ack_fd < 0) {
		perror(ack_path);
		perf_control_close();
		exit(1);
	}
	if (atexit(perf_control_close) != 0) {
		fprintf(stderr, "atexit failed\n");
		perf_control_close();
		exit(1);
	}
}

static void perf_control_command(const char *command)
{
	char request[32];
	char ack[16];
	struct pollfd pfd;
	ssize_t ret;
	int len;

	if (perf_control.ctl_fd < 0)
		return;

	len = snprintf(request, sizeof(request), "%s\n", command);
	if (len < 0 || (size_t)len >= sizeof(request)) {
		fprintf(stderr, "perf control command too long\n");
		exit(1);
	}
	do {
		ret = write(perf_control.ctl_fd, request, (size_t)len);
	} while (ret < 0 && errno == EINTR);
	if (ret != len) {
		if (ret < 0)
			perror("write perf control FIFO");
		else
			fprintf(stderr, "short write to perf control FIFO\n");
		exit(1);
	}

	pfd.fd = perf_control.ack_fd;
	pfd.events = POLLIN;
	pfd.revents = 0;
	do {
		ret = poll(&pfd, 1, PERF_ACK_TIMEOUT_MS);
	} while (ret < 0 && errno == EINTR);
	if (ret == 0) {
		fprintf(stderr, "timed out waiting for perf '%s' acknowledgement\n",
			command);
		exit(1);
	}
	if (ret < 0) {
		perror("poll perf acknowledgement FIFO");
		exit(1);
	}
	if (!(pfd.revents & POLLIN)) {
		fprintf(stderr, "invalid perf acknowledgement events: %#x\n",
			pfd.revents);
		exit(1);
	}

	do {
		ret = read(perf_control.ack_fd, ack, sizeof(ack) - 1);
	} while (ret < 0 && errno == EINTR);
	if (ret < 0) {
		perror("read perf acknowledgement FIFO");
		exit(1);
	}
	ack[ret] = '\0';
	if (ret < 4 || memcmp(ack, "ack\n", 4) != 0) {
		fprintf(stderr, "unexpected perf acknowledgement for '%s'\n",
			command);
		exit(1);
	}
}

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

static unsigned long parse_ulong(const char *s, unsigned long fallback)
{
	char *end = NULL;
	unsigned long v;

	if (!s || !*s)
		return fallback;
	errno = 0;
	v = strtoul(s, &end, 0);
	if (errno || !end || *end || v == 0)
		return fallback;
	return v;
}

static bool read_block_stats(const char *path, struct block_stats *stats)
{
	unsigned long long reads, reads_merged, sectors_read, read_ms;
	unsigned long long writes, writes_merged, sectors_written;
	FILE *fp;
	int fields;

	if (!path || !*path)
		return false;
	fp = fopen(path, "re");
	if (!fp) {
		perror(path);
		exit(1);
	}
	fields = fscanf(fp, "%llu %llu %llu %llu %llu %llu %llu",
			&reads, &reads_merged, &sectors_read, &read_ms,
			&writes, &writes_merged, &sectors_written);
	if (fclose(fp) != 0) {
		perror("fclose block stat");
		exit(1);
	}
	if (fields != 7) {
		fprintf(stderr, "failed to parse block stat: %s\n", path);
		exit(1);
	}
	stats->write_ios = writes;
	stats->sectors_written = sectors_written;
	return true;
}

static void join_path(char *buf, size_t len, const char *dir, const char *name)
{
	int ret = snprintf(buf, len, "%s/%s.%ld", dir, name, (long)getpid());

	if (ret < 0 || (size_t)ret >= len) {
		fprintf(stderr, "path too long\n");
		exit(1);
	}
}

static void fill_fd(int fd, unsigned long pages, uint64_t seed)
{
	unsigned char buf[4096];

	for (unsigned long page = 0; page < pages; page++) {
		for (size_t i = 0; i < sizeof(buf); i++)
			buf[i] = (unsigned char)((seed + page * 4099ULL + i) * 1315423911ULL);
		if (write(fd, buf, sizeof(buf)) != (ssize_t)sizeof(buf)) {
			perror("write");
			exit(1);
		}
	}
	if (fsync(fd) != 0) {
		perror("fsync");
		exit(1);
	}
}

static int make_filled_file(const char *path, unsigned long pages, uint64_t seed)
{
	int fd = open(path, O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC, 0600);

	if (fd < 0) {
		perror(path);
		exit(1);
	}
	fill_fd(fd, pages, seed);
	if (lseek(fd, 0, SEEK_SET) < 0) {
		perror("lseek");
		exit(1);
	}
	return fd;
}

static int make_sized_file(const char *path, unsigned long pages)
{
	int fd = open(path, O_CREAT | O_TRUNC | O_RDWR | O_CLOEXEC, 0600);
	off_t len = (off_t)pages * 4096;

	if (fd < 0) {
		perror(path);
		exit(1);
	}
	if (ftruncate(fd, len) != 0) {
		perror("ftruncate");
		exit(1);
	}
	return fd;
}

static void emit_result(const char *scenario, unsigned long ops,
			unsigned long range_bytes, uint64_t elapsed,
			uint64_t checksum, unsigned long unexpected,
			const struct block_stats *block_delta)
{
	int expected = unexpected ? 0 : 100;

	printf("scenario=%s ops=%lu bytes=%lu elapsed_ns=%" PRIu64
	       " ns_per_op=%.3f checksum=%" PRIu64
	       " expected_match_ratio=%d unexpected_results=%lu",
	       scenario, ops, ops * range_bytes, elapsed,
	       ops ? (double)elapsed / (double)ops : 0.0,
	       checksum, expected, unexpected);
	if (block_delta) {
		printf(" block_write_ios=%" PRIu64 " block_sectors_written=%" PRIu64,
		       block_delta->write_ios, block_delta->sectors_written);
	}
	putchar('\n');
}

static void format_scenario_name(char *buf, size_t len, const char *operation,
				 unsigned long range_bytes)
{
	int ret;

	if (range_bytes == 4096) {
		ret = snprintf(buf, len, "%s_4k_pages", operation);
	} else {
		ret = snprintf(buf, len, "%s_%lu_bytes", operation, range_bytes);
	}
	if (ret < 0 || (size_t)ret >= len) {
		fprintf(stderr, "scenario name too long\n");
		exit(1);
	}
}

static void run_clone(const char *dir, unsigned long ops,
		      unsigned long range_bytes, const char *block_stat_path)
{
	char src[512], dst[512];
	char scenario[64];
	int srcfd, dstfd;
	uint64_t t0, elapsed, checksum = 0;
	unsigned long unexpected = 0;
	unsigned long pages = ops * (range_bytes / 4096);
	struct block_stats block_before, block_after, block_delta;
	const struct block_stats *block_delta_ptr = NULL;

	join_path(src, sizeof(src), dir, "clone_src");
	join_path(dst, sizeof(dst), dir, "clone_dst");
	unlink(src);
	unlink(dst);

	srcfd = make_filled_file(src, pages, 0x51);
	dstfd = make_sized_file(dst, pages);

	if (read_block_stats(block_stat_path, &block_before))
		block_delta_ptr = &block_delta;
	perf_control_command("enable");
	t0 = now_ns();
	for (unsigned long i = 0; i < ops; i++) {
		struct file_clone_range args;

		memset(&args, 0, sizeof(args));
		args.src_fd = srcfd;
		args.src_offset = (uint64_t)i * range_bytes;
		args.src_length = range_bytes;
		args.dest_offset = (uint64_t)i * range_bytes;
		if (ioctl(dstfd, FICLONERANGE, &args) != 0)
			unexpected++;
		checksum += args.src_offset + args.dest_offset + args.src_length;
	}
	elapsed = now_ns() - t0;
	perf_control_command("disable");
	if (block_delta_ptr) {
		read_block_stats(block_stat_path, &block_after);
		block_delta.write_ios = block_after.write_ios - block_before.write_ios;
		block_delta.sectors_written = block_after.sectors_written -
			block_before.sectors_written;
	}

	close(dstfd);
	close(srcfd);
	unlink(dst);
	unlink(src);
	format_scenario_name(scenario, sizeof(scenario), "ficlonerange", range_bytes);
	emit_result(scenario, ops, range_bytes, elapsed, checksum, unexpected,
		    block_delta_ptr);
}

static void run_dedupe(const char *dir, unsigned long ops,
		       unsigned long range_bytes, const char *block_stat_path)
{
	char src[512], dst[512];
	char scenario[64];
	int srcfd, dstfd;
	uint64_t t0, elapsed, checksum = 0;
	unsigned long unexpected = 0;
	unsigned long pages = ops * (range_bytes / 4096);
	struct block_stats block_before, block_after, block_delta;
	const struct block_stats *block_delta_ptr = NULL;
	size_t alloc = sizeof(struct file_dedupe_range) +
		       sizeof(struct file_dedupe_range_info);

	join_path(src, sizeof(src), dir, "dedupe_src");
	join_path(dst, sizeof(dst), dir, "dedupe_dst");
	unlink(src);
	unlink(dst);

	srcfd = make_filled_file(src, pages, 0x71);
	dstfd = make_filled_file(dst, pages, 0x71);

	if (read_block_stats(block_stat_path, &block_before))
		block_delta_ptr = &block_delta;
	perf_control_command("enable");
	t0 = now_ns();
	for (unsigned long i = 0; i < ops; i++) {
		struct file_dedupe_range *range = calloc(1, alloc);

		if (!range) {
			perror("calloc");
			exit(1);
		}
		range->src_offset = (uint64_t)i * range_bytes;
		range->src_length = range_bytes;
		range->dest_count = 1;
		range->info[0].dest_fd = dstfd;
		range->info[0].dest_offset = (uint64_t)i * range_bytes;
		if (ioctl(srcfd, FIDEDUPERANGE, range) != 0) {
			unexpected++;
		} else if (range->info[0].status != FILE_DEDUPE_RANGE_SAME ||
			   range->info[0].bytes_deduped != range_bytes) {
			unexpected++;
		}
		checksum += (uint64_t)range->info[0].status +
			    (uint64_t)range->info[0].bytes_deduped +
			    range->src_offset;
		free(range);
	}
	elapsed = now_ns() - t0;
	perf_control_command("disable");
	if (block_delta_ptr) {
		read_block_stats(block_stat_path, &block_after);
		block_delta.write_ios = block_after.write_ios - block_before.write_ios;
		block_delta.sectors_written = block_after.sectors_written -
			block_before.sectors_written;
	}

	close(dstfd);
	close(srcfd);
	unlink(dst);
	unlink(src);
	format_scenario_name(scenario, sizeof(scenario), "fidedupe", range_bytes);
	emit_result(scenario, ops, range_bytes, elapsed, checksum, unexpected,
		    block_delta_ptr);
}

int main(int argc, char **argv)
{
	const char *dir = argc > 1 ? argv[1] : ".";
	const char *scenario = getenv("REMAP_RANGE_SCENARIO");
	const char *block_stat_path = getenv("REMAP_RANGE_BLOCK_STAT_PATH");
	unsigned long ops = argc > 2 ? parse_ulong(argv[2], 10000) : 10000;
	unsigned long range_bytes = argc > 3 ? parse_ulong(argv[3], 4096) :
		parse_ulong(getenv("REMAP_RANGE_BYTES"), 4096);
	bool do_clone = false;
	bool do_dedupe = false;

	if (range_bytes % 4096 != 0) {
		fprintf(stderr, "range bytes must be a positive multiple of 4096: %lu\n",
			range_bytes);
		return 2;
	}
	if (ops > ULONG_MAX / range_bytes ||
	    (uint64_t)ops > INT64_MAX / (uint64_t)range_bytes) {
		fprintf(stderr, "ops * range bytes is too large\n");
		return 2;
	}

	if (!scenario || !*scenario || strcmp(scenario, "all") == 0) {
		do_clone = true;
		do_dedupe = true;
	} else if (strcmp(scenario, "clone") == 0 ||
		   strcmp(scenario, "ficlonerange_4k_pages") == 0) {
		do_clone = true;
	} else if (strcmp(scenario, "dedupe") == 0 ||
		   strcmp(scenario, "fidedupe_4k_pages") == 0) {
		do_dedupe = true;
	} else {
		fprintf(stderr, "invalid REMAP_RANGE_SCENARIO: %s\n", scenario);
		return 2;
	}

	perf_control_init();

	if ((!scenario || !*scenario) && perf_control.ctl_fd < 0 &&
	    range_bytes == 4096) {
		printf("test_dir=%s pages=%lu bytes=%lu\n",
		       dir, ops, ops * range_bytes);
	} else {
		printf("test_dir=%s ops=%lu range_bytes=%lu bytes=%lu "
		       "selected_scenario=%s perf_control=%s\n",
		       dir, ops, range_bytes, ops * range_bytes,
		       scenario && *scenario ? scenario : "all",
		       perf_control.ctl_fd >= 0 ? "enabled" : "disabled");
	}
	if (do_clone)
		run_clone(dir, ops, range_bytes, block_stat_path);
	if (do_dedupe)
		run_dedupe(dir, ops, range_bytes, block_stat_path);
	return 0;
}
