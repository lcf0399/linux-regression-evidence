#define _GNU_SOURCE

#include <errno.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdatomic.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/resource.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#define NS_PER_MS 1000000ULL
#define DEFAULT_WARMUPS 3U
#define DEFAULT_ROUNDS 15U
#define DEFAULT_OPS 100000ULL
#define DEFAULT_AFFINITY_OPS 20000ULL
#define WAIT_TIMEOUT_NS (10ULL * 1000000000ULL)

struct ks_sched_attr {
	uint32_t size;
	uint32_t sched_policy;
	uint64_t sched_flags;
	int32_t sched_nice;
	uint32_t sched_priority;
	uint64_t sched_runtime;
	uint64_t sched_deadline;
	uint64_t sched_period;
	uint32_t sched_util_min;
	uint32_t sched_util_max;
};

enum profile {
	PROFILE_YIELD_DUAL,
	PROFILE_YIELD_SOLO,
	PROFILE_SETPARAM_CURRENT,
	PROFILE_SETPARAM_FAST_CONTROL,
	PROFILE_AFFINITY_QUEUED,
	PROFILE_AFFINITY_SAME_MASK,
};

struct options {
	enum profile profile;
	const char *profile_name;
	int cpu_a;
	int cpu_b;
	uint64_t ops;
	unsigned int warmups;
	unsigned int rounds;
};

struct result_row {
	uint64_t requested_ops;
	uint64_t success_ops;
	uint64_t elapsed_ns;
	uint64_t call_ns_sum;
	uint64_t errors;
	pid_t caller_tid;
	pid_t peer_tid;
	pid_t target_tid;
	uint64_t heartbeat_before;
	uint64_t heartbeat_after;
	int final_cpu;
};

struct yield_peer_state {
	atomic_uint_fast64_t ready_generation;
	atomic_uint_fast64_t go_generation;
	atomic_uint_fast64_t peer_done_generation;
	atomic_uint_fast64_t main_done_generation;
	atomic_uint_fast64_t cumulative_errors;
	atomic_int thread_error;
	atomic_int peer_tid;
	uint64_t ops;
	unsigned int total_rounds;
	int cpu;
};

struct affinity_state {
	atomic_uint_fast64_t turn;
	atomic_uint_fast64_t target_heartbeat;
	atomic_uint_fast64_t controller_b_call_ns;
	atomic_uint_fast64_t controller_b_success;
	atomic_uint_fast64_t controller_b_errors;
	atomic_int target_ready;
	atomic_int controller_b_ready;
	atomic_int controller_b_tid;
	atomic_int target_last_cpu;
	atomic_int thread_error;
	atomic_int stop_target;
	pid_t target_tid;
	uint64_t ops;
	int cpu_a;
	int cpu_b;
	int target_cpu;
};

static void die(const char *what)
{
	fprintf(stderr, "%s: %s\n", what, strerror(errno));
	exit(EXIT_FAILURE);
}

static void fail(const char *what)
{
	fprintf(stderr, "%s\n", what);
	exit(EXIT_FAILURE);
}

static void set_thread_name(const char *name)
{
	int error = pthread_setname_np(pthread_self(), name);

	if (error) {
		errno = error;
		die("pthread_setname_np");
	}
}

static uint64_t now_ns(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts))
		die("clock_gettime");
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static void cpu_relax(void)
{
#if defined(__x86_64__) || defined(__i386__)
	__asm__ __volatile__("pause" ::: "memory");
#else
	atomic_signal_fence(memory_order_seq_cst);
#endif
}

static uint64_t parse_u64(const char *value, const char *name, bool allow_zero)
{
	char *end = NULL;
	unsigned long long parsed;

	errno = 0;
	parsed = strtoull(value, &end, 10);
	if (errno || !end || *end || (!allow_zero && parsed == 0)) {
		fprintf(stderr, "invalid %s: %s\n", name, value);
		exit(EXIT_FAILURE);
	}
	return (uint64_t)parsed;
}

