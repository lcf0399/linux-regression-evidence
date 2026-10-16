#define _GNU_SOURCE

/*
 * Focused AF_UNIX/SOCK_DGRAM sendmmsg() microbenchmark.
 *
 * The timed region contains only sendmmsg().  Message preparation, peer
 * draining, and payload validation happen outside timing.  This is the
 * concise counterpart of the larger source used for the formal A/B result.
 */

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#define DEFAULT_CPU 2U
#define BATCH_SIZE 32U
#define PAYLOAD_SIZE 128U
#define MESSAGES_PER_ROUND 65536U
#define WARMUP_ROUNDS 3U
#define MEASURED_ROUNDS 15U

struct options {
	unsigned int cpu;
	unsigned int warmups;
	unsigned int rounds;
	unsigned int messages;
};

struct workload {
	int sockets[2];
	unsigned char send_buf[BATCH_SIZE][PAYLOAD_SIZE];
	struct iovec iov[BATCH_SIZE];
	struct mmsghdr messages[BATCH_SIZE];
};

struct round_result {
	uint64_t elapsed_ns;
	uint64_t messages;
	uint64_t batches;
	uint64_t content_mismatches;
	uint64_t bad_lengths;
	uint64_t eagain;
	bool pass;
};

static uint64_t now_ns(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
		perror("clock_gettime");
		exit(EXIT_FAILURE);
	}
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void fill_pattern(void *buffer, uint64_t sequence)
{
	unsigned char *p = buffer;
	unsigned int i;

	for (i = 0; i < PAYLOAD_SIZE; i++)
		p[i] = (unsigned char)((sequence * 131U + i * 17U + 0x5aU) & 0xffU);
}

static bool pattern_matches(const void *buffer, uint64_t sequence)
{
	unsigned char expected[PAYLOAD_SIZE];

	fill_pattern(expected, sequence);
	return memcmp(buffer, expected, sizeof(expected)) == 0;
}

static int set_nonblock(int fd)
{
	int flags = fcntl(fd, F_GETFL);

	return flags < 0 ? -1 : fcntl(fd, F_SETFL, flags | O_NONBLOCK);
}

static int pin_cpu(unsigned int cpu)
{
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	if (sched_setaffinity(0, sizeof(set), &set) != 0) {
		perror("sched_setaffinity");
		return -1;
	}
	return sched_getcpu() == (int)cpu ? 0 : -1;
}

static int setup_workload(struct workload *work)
{
	int size = 1 << 20;
	unsigned int i;

	memset(work, 0, sizeof(*work));
	work->sockets[0] = -1;
	work->sockets[1] = -1;
	if (socketpair(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0,
		       work->sockets) != 0) {
		perror("socketpair");
		return -1;
	}
	if (set_nonblock(work->sockets[0]) != 0 ||
	    set_nonblock(work->sockets[1]) != 0) {
		perror("set_nonblock");
		return -1;
	}
	(void)setsockopt(work->sockets[0], SOL_SOCKET, SO_SNDBUF,
			 &size, sizeof(size));
	(void)setsockopt(work->sockets[0], SOL_SOCKET, SO_RCVBUF,
			 &size, sizeof(size));
	(void)setsockopt(work->sockets[1], SOL_SOCKET, SO_SNDBUF,
			 &size, sizeof(size));
	(void)setsockopt(work->sockets[1], SOL_SOCKET, SO_RCVBUF,
			 &size, sizeof(size));

	for (i = 0; i < BATCH_SIZE; i++) {
		work->iov[i].iov_base = work->send_buf[i];
		work->iov[i].iov_len = PAYLOAD_SIZE;
		work->messages[i].msg_hdr.msg_iov = &work->iov[i];
		work->messages[i].msg_hdr.msg_iovlen = 1;
	}
	return 0;
}

static void cleanup_workload(struct workload *work)
{
	if (work->sockets[0] >= 0)
		close(work->sockets[0]);
	if (work->sockets[1] >= 0)
		close(work->sockets[1]);
}

static int drain_and_validate(struct workload *work, uint64_t base,
			      struct round_result *result)
{
	unsigned char buffer[PAYLOAD_SIZE];
	unsigned int i;

	for (i = 0; i < BATCH_SIZE; i++) {
		ssize_t nr = recv(work->sockets[1], buffer, sizeof(buffer),
				  MSG_DONTWAIT);

		if (nr != (ssize_t)sizeof(buffer)) {
			fprintf(stderr, "recv=%zd expected=%u at slot=%u errno=%d\n",
				nr, PAYLOAD_SIZE, i, errno);
			return -1;
		}
		if (!pattern_matches(buffer, base + i))
			result->content_mismatches++;
	}
	errno = 0;
	if (recv(work->sockets[1], buffer, sizeof(buffer), MSG_DONTWAIT) >= 0 ||
	    errno != EAGAIN)
		return -1;
	return 0;
}

