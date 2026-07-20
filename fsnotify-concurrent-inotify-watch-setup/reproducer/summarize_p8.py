#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-only
import csv
import statistics
import sys
from collections import defaultdict
from pathlib import Path


def fail(message: str) -> None:
    raise SystemExit(f"fsnotify paired summary: {message}")


if len(sys.argv) != 3:
    fail("usage: summarize_p8.py <results.tsv> <summary.tsv>")

results_path = Path(sys.argv[1])
summary_path = Path(sys.argv[2])
rows = list(csv.DictReader(results_path.open(newline=""), delimiter="\t"))
formal = [
    row
    for row in rows
    if row["phase"] == "formal"
]
if not formal:
    fail("no formal rows")
worker_counts = {row.get("workers", "") for row in formal}
if len(worker_counts) != 1:
    fail("formal rows contain different worker counts")
if any(row["semantic_status"] != "PASS" for row in formal):
    fail("a formal row failed its semantic gate")

by_topology: dict[str, list[float]] = defaultdict(list)
by_round: dict[str, dict[str, float]] = defaultdict(dict)
for row in formal:
    topology = row["topology"]
    if topology not in {"distinct", "shared"}:
        fail(f"unexpected topology {topology}")
    value = float(row["metric_value"])
    by_topology[topology].append(value)
    if topology in by_round[row["round"]]:
        fail(f"duplicate {topology} row in round {row['round']}")
    by_round[row["round"]][topology] = value

if set(by_topology) != {"distinct", "shared"}:
    fail("both distinct and shared data are required")
if len(by_topology["distinct"]) != len(by_topology["shared"]):
    fail("distinct/shared sample counts differ")
if any(set(pair) != {"distinct", "shared"} for pair in by_round.values()):
    fail("a formal round is missing its matched pair")


def stats(values: list[float]) -> tuple[int, float, float, float]:
    mean = statistics.fmean(values)
    cv = 0.0 if len(values) < 2 or mean == 0 else statistics.stdev(values) / mean * 100
    return len(values), mean, statistics.median(values), cv


summary_rows: list[tuple[str, str, int, float, float, float]] = []
for topology in ("distinct", "shared"):
    n, mean, median, cv = stats(by_topology[topology])
    summary_rows.append(("pair_worker_ns_per_watch", topology, n, mean, median, cv))

ratios = [pair["distinct"] / pair["shared"] for pair in by_round.values()]
n, mean, median, cv = stats(ratios)
summary_rows.append(("distinct_over_shared_ratio", "paired", n, mean, median, cv))

with summary_path.open("w", newline="") as handle:
    writer = csv.writer(handle, delimiter="\t", lineterminator="\n")
    writer.writerow(("metric", "topology", "n", "mean", "median", "cv_pct"))
    for metric, topology, n, mean, median, cv in summary_rows:
        writer.writerow((metric, topology, n, f"{mean:.9f}", f"{median:.9f}", f"{cv:.6f}"))