static int parse_cpu(const char *value, const char *name)
{
	uint64_t parsed = parse_u64(value, name, true);

	if (parsed >= CPU_SETSIZE || parsed > INT_MAX) {
		fprintf(stderr, "invalid %s: %s\n", name, value);
		exit(EXIT_FAILURE);
	}
	return (int)parsed;
}

static enum profile parse_profile(const char *name)
{
	if (!strcmp(name, "yield-dual"))
		return PROFILE_YIELD_DUAL;
	if (!strcmp(name, "yield-solo"))
		return PROFILE_YIELD_SOLO;
	if (!strcmp(name, "setparam-current-v2"))
		return PROFILE_SETPARAM_CURRENT;
	if (!strcmp(name, "setparam-fast-control-v2"))
		return PROFILE_SETPARAM_FAST_CONTROL;
	if (!strcmp(name, "affinity-queued-v2"))
		return PROFILE_AFFINITY_QUEUED;
	if (!strcmp(name, "affinity-remote-same-mask-v2"))
		return PROFILE_AFFINITY_SAME_MASK;
	fail("unknown profile");
	return PROFILE_YIELD_SOLO;
}

static bool is_affinity_profile(enum profile profile)
{
	return profile == PROFILE_AFFINITY_QUEUED ||
	       profile == PROFILE_AFFINITY_SAME_MASK;
}

static void usage(const char *argv0)
{
	fprintf(stderr,
		"usage: %s --profile yield-dual|yield-solo|setparam-current-v2|"
		"setparam-fast-control-v2|affinity-queued-v2|"
		"affinity-remote-same-mask-v2 "
		"--cpu-a N [--cpu-b N] [--ops N] [--warmups N] [--rounds N]\n",
		argv0);
	exit(EXIT_FAILURE);
}

static struct options parse_options(int argc, char **argv)
{
	struct options opts = {
		.cpu_a = -1,
		.cpu_b = -1,
		.ops = 0,
		.warmups = DEFAULT_WARMUPS,
		.rounds = DEFAULT_ROUNDS,
	};
	int i;

	for (i = 1; i < argc; i++) {
		if (!strcmp(argv[i], "--profile") && i + 1 < argc) {
			opts.profile_name = argv[++i];
			opts.profile = parse_profile(opts.profile_name);
		} else if (!strcmp(argv[i], "--cpu-a") && i + 1 < argc) {
			opts.cpu_a = parse_cpu(argv[++i], "CPU A");
		} else if (!strcmp(argv[i], "--cpu-b") && i + 1 < argc) {
			opts.cpu_b = parse_cpu(argv[++i], "CPU B");
		} else if (!strcmp(argv[i], "--ops") && i + 1 < argc) {
			opts.ops = parse_u64(argv[++i], "ops", false);
		} else if (!strcmp(argv[i], "--warmups") && i + 1 < argc) {
			uint64_t parsed = parse_u64(argv[++i], "warmups", true);

			if (parsed > UINT_MAX)
				fail("warmups is too large");
			opts.warmups = (unsigned int)parsed;
		} else if (!strcmp(argv[i], "--rounds") && i + 1 < argc) {
			uint64_t parsed = parse_u64(argv[++i], "rounds", false);

			if (parsed > UINT_MAX)
				fail("rounds is too large");
			opts.rounds = (unsigned int)parsed;
		} else {
			usage(argv[0]);
		}
	}
	if (!opts.profile_name || opts.cpu_a < 0)
		usage(argv[0]);
	if (opts.ops == 0)
		opts.ops = is_affinity_profile(opts.profile) ?
			DEFAULT_AFFINITY_OPS : DEFAULT_OPS;
	if (is_affinity_profile(opts.profile)) {
		if (opts.cpu_b < 0 || opts.cpu_a == opts.cpu_b)
			fail("affinity profiles need two different CPUs");
	}
	if (opts.profile == PROFILE_AFFINITY_QUEUED) {
		if (opts.ops & 1)
			fail("affinity-queued ops must be even");
	}
	return opts;
}

static void make_single_cpu_mask(cpu_set_t *mask, int cpu)
{
	CPU_ZERO(mask);
	CPU_SET(cpu, mask);
}