static int run_round(struct workload *work, unsigned int messages,
		     bool timed, struct round_result *result)
{
	unsigned int base, i;

	memset(result, 0, sizeof(*result));
	if (!messages || messages % BATCH_SIZE)
		return -1;

	for (base = 0; base < messages; base += BATCH_SIZE) {
		uint64_t started = 0;
		int sent;

		for (i = 0; i < BATCH_SIZE; i++) {
			fill_pattern(work->send_buf[i], (uint64_t)base + i);
			work->messages[i].msg_len = 0;
		}

		/* Only this system call is timed. */
		if (timed)
			started = now_ns();
		sent = sendmmsg(work->sockets[0], work->messages, BATCH_SIZE,
				MSG_DONTWAIT | MSG_NOSIGNAL);
		if (timed)
			result->elapsed_ns += now_ns() - started;

		if (sent != (int)BATCH_SIZE) {
			if (sent < 0 && errno == EAGAIN)
				result->eagain++;
			fprintf(stderr, "sendmmsg=%d expected=%u errno=%d\n",
				sent, BATCH_SIZE, errno);
			return -1;
		}
		result->batches++;
		for (i = 0; i < BATCH_SIZE; i++) {
			if (work->messages[i].msg_len != PAYLOAD_SIZE)
				result->bad_lengths++;
			else
				result->messages++;
		}
		if (drain_and_validate(work, base, result) != 0)
			return -1;
	}

	result->pass = result->messages == messages &&
		result->batches == messages / BATCH_SIZE &&
		result->content_mismatches == 0 && result->bad_lengths == 0 &&
		result->eagain == 0;
	return result->pass ? 0 : -1;
}

static int parse_uint(const char *text, unsigned int *value)
{
	char *end;
	unsigned long parsed;

	errno = 0;
	parsed = strtoul(text, &end, 10);
	if (errno || !*text || *end || parsed > UINT32_MAX)
		return -1;
	*value = (unsigned int)parsed;
	return 0;
}

static int parse_options(int argc, char **argv, struct options *opt)
{
	int i;

	opt->cpu = DEFAULT_CPU;
	opt->warmups = WARMUP_ROUNDS;
	opt->rounds = MEASURED_ROUNDS;
	opt->messages = MESSAGES_PER_ROUND;
	for (i = 1; i < argc; i++) {
		unsigned int *target = NULL;

		if (!strcmp(argv[i], "--cpu"))
			target = &opt->cpu;
		else if (!strcmp(argv[i], "--warmups"))
			target = &opt->warmups;
		else if (!strcmp(argv[i], "--rounds"))
			target = &opt->rounds;
		else if (!strcmp(argv[i], "--messages"))
			target = &opt->messages;
		else
			return -1;
		if (++i >= argc || parse_uint(argv[i], target) != 0)
			return -1;
	}
	return opt->rounds && opt->messages &&
		!(opt->messages % BATCH_SIZE) ? 0 : -1;
}

static void print_text_value(const char *label, const char *path)
{
	char buffer[256];
	FILE *file = fopen(path, "r");

	if (!file || !fgets(buffer, sizeof(buffer), file)) {
		printf("# %s=unknown\n", label);
	} else {
		buffer[strcspn(buffer, "\r\n")] = '\0';
		printf("# %s=%s\n", label, buffer);
	}
	if (file)
		fclose(file);
}

int main(int argc, char **argv)
{
	struct options opt;
	struct workload work;
	struct round_result result;
	struct utsname uts;
	unsigned int round;
	int rc = EXIT_FAILURE;

	if (parse_options(argc, argv, &opt) != 0) {
		fprintf(stderr, "usage: %s [--cpu N] [--warmups N] "
			"[--rounds N] [--messages N]\n", argv[0]);
		return EXIT_FAILURE;
	}
	if (pin_cpu(opt.cpu) != 0 || setup_workload(&work) != 0)
		goto out;

	if (uname(&uts) == 0)
		printf("# kernel_release=%s\n", uts.release);
	print_text_value("boot_id", "/proc/sys/kernel/random/boot_id");
	print_text_value("apparmor_profile", "/proc/self/attr/current");
	printf("# cpu=%u\n# batch_size=%u\n# payload_bytes=%u\n",
	       opt.cpu, BATCH_SIZE, PAYLOAD_SIZE);
	printf("# messages_per_round=%u\n# warmup_rounds=%u\n# measured_rounds=%u\n",
	       opt.messages, opt.warmups, opt.rounds);

	/* One untimed semantic smoke, then the requested warm-up rounds. */
	if (run_round(&work, BATCH_SIZE, false, &result) != 0)
		goto out;
	for (round = 0; round < opt.warmups; round++)
		if (run_round(&work, opt.messages, false, &result) != 0)
			goto out;

	puts("round\tmessages\tbatches\telapsed_ns\tns_per_message\tsemantic_pass");
	for (round = 0; round < opt.rounds; round++) {
		double ns_per_message;

		if (run_round(&work, opt.messages, true, &result) != 0)
			goto out;
		ns_per_message = (double)result.elapsed_ns / (double)result.messages;
		printf("%u\t%" PRIu64 "\t%" PRIu64 "\t%" PRIu64
		       "\t%.6f\t1\n", round, result.messages, result.batches,
		       result.elapsed_ns, ns_per_message);
	}
	rc = EXIT_SUCCESS;
out:
	cleanup_workload(&work);
	return rc;
}
