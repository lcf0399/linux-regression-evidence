// SPDX-License-Identifier: GPL-2.0-only
/*
 * fsnotify/inotify connector lifecycle semantic and timing workload.
 *
 * The primary topology case keeps timed syscall counts matched:
 *
 *   distinct: G independent inotify groups -> G distinct inodes
 *   shared:   G independent inotify groups -> G equal-length hardlinks to
 *             one inode whose connector is seeded before timing
 *
 * Each group performs exactly one add and one remove.  The shared keeper mark
 * prevents concurrent first-mark races from creating transient connectors,
 * so the timed shared case has mark/IDR activity but no connector list churn.
 * All fixture creation, group creation, event draining, and cleanup are
 * outside the timed regions.
 */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <inttypes.h>
#include <limits.h>
#include <pthread.h>
#include <sched.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/inotify.h>
#include <sys/stat.h>
#include <sys/statfs.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifndef CLOCK_MONOTONIC_RAW
#define CLOCK_MONOTONIC_RAW CLOCK_MONOTONIC
#endif

#define MAX_WORKERS 64U
#define MAX_ITEMS 1000000UL
#define EVENT_BUFFER_SIZE (64U * 1024U)

enum scenario {
	SCENARIO_TOPOLOGY,
	SCENARIO_BULK,
	SCENARIO_MASK_UPDATE,
	SCENARIO_PATH_LOOKUP,
};

enum topology {
	TOPO_DISTINCT,
	TOPO_SHARED,
};

struct config {
	const char *test_dir;
	enum scenario scenario;
	const char *scenario_name;
	enum topology topology;
	const char *topology_name;
	unsigned int workers;
	unsigned long items;
	bool keep_files;
	bool have_cpu_list;
	int cpus[MAX_WORKERS];
};

struct event_counts {
	unsigned long ignored;
	unsigned long overflow;
	unsigned long unexpected;
};

struct worker_result {
	uint64_t add_elapsed_ns;
	uint64_t remove_elapsed_ns;
	unsigned long add_success;
	unsigned long remove_success;
	unsigned long unexpected;
	int affinity_ok;
};

struct topology_run {
	const struct config *cfg;
	char **paths;
	int *group_fds;
	int *wds;
	pthread_barrier_t barrier;
	struct worker_result *results;
};

struct worker_arg {
	struct topology_run *run;
	unsigned int worker;
	unsigned long begin;
	unsigned long end;
};

struct inode_key {
	dev_t dev;
	ino_t ino;
};

static char cleanup_root[PATH_MAX];
static bool cleanup_enabled;

static void die(const char *what)
{
	perror(what);
	exit(1);
}

static void die_msg(const char *msg)
{
	fprintf(stderr, "%s\n", msg);
	exit(2);
}

static void *xcalloc(size_t n, size_t size)
{
	void *ptr = calloc(n, size);

	if (!ptr)
		die("calloc");
	return ptr;
}

static char *xasprintf(const char *fmt, const char *a, unsigned long value)
{
	char *result = NULL;

	if (asprintf(&result, fmt, a, value) < 0)
		die("asprintf");
	return result;
}