static void pin_current(int cpu)
{
	cpu_set_t mask;

	make_single_cpu_mask(&mask, cpu);
	if (sched_setaffinity(0, sizeof(mask), &mask))
		die("sched_setaffinity current");
}

static void require_cpu_allowed(const cpu_set_t *mask, int cpu)
{
	if (!CPU_ISSET(cpu, mask)) {
		fprintf(stderr, "CPU %d is outside the original affinity mask\n", cpu);
		exit(EXIT_FAILURE);
	}
}

static int current_nice(void)
{
	int value;

	errno = 0;
	value = getpriority(PRIO_PROCESS, 0);
	if (value == -1 && errno)
		die("getpriority");
	return value;
}

static void verify_current_state(int cpu)
{
	struct sched_param param;

	if (sched_getscheduler(0) != SCHED_OTHER)
		fail("current policy is not SCHED_OTHER");
	if (sched_getparam(0, &param))
		die("sched_getparam");
	if (param.sched_priority != 0)
		fail("current sched_priority is not zero");
	if (current_nice() != 0)
		fail("current nice is not zero");
	if (sched_getcpu() != cpu)
		fail("current CPU does not match the requested CPU");
}

static int ks_sched_setattr(pid_t pid, uint64_t runtime)
{
	struct ks_sched_attr attr = {
		.size = sizeof(attr),
		.sched_policy = SCHED_OTHER,
		.sched_nice = 0,
		.sched_runtime = runtime,
	};

	return (int)syscall(SYS_sched_setattr, pid, &attr, 0);
}

static void wait_atomic_u64(atomic_uint_fast64_t *value, uint64_t expected,
			    atomic_int *thread_error, const char *what)
{
	uint64_t start = now_ns();

	while (atomic_load_explicit(value, memory_order_acquire) < expected) {
		if (thread_error && atomic_load_explicit(thread_error,
						       memory_order_acquire))
			fail(what);
		if (now_ns() - start > WAIT_TIMEOUT_NS)
			fail(what);
		cpu_relax();
	}
}

static void wait_atomic_int(atomic_int *value, int expected,
			    atomic_int *thread_error, const char *what)
{
	uint64_t start = now_ns();

	while (atomic_load_explicit(value, memory_order_acquire) != expected) {
		if (thread_error && atomic_load_explicit(thread_error,
						       memory_order_acquire))
			fail(what);
		if (now_ns() - start > WAIT_TIMEOUT_NS)
			fail(what);
		cpu_relax();
	}
}

static void *yield_peer_main(void *opaque)
{
	struct yield_peer_state *state = opaque;
	unsigned int round;

	set_thread_name("sf-yield-peer");
	atomic_store_explicit(&state->peer_tid, (int)syscall(SYS_gettid),
			      memory_order_release);
	{
		cpu_set_t mask;

		make_single_cpu_mask(&mask, state->cpu);
		if (sched_setaffinity(0, sizeof(mask), &mask)) {
			atomic_store_explicit(&state->thread_error, errno ?: EIO,
					      memory_order_release);
			return NULL;
		}
	}
	for (round = 1; round <= state->total_rounds; round++) {
		uint64_t i, errors = 0;

		atomic_store_explicit(&state->ready_generation, round,
				      memory_order_release);
		wait_atomic_u64(&state->go_generation, round,
				&state->thread_error, "yield peer start timeout");
		for (i = 0; i < state->ops; i++) {
			if (syscall(SYS_sched_yield) != 0)
				errors++;
		}
		atomic_fetch_add_explicit(&state->cumulative_errors, errors,
					  memory_order_relaxed);
		atomic_store_explicit(&state->peer_done_generation, round,
				      memory_order_release);
		wait_atomic_u64(&state->main_done_generation, round,
				&state->thread_error, "yield main completion timeout");
	}
	return NULL;
}

static struct result_row run_yield_solo(uint64_t ops)
{
	struct result_row row = {
		.requested_ops = ops,
		.caller_tid = (pid_t)syscall(SYS_gettid),
	};
	uint64_t i, start = now_ns();

