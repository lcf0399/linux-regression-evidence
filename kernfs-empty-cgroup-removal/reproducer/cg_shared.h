/* SPDX-License-Identifier: GPL-2.0 */
#ifndef CG_SHARED_H
#define CG_SHARED_H
#define CG_MAX_TASKS 8
#define CG_MAGIC 0x43473101U
struct cg_config {
	unsigned long long root, from, to;
	unsigned int armed, op, weight, deny_after, trace;
};
struct cg_stats {
	unsigned long long created_id, exited_id, weight_id;
	unsigned int init, exit, weight, weight_value, prep, move, cancel, denied, errors;
};
struct cg_task {
	unsigned long long from, to;
	unsigned int prep, move, cancel, denied, errors;
};
struct cg_safety { unsigned int magic, initialized, exited, foreign; int exit_kind; };
#endif