static uint64_t now_ns(void)
{
	struct timespec ts;

	if (clock_gettime(CLOCK_MONOTONIC_RAW, &ts) != 0) {
		if (errno != EINVAL || clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
			die("clock_gettime");
	}
	return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

static unsigned long parse_ulong(const char *name, const char *value,
				 unsigned long fallback)
{
	char *end = NULL;
	unsigned long parsed;

	if (!value || !*value)
		return fallback;
	errno = 0;
	parsed = strtoul(value, &end, 0);
	if (errno || !end || *end != '\0' || parsed == 0) {
		fprintf(stderr, "invalid %s=%s\n", name, value);
		exit(2);
	}
	return parsed;
}

static bool env_bool(const char *name, bool fallback)
{
	const char *value = getenv(name);

	if (!value || !*value)
		return fallback;
	if (!strcmp(value, "1") || !strcasecmp(value, "true") ||
	    !strcasecmp(value, "yes"))
		return true;
	if (!strcmp(value, "0") || !strcasecmp(value, "false") ||
	    !strcasecmp(value, "no"))
		return false;
	fprintf(stderr, "invalid %s=%s\n", name, value);
	exit(2);
}

static unsigned long read_ulong_file(const char *path)
{
	FILE *stream;
	unsigned long value;

	stream = fopen(path, "r");
	if (!stream)
		die(path);
	if (fscanf(stream, "%lu", &value) != 1) {
		fclose(stream);
		die_msg("failed to parse sysctl value");
	}
	if (fclose(stream) != 0)
		die("fclose sysctl");
	return value;
}

static enum scenario parse_scenario(const char *value, const char **name)
{
	if (!value || !*value || !strcmp(value, "topology")) {
		*name = "connector_topology";
		return SCENARIO_TOPOLOGY;
	}
	if (!strcmp(value, "bulk")) {
		*name = "bulk_directory_watch";
		return SCENARIO_BULK;
	}
	if (!strcmp(value, "mask-update")) {
		*name = "existing_mask_update";
		return SCENARIO_MASK_UPDATE;
	}
	if (!strcmp(value, "path-lookup")) {
		*name = "path_lookup_control";
		return SCENARIO_PATH_LOOKUP;
	}
	fprintf(stderr, "invalid SCENARIO=%s\n", value);
	exit(2);
}

static enum topology parse_topology(const char *value, const char **name)
{
	if (!value || !*value || !strcmp(value, "distinct")) {
		*name = "distinct";
		return TOPO_DISTINCT;
	}
	if (!strcmp(value, "shared")) {
		*name = "shared";
		return TOPO_SHARED;
	}
	fprintf(stderr, "invalid TOPOLOGY=%s\n", value);
	exit(2);
}

static void parse_cpu_list(struct config *cfg)
{
	const char *value = getenv("CPU_LIST");
	char *copy, *save = NULL, *token;
	unsigned int count = 0;

	if (!value || !*value)
		return;
	copy = strdup(value);
	if (!copy)
		die("strdup CPU_LIST");
	for (token = strtok_r(copy, ",", &save); token;
	     token = strtok_r(NULL, ",", &save)) {
		char *end = NULL;
		unsigned long cpu;

		if (count >= cfg->workers)
			die_msg("CPU_LIST has more entries than WORKERS");
		errno = 0;
		cpu = strtoul(token, &end, 0);
		if (errno || !end || *end != '\0' || cpu >= CPU_SETSIZE)
			die_msg("invalid CPU_LIST item");
		cfg->cpus[count++] = (int)cpu;
	}
	free(copy);
	if (count != cfg->workers)
		die_msg("CPU_LIST must contain exactly WORKERS entries");
	cfg->have_cpu_list = true;
}

static void load_config(struct config *cfg)
{
	memset(cfg, 0, sizeof(*cfg));
	cfg->test_dir = getenv("TEST_DIR");
	if (!cfg->test_dir || cfg->test_dir[0] != '/')
		die_msg("TEST_DIR must be an existing absolute directory");
	cfg->scenario = parse_scenario(getenv("SCENARIO"), &cfg->scenario_name);
	cfg->topology = parse_topology(getenv("TOPOLOGY"), &cfg->topology_name);
	cfg->workers = (unsigned int)parse_ulong("WORKERS", getenv("WORKERS"), 1);
	cfg->items = parse_ulong("ITEMS", getenv("ITEMS"),
				 cfg->scenario == SCENARIO_TOPOLOGY ? 32 : 1024);
	cfg->keep_files = env_bool("KEEP_FILES", false);
	if (cfg->workers == 0 || cfg->workers > MAX_WORKERS)
		die_msg("WORKERS outside supported range");
	if (cfg->items == 0 || cfg->items > MAX_ITEMS)
		die_msg("ITEMS outside supported range");
	if (cfg->scenario != SCENARIO_TOPOLOGY && cfg->workers != 1)
		die_msg("only SCENARIO=topology supports WORKERS > 1");
	if (cfg->scenario == SCENARIO_TOPOLOGY && cfg->workers > cfg->items)
		die_msg("WORKERS must not exceed topology group count (ITEMS)");
	parse_cpu_list(cfg);
}

static int cleanup_cb(const char *path, const struct stat *sb, int type,
		      struct FTW *ftwbuf)
{
	(void)sb;
	(void)type;
	(void)ftwbuf;
	return remove(path);
}

static void cleanup_fixture(void)
{
	if (cleanup_enabled && cleanup_root[0])
		nftw(cleanup_root, cleanup_cb, 32, FTW_DEPTH | FTW_PHYS);
}

static void make_fixture_root(const struct config *cfg, const char *tag)
{
	struct stat st;
	char template[PATH_MAX];
	char *created;

	if (stat(cfg->test_dir, &st) != 0)
		die("stat TEST_DIR");
	if (!S_ISDIR(st.st_mode))
		die_msg("TEST_DIR is not a directory");
	if (snprintf(template, sizeof(template), "%s/fsnotify-%s.XXXXXX",
		     cfg->test_dir, tag) >= (int)sizeof(template))
		die_msg("fixture path too long");
	created = mkdtemp(template);
	if (!created)
		die("mkdtemp");
	if (snprintf(cleanup_root, sizeof(cleanup_root), "%s", created) >=
	    (int)sizeof(cleanup_root))
		die_msg("cleanup path too long");
	cleanup_enabled = !cfg->keep_files;
	if (atexit(cleanup_fixture) != 0)
		die_msg("atexit failed");
}

static void make_dir(const char *path)
{
	if (mkdir(path, 0700) != 0)
		die(path);
}

static void make_file(const char *path)
{
	int fd = open(path, O_CREAT | O_EXCL | O_RDWR | O_CLOEXEC, 0600);

	if (fd < 0)
		die(path);
	if (close(fd) != 0)
		die("close fixture file");
}

static int inode_key_cmp(const void *left, const void *right)
{
	const struct inode_key *a = left;
	const struct inode_key *b = right;

	if (a->dev < b->dev)
		return -1;
	if (a->dev > b->dev)
		return 1;
	if (a->ino < b->ino)
		return -1;
	if (a->ino > b->ino)
		return 1;
	return 0;
}

static int int_cmp(const void *left, const void *right)
{
	const int a = *(const int *)left;
	const int b = *(const int *)right;

	return (a > b) - (a < b);
}

static bool verify_inode_shape(char **paths, unsigned long items,
			       enum topology topology)
{
	struct inode_key *keys = xcalloc(items, sizeof(*keys));
	unsigned long i;
	bool ok = true;

	for (i = 0; i < items; i++) {
		struct stat st;

		if (stat(paths[i], &st) != 0)
			die("stat fixture path");
		keys[i].dev = st.st_dev;
		keys[i].ino = st.st_ino;
	}
	qsort(keys, items, sizeof(*keys), inode_key_cmp);
	for (i = 1; i < items; i++) {
		bool equal = keys[i - 1].dev == keys[i].dev &&
			     keys[i - 1].ino == keys[i].ino;

		if ((topology == TOPO_DISTINCT && equal) ||
		    (topology == TOPO_SHARED && !equal)) {
			ok = false;
			break;
		}
	}
	free(keys);
	return ok;
}

static int pin_current_thread(int cpu)
{
	cpu_set_t set;

	CPU_ZERO(&set);
	CPU_SET(cpu, &set);
	return sched_setaffinity(0, sizeof(set), &set) == 0;
}

static struct event_counts drain_events(int fd)
{
	struct event_counts counts = { 0 };
	char *buffer = xcalloc(1, EVENT_BUFFER_SIZE);

	for (;;) {
		ssize_t bytes = read(fd, buffer, EVENT_BUFFER_SIZE);
		char *cursor;

		if (bytes < 0) {
			if (errno == EINTR)
				continue;
			if (errno == EAGAIN)
				break;
			die("read inotify events");
		}
		if (bytes == 0)
			break;
		for (cursor = buffer; cursor < buffer + bytes;) {
			struct inotify_event *event = (struct inotify_event *)cursor;
			size_t size = sizeof(*event) + event->len;

			if (cursor + size > buffer + bytes)
				die_msg("truncated inotify event");
			if (event->mask & IN_Q_OVERFLOW)
				counts.overflow++;
			else if (event->mask & IN_IGNORED)
				counts.ignored++;
			else
				counts.unexpected++;
			cursor += size;
		}
	}
	free(buffer);
	return counts;
}

static void *topology_worker(void *opaque)
{
	struct worker_arg *arg = opaque;
	struct topology_run *run = arg->run;
	struct worker_result *result = &run->results[arg->worker];
	unsigned long i;
	uint64_t start;

	result->affinity_ok = !run->cfg->have_cpu_list ||
		pin_current_thread(run->cfg->cpus[arg->worker]);
	pthread_barrier_wait(&run->barrier);
	start = now_ns();
	for (i = arg->begin; i < arg->end; i++) {
		int wd = inotify_add_watch(run->group_fds[i], run->paths[i],
					   IN_ATTRIB);

		run->wds[i] = wd;
		if (wd >= 0)
			result->add_success++;
		else
			result->unexpected++;
	}
	result->add_elapsed_ns = now_ns() - start;
	pthread_barrier_wait(&run->barrier);
	start = now_ns();
	for (i = arg->begin; i < arg->end; i++) {
		if (run->wds[i] >= 0 &&
		    inotify_rm_watch(run->group_fds[i], run->wds[i]) == 0)
			result->remove_success++;
		else
			result->unexpected++;
	}
	result->remove_elapsed_ns = now_ns() - start;
	pthread_barrier_wait(&run->barrier);
	return NULL;
}

static void print_common_prefix(const struct config *cfg, const char *topology,
				unsigned long groups, unsigned long paths,
				unsigned long inodes, unsigned long marks,
				unsigned long expected_connectors,
				unsigned long expected_timed_churn,
				unsigned long max_instances,
				unsigned long max_watches,
				unsigned long max_queue,
				long fs_type)
{
	unsigned int i;

	printf("result scenario=%s topology=%s filesystem_magic=0x%lx "
	       "workers=%u cpu_list=", cfg->scenario_name, topology, fs_type,
	       cfg->workers);
	if (!cfg->have_cpu_list) {
		printf("none");
	} else {
		for (i = 0; i < cfg->workers; i++)
			printf("%s%d", i ? "," : "", cfg->cpus[i]);
	}
	printf(" groups=%lu paths=%lu inode_count=%lu marks=%lu "
	       "expected_connectors=%lu expected_timed_connector_churn=%lu "
	       "sysctl_max_instances=%lu sysctl_max_watches=%lu "
	       "sysctl_max_queue=%lu ", groups, paths, inodes, marks,
	       expected_connectors, expected_timed_churn, max_instances,
	       max_watches, max_queue);
}

static int run_topology(const struct config *cfg)
{
	const unsigned long groups = cfg->items;
	unsigned long max_instances, max_watches, max_queue, i;
	char data_dir[PATH_MAX], keeper_path[PATH_MAX];
	struct topology_run run = { .cfg = cfg };
	struct worker_arg *args;
	pthread_t *threads;
	struct statfs fs;
	int keeper_fd = -1, keeper_wd = -1;
	unsigned long add_success = 0, remove_success = 0, unexpected = 0;
	unsigned long ignored = 0, overflow = 0, event_unexpected = 0;
	uint64_t add_max = 0, remove_max = 0, add_sum = 0, remove_sum = 0;
	int affinity_ok = 1, semantic_ok;

	max_instances = read_ulong_file("/proc/sys/fs/inotify/max_user_instances");
	max_watches = read_ulong_file("/proc/sys/fs/inotify/max_user_watches");
	max_queue = read_ulong_file("/proc/sys/fs/inotify/max_queued_events");
	if (groups + (cfg->topology == TOPO_SHARED ? 1 : 0) + 8 >
	    max_instances)
		die_msg("topology groups must leave an eight-instance sysctl margin");
	if (groups + 1 > max_watches)
		die_msg("topology watch count exceeds max_user_watches");
	make_fixture_root(cfg, "topology");
	if (snprintf(data_dir, sizeof(data_dir), "%s/p", cleanup_root) >=
	    (int)sizeof(data_dir))
		die_msg("fixture data path too long");
	make_dir(data_dir);
	run.paths = xcalloc(groups, sizeof(*run.paths));
	run.group_fds = xcalloc(groups, sizeof(*run.group_fds));
	run.wds = xcalloc(groups, sizeof(*run.wds));
	for (i = 0; i < groups; i++) {
		run.paths[i] = xasprintf("%s/f-%08lu", data_dir, i);
		run.group_fds[i] = -1;
		run.wds[i] = -1;
	}
	if (cfg->topology == TOPO_SHARED) {
		if (snprintf(keeper_path, sizeof(keeper_path), "%s/keeper", data_dir) >=
		    (int)sizeof(keeper_path))
			die_msg("keeper path too long");
		make_file(keeper_path);
		for (i = 0; i < groups; i++)
			if (link(keeper_path, run.paths[i]) != 0)
				die("link shared fixture");
	} else {
		for (i = 0; i < groups; i++)
			make_file(run.paths[i]);
	}
	if (!verify_inode_shape(run.paths, groups, cfg->topology))
		die_msg("fixture inode topology verification failed");
	for (i = 0; i < groups; i++) {
		struct stat st;

		if (stat(run.paths[i], &st) != 0)
			die("warm stat fixture");
		run.group_fds[i] = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
		if (run.group_fds[i] < 0)
			die("inotify_init1 topology group");
	}
	if (cfg->topology == TOPO_SHARED) {
		keeper_fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
		if (keeper_fd < 0)
			die("inotify_init1 keeper");
		keeper_wd = inotify_add_watch(keeper_fd, keeper_path, IN_ATTRIB);
		if (keeper_wd < 0)
			die("inotify_add_watch keeper");
	}
	if (statfs(cleanup_root, &fs) != 0)
		die("statfs fixture");
	run.results = xcalloc(cfg->workers, sizeof(*run.results));
	args = xcalloc(cfg->workers, sizeof(*args));
	threads = xcalloc(cfg->workers, sizeof(*threads));
	if (pthread_barrier_init(&run.barrier, NULL, cfg->workers) != 0)
		die_msg("pthread_barrier_init failed");
	for (i = 0; i < cfg->workers; i++) {
		args[i].run = &run;
		args[i].worker = (unsigned int)i;
		args[i].begin = groups * i / cfg->workers;
		args[i].end = groups * (i + 1) / cfg->workers;
		if (pthread_create(&threads[i], NULL, topology_worker, &args[i]) != 0)
			die_msg("pthread_create failed");
	}
	for (i = 0; i < cfg->workers; i++) {
		struct worker_result *result = &run.results[i];

		if (pthread_join(threads[i], NULL) != 0)
			die_msg("pthread_join failed");
		add_success += result->add_success;
		remove_success += result->remove_success;
		unexpected += result->unexpected;
		add_sum += result->add_elapsed_ns;
		remove_sum += result->remove_elapsed_ns;
		if (result->add_elapsed_ns > add_max)
			add_max = result->add_elapsed_ns;
		if (result->remove_elapsed_ns > remove_max)
			remove_max = result->remove_elapsed_ns;
		if (!result->affinity_ok)
			affinity_ok = 0;
	}
	for (i = 0; i < groups; i++) {
		struct event_counts counts = drain_events(run.group_fds[i]);

		ignored += counts.ignored;
		overflow += counts.overflow;
		event_unexpected += counts.unexpected;
		if (close(run.group_fds[i]) != 0)
			die("close topology group");
	}
	if (cfg->topology == TOPO_SHARED) {
		struct event_counts counts;

		if (inotify_rm_watch(keeper_fd, keeper_wd) != 0)
			die("inotify_rm_watch keeper");
		counts = drain_events(keeper_fd);
		if (counts.ignored != 1 || counts.overflow || counts.unexpected)
			unexpected++;
		if (close(keeper_fd) != 0)
			die("close keeper group");
	}
	semantic_ok = add_success == groups && remove_success == groups &&
		ignored == groups && overflow == 0 && event_unexpected == 0 &&
		unexpected == 0 && affinity_ok;
	print_common_prefix(cfg, cfg->topology_name, groups, groups,
			    cfg->topology == TOPO_DISTINCT ? groups : 1,
			    groups, cfg->topology == TOPO_DISTINCT ? groups : 1,
			    cfg->topology == TOPO_DISTINCT ? groups : 0,
			    max_instances, max_watches, max_queue, fs.f_type);
	printf("setup_keeper_marks=%d operations=%lu add_success=%lu remove_success=%lu "
	       "ignored_events=%lu overflow_events=%lu unexpected_results=%lu "
	       "affinity_ok=%d add_wall_ns=%" PRIu64 " remove_wall_ns=%" PRIu64 " "
	       "add_worker_ns_sum=%" PRIu64 " remove_worker_ns_sum=%" PRIu64 " "
	       "add_wall_ns_per_watch=%.3f remove_wall_ns_per_watch=%.3f "
	       "add_worker_ns_per_watch=%.3f remove_worker_ns_per_watch=%.3f "
	       "semantic_status=%s\n",
	       cfg->topology == TOPO_SHARED ? 1 : 0, groups, add_success,
	       remove_success, ignored, overflow,
	       unexpected + event_unexpected, affinity_ok, add_max, remove_max,
	       add_sum, remove_sum, (double)add_max / groups,
	       (double)remove_max / groups, (double)add_sum / groups,
	       (double)remove_sum / groups, semantic_ok ? "PASS" : "FAIL");
	pthread_barrier_destroy(&run.barrier);
	for (i = 0; i < groups; i++)
		free(run.paths[i]);
	free(run.paths);
	free(run.group_fds);
	free(run.wds);
	free(run.results);
	free(args);
	free(threads);
	return semantic_ok ? 0 : 1;
}

static bool unique_wds(const int *wds, unsigned long items)
{
	int *copy = xcalloc(items, sizeof(*copy));
	unsigned long i;
	bool unique = true;

	memcpy(copy, wds, items * sizeof(*copy));
	qsort(copy, items, sizeof(*copy), int_cmp);
	for (i = 1; i < items; i++)
		if (copy[i] == copy[i - 1])
			unique = false;
	free(copy);
	return unique;
}

static int run_bulk(const struct config *cfg)
{
	unsigned long max_instances, max_watches, max_queue, i;
	char data_dir[PATH_MAX];
	char **paths;
	int *wds, fd, affinity_ok, semantic_ok;
	unsigned long add_success = 0, remove_success = 0, unexpected = 0;
	uint64_t add_elapsed, remove_elapsed, start;
	struct event_counts counts;
	struct statfs fs;

	max_instances = read_ulong_file("/proc/sys/fs/inotify/max_user_instances");
	max_watches = read_ulong_file("/proc/sys/fs/inotify/max_user_watches");
	max_queue = read_ulong_file("/proc/sys/fs/inotify/max_queued_events");
	if (cfg->items + 8 > max_watches)
		die_msg("bulk item count must leave an eight-watch sysctl margin");
	if (cfg->items >= max_queue)
		die_msg("bulk item count must stay below max_queued_events for IN_IGNORED drain");
	make_fixture_root(cfg, "bulk");
	if (snprintf(data_dir, sizeof(data_dir), "%s/d", cleanup_root) >=
	    (int)sizeof(data_dir))
		die_msg("bulk data path too long");
	make_dir(data_dir);
	paths = xcalloc(cfg->items, sizeof(*paths));
	wds = xcalloc(cfg->items, sizeof(*wds));
	for (i = 0; i < cfg->items; i++) {
		struct stat st;

		paths[i] = xasprintf("%s/d-%08lu", data_dir, i);
		make_dir(paths[i]);
		if (stat(paths[i], &st) != 0)
			die("warm stat bulk path");
	}
	if (statfs(cleanup_root, &fs) != 0)
		die("statfs bulk fixture");
	affinity_ok = !cfg->have_cpu_list || pin_current_thread(cfg->cpus[0]);
	fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (fd < 0)
		die("inotify_init1 bulk");
	start = now_ns();
	for (i = 0; i < cfg->items; i++) {
		wds[i] = inotify_add_watch(fd, paths[i],
					    IN_CREATE | IN_DELETE |
					    IN_MOVED_FROM | IN_MOVED_TO);
		if (wds[i] >= 0)
			add_success++;
		else
			unexpected++;
	}
	add_elapsed = now_ns() - start;
	if (!unique_wds(wds, cfg->items))
		unexpected++;
	start = now_ns();
	for (i = 0; i < cfg->items; i++) {
		if (wds[i] >= 0 && inotify_rm_watch(fd, wds[i]) == 0)
			remove_success++;
		else
			unexpected++;
	}
	remove_elapsed = now_ns() - start;
	counts = drain_events(fd);
	if (close(fd) != 0)
		die("close bulk group");
	semantic_ok = add_success == cfg->items &&
		remove_success == cfg->items && counts.ignored == cfg->items &&
		counts.overflow == 0 && counts.unexpected == 0 &&
		unexpected == 0 && affinity_ok;
	print_common_prefix(cfg, "natural", 1, cfg->items, cfg->items,
			    cfg->items, cfg->items, cfg->items,
			    max_instances, max_watches, max_queue, fs.f_type);
	printf("operations=%lu add_success=%lu remove_success=%lu "
	       "ignored_events=%lu overflow_events=%lu unexpected_results=%lu "
	       "affinity_ok=%d add_elapsed_ns=%" PRIu64 " remove_elapsed_ns=%" PRIu64 " "
	       "add_ns_per_watch=%.3f remove_ns_per_watch=%.3f semantic_status=%s\n",
	       cfg->items, add_success, remove_success, counts.ignored,
	       counts.overflow, unexpected + counts.unexpected, affinity_ok,
	       add_elapsed, remove_elapsed, (double)add_elapsed / cfg->items,
	       (double)remove_elapsed / cfg->items,
	       semantic_ok ? "PASS" : "FAIL");
	for (i = 0; i < cfg->items; i++)
		free(paths[i]);
	free(paths);
	free(wds);
	return semantic_ok ? 0 : 1;
}

static int setup_single_file(const struct config *cfg, const char *tag,
			     char *path, size_t path_size, struct statfs *fs)
{
	make_fixture_root(cfg, tag);
	if (snprintf(path, path_size, "%s/target", cleanup_root) >=
	    (int)path_size)
		die_msg("single-file fixture path too long");
	make_file(path);
	if (statfs(cleanup_root, fs) != 0)
		die("statfs single-file fixture");
	return !cfg->have_cpu_list || pin_current_thread(cfg->cpus[0]);
}

static int run_mask_update(const struct config *cfg)
{
	unsigned long max_instances, max_watches, max_queue, i, success = 0;
	unsigned long unexpected = 0;
	char path[PATH_MAX];
	struct statfs fs;
	struct event_counts counts;
	uint64_t start, elapsed;
	int fd, wd, affinity_ok, semantic_ok;

	max_instances = read_ulong_file("/proc/sys/fs/inotify/max_user_instances");
	max_watches = read_ulong_file("/proc/sys/fs/inotify/max_user_watches");
	max_queue = read_ulong_file("/proc/sys/fs/inotify/max_queued_events");
	affinity_ok = setup_single_file(cfg, "mask", path, sizeof(path), &fs);
	fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
	if (fd < 0)
		die("inotify_init1 mask update");
	wd = inotify_add_watch(fd, path, IN_ATTRIB);
	if (wd < 0)
		die("initial inotify_add_watch mask update");
	if (inotify_add_watch(fd, path, IN_MODIFY) != wd ||
	    inotify_add_watch(fd, path, IN_ATTRIB) != wd)
		die_msg("mask-update warmup returned a different wd");
	start = now_ns();
	for (i = 0; i < cfg->items; i++) {
		uint32_t mask = i & 1 ? IN_ATTRIB : IN_MODIFY;
		int ret = inotify_add_watch(fd, path, mask);

		if (ret == wd)
			success++;
		else
			unexpected++;
	}
	elapsed = now_ns() - start;
	if (inotify_rm_watch(fd, wd) != 0)
		unexpected++;
	counts = drain_events(fd);
	if (close(fd) != 0)
		die("close mask-update group");
	semantic_ok = success == cfg->items && unexpected == 0 &&
		counts.ignored == 1 && counts.overflow == 0 &&
		counts.unexpected == 0 && affinity_ok;
	print_common_prefix(cfg, "existing", 1, 1, 1, 1, 1, 0,
			    max_instances, max_watches, max_queue, fs.f_type);
	printf("operations=%lu update_success=%lu ignored_events=%lu "
	       "overflow_events=%lu unexpected_results=%lu affinity_ok=%d "
	       "elapsed_ns=%" PRIu64 " update_ns_per_call=%.3f semantic_status=%s\n",
	       cfg->items, success, counts.ignored, counts.overflow,
	       unexpected + counts.unexpected, affinity_ok, elapsed,
	       (double)elapsed / cfg->items, semantic_ok ? "PASS" : "FAIL");
	return semantic_ok ? 0 : 1;
}

static int run_path_lookup(const struct config *cfg)
{
	unsigned long max_instances, max_watches, max_queue, i, success = 0;
	unsigned long unexpected = 0;
	char path[PATH_MAX];
	struct statfs fs;
	uint64_t start, elapsed;
	int affinity_ok, semantic_ok;

	max_instances = read_ulong_file("/proc/sys/fs/inotify/max_user_instances");
	max_watches = read_ulong_file("/proc/sys/fs/inotify/max_user_watches");
	max_queue = read_ulong_file("/proc/sys/fs/inotify/max_queued_events");
	affinity_ok = setup_single_file(cfg, "lookup", path, sizeof(path), &fs);
	for (i = 0; i < 16; i++) {
		int fd = open(path, O_PATH | O_CLOEXEC);

		if (fd < 0)
			die("path lookup warmup");
		close(fd);
	}
	start = now_ns();
	for (i = 0; i < cfg->items; i++) {
		int fd = open(path, O_PATH | O_CLOEXEC);

		if (fd >= 0) {
			success++;
			if (close(fd) != 0)
				unexpected++;
		} else {
			unexpected++;
		}
	}
	elapsed = now_ns() - start;
	semantic_ok = success == cfg->items && unexpected == 0 && affinity_ok;
	print_common_prefix(cfg, "none", 0, 1, 1, 0, 0, 0,
			    max_instances, max_watches, max_queue, fs.f_type);
	printf("operations=%lu lookup_success=%lu unexpected_results=%lu "
	       "affinity_ok=%d elapsed_ns=%" PRIu64 " lookup_ns_per_call=%.3f "
	       "semantic_status=%s\n", cfg->items, success, unexpected,
	       affinity_ok, elapsed, (double)elapsed / cfg->items,
	       semantic_ok ? "PASS" : "FAIL");
	return semantic_ok ? 0 : 1;
}

int main(void)
{
	struct config cfg;

	load_config(&cfg);
	switch (cfg.scenario) {
	case SCENARIO_TOPOLOGY:
		return run_topology(&cfg);
	case SCENARIO_BULK:
		return run_bulk(&cfg);
	case SCENARIO_MASK_UPDATE:
		return run_mask_update(&cfg);
	case SCENARIO_PATH_LOOKUP:
		return run_path_lookup(&cfg);
	}
	return 2;
}