	for (i = 0; i < ops; i++) {
		if (syscall(SYS_sched_yield) == 0)
			row.success_ops++;
		else
			row.errors++;
	}
	row.elapsed_ns = now_ns() - start;
	row.call_ns_sum = row.elapsed_ns;
	row.final_cpu = sched_getcpu();
	return row;
}

static void run_yield_dual(const struct options *opts, unsigned int total_rounds,
			   struct result_row *rows)
{
	struct yield_peer_state state;
	pthread_t peer;
	unsigned int round;
	uint64_t previous_peer_errors = 0;

	memset(&state, 0, sizeof(state));
	atomic_init(&state.ready_generation, 0);
	atomic_init(&state.go_generation, 0);
	atomic_init(&state.peer_done_generation, 0);
	atomic_init(&state.main_done_generation, 0);
	atomic_init(&state.cumulative_errors, 0);
	atomic_init(&state.thread_error, 0);
	atomic_init(&state.peer_tid, 0);
	state.ops = opts->ops;
	state.total_rounds = total_rounds;
	state.cpu = opts->cpu_a;
	if (pthread_create(&peer, NULL, yield_peer_main, &state))
		die("pthread_create yield peer");
	pin_current(opts->cpu_a);
	for (round = 1; round <= total_rounds; round++) {
		struct result_row *row = &rows[round - 1];
		uint64_t i, start, peer_errors;

		wait_atomic_u64(&state.ready_generation, round,
				&state.thread_error, "yield peer readiness timeout");
		start = now_ns();
		atomic_store_explicit(&state.go_generation, round,
				      memory_order_release);
		for (i = 0; i < opts->ops; i++) {
			if (syscall(SYS_sched_yield) == 0)
				row->success_ops++;
			else
				row->errors++;
		}
		atomic_store_explicit(&state.main_done_generation, round,
				      memory_order_release);
		wait_atomic_u64(&state.peer_done_generation, round,
				&state.thread_error, "yield peer completion timeout");
		row->elapsed_ns = now_ns() - start;
		row->call_ns_sum = row->elapsed_ns;
		peer_errors = atomic_load_explicit(&state.cumulative_errors,
						  memory_order_acquire);
		row->errors += peer_errors - previous_peer_errors;
		previous_peer_errors = peer_errors;
		row->requested_ops = 2 * opts->ops;
		row->success_ops = row->requested_ops - row->errors;
		row->caller_tid = (pid_t)syscall(SYS_gettid);
		row->peer_tid = (pid_t)atomic_load_explicit(&state.peer_tid,
							 memory_order_acquire);
		row->final_cpu = sched_getcpu();
	}
	if (pthread_join(peer, NULL))
		die("pthread_join yield peer");
	if (atomic_load_explicit(&state.thread_error, memory_order_acquire))
		fail("yield peer failed");
}

static struct result_row run_setparam(uint64_t ops)
{
	struct result_row row = {
		.requested_ops = ops,
		.caller_tid = (pid_t)syscall(SYS_gettid),
	};
	struct sched_param param = { .sched_priority = 0 };
	uint64_t i, start = now_ns();

	for (i = 0; i < ops; i++) {
		if (syscall(SYS_sched_setparam, 0, &param) == 0)
			row.success_ops++;
		else
			row.errors++;
	}
	row.elapsed_ns = now_ns() - start;
	row.call_ns_sum = row.elapsed_ns;
	row.final_cpu = sched_getcpu();
	return row;
}

static void *affinity_target_main(void *opaque)
{
	struct affinity_state *state = opaque;
	cpu_set_t mask;
	uint64_t heartbeat = 0;

	set_thread_name("sf-target");
	make_single_cpu_mask(&mask, state->target_cpu);
	if (sched_setaffinity(0, sizeof(mask), &mask)) {
		atomic_store_explicit(&state->thread_error, errno ?: EIO,
				      memory_order_release);
		return NULL;
	}
	state->target_tid = (pid_t)syscall(SYS_gettid);
	atomic_store_explicit(&state->target_last_cpu, sched_getcpu(),
			      memory_order_release);
	atomic_store_explicit(&state->target_ready, 1, memory_order_release);
	while (!atomic_load_explicit(&state->stop_target, memory_order_acquire)) {
		heartbeat++;
		if ((heartbeat & 1023) == 0) {
			atomic_store_explicit(&state->target_heartbeat, heartbeat,
					      memory_order_release);
			atomic_store_explicit(&state->target_last_cpu,
					      sched_getcpu(), memory_order_release);
		}
		cpu_relax();
	}
	atomic_store_explicit(&state->target_heartbeat, heartbeat,
			      memory_order_release);
	atomic_store_explicit(&state->target_last_cpu, sched_getcpu(),
			      memory_order_release);
	return NULL;
}

static int set_target_cpu(pid_t tid, int cpu)
{
	cpu_set_t mask;

	make_single_cpu_mask(&mask, cpu);
	return (int)syscall(SYS_sched_setaffinity, tid, sizeof(mask), &mask);
}

static void *affinity_controller_b_main(void *opaque)
{
	struct affinity_state *state = opaque;
	cpu_set_t mask;
	uint64_t i, call_ns = 0, success = 0, errors = 0;

	set_thread_name("sf-ctl-b");
	atomic_store_explicit(&state->controller_b_tid,
			      (int)syscall(SYS_gettid), memory_order_release);
	make_single_cpu_mask(&mask, state->cpu_b);
	if (sched_setaffinity(0, sizeof(mask), &mask)) {
		atomic_store_explicit(&state->thread_error, errno ?: EIO,
				      memory_order_release);
		return NULL;
	}
	atomic_store_explicit(&state->controller_b_ready, 1, memory_order_release);
	for (i = 1; i < state->ops; i += 2) {
		uint64_t start;

		wait_atomic_u64(&state->turn, i, &state->thread_error,
				"affinity controller B turn timeout");
		start = now_ns();
		if (set_target_cpu(state->target_tid, state->cpu_a) == 0)
			success++;
		else
			errors++;
		call_ns += now_ns() - start;
		atomic_store_explicit(&state->turn, i + 1, memory_order_release);
	}
	atomic_store_explicit(&state->controller_b_call_ns, call_ns,
			      memory_order_release);
	atomic_store_explicit(&state->controller_b_success, success,
			      memory_order_release);
	atomic_store_explicit(&state->controller_b_errors, errors,
			      memory_order_release);
	return NULL;
}

static void wait_target_on_cpu(struct affinity_state *state, int cpu,
			       uint64_t heartbeat_before)
{
	uint64_t start = now_ns();

	while (atomic_load_explicit(&state->target_last_cpu,
				   memory_order_acquire) != cpu ||
	       atomic_load_explicit(&state->target_heartbeat,
				   memory_order_acquire) <= heartbeat_before) {
		if (atomic_load_explicit(&state->thread_error,
					memory_order_acquire))
			fail("affinity target failed");
		if (now_ns() - start > WAIT_TIMEOUT_NS)
			fail("affinity target CPU acknowledgement timeout");
		syscall(SYS_sched_yield);
	}
}

static void verify_target_mask(pid_t tid, int cpu)
{
	cpu_set_t expected, observed;

	make_single_cpu_mask(&expected, cpu);
	if (sched_getaffinity(tid, sizeof(observed), &observed))
		die("sched_getaffinity target");
	if (!CPU_EQUAL(&expected, &observed))
		fail("target affinity mask does not match the expected singleton");
}

static struct result_row run_affinity_round(const struct options *opts,
				     bool queued)
{
	struct affinity_state state;
	pthread_t target, controller_b;
	struct result_row row = {
		.requested_ops = opts->ops,
		.caller_tid = (pid_t)syscall(SYS_gettid),
	};
	uint64_t i, start;
	bool has_controller_b = false;

	memset(&state, 0, sizeof(state));
	atomic_init(&state.turn, 0);
	atomic_init(&state.target_heartbeat, 0);
	atomic_init(&state.controller_b_call_ns, 0);
	atomic_init(&state.controller_b_success, 0);
	atomic_init(&state.controller_b_errors, 0);
	atomic_init(&state.target_ready, 0);
	atomic_init(&state.controller_b_ready, 0);
	atomic_init(&state.controller_b_tid, 0);
	atomic_init(&state.target_last_cpu, -1);
	atomic_init(&state.thread_error, 0);
	atomic_init(&state.stop_target, 0);
	state.ops = opts->ops;
	state.cpu_a = opts->cpu_a;
	state.cpu_b = opts->cpu_b;
	state.target_cpu = queued ? opts->cpu_a : opts->cpu_b;
	if (pthread_create(&target, NULL, affinity_target_main, &state))
		die("pthread_create affinity target");
	wait_atomic_int(&state.target_ready, 1, &state.thread_error,
			"affinity target readiness timeout");
	row.target_tid = state.target_tid;
	if (queued) {
		if (pthread_create(&controller_b, NULL,
				   affinity_controller_b_main, &state))
			die("pthread_create affinity controller B");
		has_controller_b = true;
		wait_atomic_int(&state.controller_b_ready, 1,
				&state.thread_error,
				"affinity controller B readiness timeout");
		row.peer_tid = (pid_t)atomic_load_explicit(&state.controller_b_tid,
							memory_order_acquire);
	}
	pin_current(opts->cpu_a);
	row.heartbeat_before = atomic_load_explicit(&state.target_heartbeat,
						    memory_order_acquire);
	start = now_ns();
	if (queued) {
		uint64_t call_ns = 0, success = 0, errors = 0;

		for (i = 0; i < opts->ops; i += 2) {
			uint64_t call_start;

			wait_atomic_u64(&state.turn, i, &state.thread_error,
					"affinity controller A turn timeout");
			call_start = now_ns();
			if (set_target_cpu(state.target_tid, opts->cpu_b) == 0)
				success++;
			else
				errors++;
			call_ns += now_ns() - call_start;
			atomic_store_explicit(&state.turn, i + 1,
					      memory_order_release);
		}
		wait_atomic_u64(&state.turn, opts->ops, &state.thread_error,
				"affinity final turn timeout");
		row.call_ns_sum = call_ns +
			atomic_load_explicit(&state.controller_b_call_ns,
					     memory_order_acquire);
		row.success_ops = success +
			atomic_load_explicit(&state.controller_b_success,
					     memory_order_acquire);
		row.errors = errors +
			atomic_load_explicit(&state.controller_b_errors,
					     memory_order_acquire);
	} else {
		for (i = 0; i < opts->ops; i++) {
			if (set_target_cpu(state.target_tid, state.target_cpu) == 0)
				row.success_ops++;
			else
				row.errors++;
		}
		row.call_ns_sum = now_ns() - start;
	}
	row.elapsed_ns = now_ns() - start;
	if (has_controller_b && pthread_join(controller_b, NULL))
		die("pthread_join affinity controller B");
	verify_target_mask(state.target_tid, state.target_cpu);
	wait_target_on_cpu(&state, state.target_cpu, row.heartbeat_before);
	row.heartbeat_after = atomic_load_explicit(&state.target_heartbeat,
						   memory_order_acquire);
	row.final_cpu = sched_getcpu();
	atomic_store_explicit(&state.stop_target, 1, memory_order_release);
	if (pthread_join(target, NULL))
		die("pthread_join affinity target");
	if (atomic_load_explicit(&state.thread_error, memory_order_acquire))
		fail("affinity worker failed");
	return row;
}

static void validate_row(const struct options *opts, const struct result_row *row)
{
	if (row->errors || row->success_ops != row->requested_ops)
		fail("timed syscall semantic error");
	if (row->final_cpu != opts->cpu_a)
		fail("profile did not finish on CPU A");
	if (is_affinity_profile(opts->profile) &&
	    row->heartbeat_after <= row->heartbeat_before)
		fail("affinity target heartbeat did not advance");
	verify_current_state(opts->cpu_a);
}

static void print_header(void)
{
	printf("profile\tphase\tround\trequested_ops\tsuccess_ops\t"
	       "elapsed_ns\tcall_ns_sum\tns_per_op\terrors\tcpu_a\tcpu_b\t"
	       "caller_tid\tpeer_tid\ttarget_tid\theartbeat_before\t"
	       "heartbeat_after\tfinal_cpu\t"
	       "state_ok\n");
}

static void print_row(const struct options *opts, const char *phase,
		      unsigned int round, const struct result_row *row)
{
	double ns_per_op = (double)row->call_ns_sum /
			   (double)row->success_ops;

	printf("%s\t%s\t%u\t%llu\t%llu\t%llu\t%llu\t%.3f\t%llu\t"
	       "%d\t%d\t%d\t%d\t%d\t%llu\t%llu\t%d\t1\n",
	       opts->profile_name, phase, round,
	       (unsigned long long)row->requested_ops,
	       (unsigned long long)row->success_ops,
	       (unsigned long long)row->elapsed_ns,
	       (unsigned long long)row->call_ns_sum, ns_per_op,
	       (unsigned long long)row->errors, opts->cpu_a, opts->cpu_b,
	       row->caller_tid, row->peer_tid, row->target_tid,
	       (unsigned long long)row->heartbeat_before,
	       (unsigned long long)row->heartbeat_after, row->final_cpu);
	fflush(stdout);
}

int main(int argc, char **argv)
{
	struct options opts = parse_options(argc, argv);
	cpu_set_t original_affinity;
	unsigned int total_rounds = opts.warmups + opts.rounds;
	struct result_row *dual_rows = NULL;
	unsigned int round;

	set_thread_name("sf-main");

	if (sched_getaffinity(0, sizeof(original_affinity), &original_affinity))
		die("sched_getaffinity original");
	require_cpu_allowed(&original_affinity, opts.cpu_a);
	if (opts.cpu_b >= 0)
		require_cpu_allowed(&original_affinity, opts.cpu_b);
	if (opts.profile == PROFILE_YIELD_DUAL) {
		dual_rows = calloc(total_rounds, sizeof(*dual_rows));
		if (!dual_rows)
			die("calloc yield rows");
		run_yield_dual(&opts, total_rounds, dual_rows);
	} else {
		pin_current(opts.cpu_a);
	}
	verify_current_state(opts.cpu_a);
	if (opts.profile == PROFILE_SETPARAM_CURRENT) {
		if (ks_sched_setattr(0, 0))
			die("restore default slice before setparam-current");
	} else if (opts.profile == PROFILE_SETPARAM_FAST_CONTROL) {
		if (ks_sched_setattr(0, 10 * NS_PER_MS))
			die("set custom slice before setparam control");
	}

	print_header();
	for (round = 0; round < total_rounds; round++) {
		struct result_row row;
		const char *phase = round < opts.warmups ? "warmup" : "measured";
		unsigned int visible_round = round < opts.warmups ? round :
					     round - opts.warmups;

		if (opts.profile == PROFILE_YIELD_DUAL) {
			row = dual_rows[round];
		} else if (opts.profile == PROFILE_YIELD_SOLO) {
			row = run_yield_solo(opts.ops);
		} else if (opts.profile == PROFILE_SETPARAM_CURRENT ||
			   opts.profile == PROFILE_SETPARAM_FAST_CONTROL) {
			row = run_setparam(opts.ops);
		} else {
			row = run_affinity_round(&opts,
				opts.profile == PROFILE_AFFINITY_QUEUED);
		}
		validate_row(&opts, &row);
		print_row(&opts, phase, visible_round, &row);
	}
	if (opts.profile == PROFILE_SETPARAM_CURRENT ||
	    opts.profile == PROFILE_SETPARAM_FAST_CONTROL) {
		if (ks_sched_setattr(0, 0))
			die("restore default slice after setparam profile");
	}
	if (sched_setaffinity(0, sizeof(original_affinity), &original_affinity))
		die("restore original affinity");
	free(dual_rows);
	return EXIT_SUCCESS;
}
