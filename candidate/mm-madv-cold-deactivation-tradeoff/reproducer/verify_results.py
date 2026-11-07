#!/usr/bin/env python3
"""Offline raw-output checks and recomputation; never runs a workload."""
import hashlib
import json
import math
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[1]


def require(condition, explanation):
    if not condition:
        raise ValueError(explanation)


def load(path):
    return json.loads(path.read_text())


def stats(values):
    mean = statistics.mean(values)
    return dict(n=len(values), mean=mean, median=statistics.median(values),
                minimum=min(values), maximum=max(values),
                cv_percent=100 * statistics.stdev(values) / mean)


def collect():
    groups = {}
    count = {"anonymous": 0, "file": 0}
    for path in sorted((ROOT / "measurements/raw").glob("*/*/*.log")):
        batch, point = path.parts[-3:-1]
        text = path.read_text()
        if batch == "anonymous":
            lines = text.splitlines()
            require(len(lines) == 2 and lines[1].startswith("KS_TIMING "), "anonymous framing")
            row, timing = json.loads(lines[0]), json.loads(lines[1][10:])
            case = row["case"]
            require(case in ("vector-self-one", "vector-self-many"), "case")
            require(row["bytes"] == 16 * 1024**2 and row["rounds"] == 2
                    and row["resident_pages"] == 4096 and row["rss_kb"] == 16384
                    and row["returns"] == [16 * 1024**2] * 2 and row["errors"] == [0, 0]
                    and not row["remote"] and not row["denied"]
                    and all(row[k] for k in ("content_pass", "topology_pass",
                                             "unmapped", "joined", "caps_cleared")), "anonymous semantics")
            operation = [r for r in timing["rows"] if r["phase"] == 30]
            require(len(operation) == 1 and operation[0]["count"] == 2, "operation denominator")
            values = {
                "operation_cpu_ns_per_call": operation[0]["cpu_ns"] / 2,
                "operation_wall_ns_per_call": operation[0]["wall_ns"] / 2,
                "main_cpu_ns": timing["main_process_cpu_ns"] + timing["waited_children_cpu_ns"],
                "main_wall_ns": timing["main_wall_ns"],
            }
            for metric, value in values.items():
                groups.setdefault((batch, case + "." + metric, point), []).append(value)
        elif batch == "file":
            row = json.loads(text)
            require(row["schema"] == "cold-file-private-reclaim-timing-v1"
                    and row["bytes"] == 16 * 1024**2 and row["target_resident"] == 4096
                    and row["target_rss_kb"] == 16384 and row["reclaim_requests"] == 1
                    and row["reclaim_bytes"] == 4096 and row["reclaim_errno"] == 0
                    and all(row[k] for k in ("target_executed", "content_pass", "cleanup")), "file semantics")
            require([r["phase"] for r in row["phases"]] == [10, 11, 30, 31], "phase order")
            for state, pair in (("fresh", row["phases"][:2]), ("oldest", row["phases"][2:])):
                for clock in ("cpu_ns", "wall_ns"):
                    for boundary, value in (("interface", pair[0][clock]),
                                            ("drain", pair[1][clock]),
                                            ("including_drain", sum(r[clock] for r in pair))):
                        groups.setdefault((batch, state + "." + boundary + "." + clock, point), []).append(value)
            for clock in ("cpu_ns", "wall_ns"):
                groups.setdefault((batch, "whole_worker." + clock, point), []).append(row["whole_worker_" + clock])
        else:
            raise ValueError("unknown batch")
        count[batch] += 1
    require(count == {"anonymous": 54, "file": 27}, "formal sample inventory")
    require(all(len(v) == 9 for v in groups.values()), "nine samples per point and metric")
    results = {}
    for batch, metric in sorted({(b, m) for b, m, _ in groups}):
        a, c, b = (groups[batch, metric, p] for p in ("parent-a", "child", "parent-b"))
        sa, sc, sb, sp = (stats(v) for v in (a, c, b, a + b))
        results.setdefault(batch, {})[metric] = dict(parent_a=sa, child=sc, parent_b=sb,
            parent_pooled=sp, child_minus_parent=sc["mean"] - sp["mean"],
            child_change_percent=100 * (sc["mean"] / sp["mean"] - 1),
            parent_return_drift_percent=100 * (sb["mean"] / sa["mean"] - 1))
    return count, results


def equal(actual, expected, at=""):
    if isinstance(expected, dict):
        require(set(actual) == set(expected), "keys: " + at)
        for key in expected:
            equal(actual[key], expected[key], at + "/" + key)
    elif isinstance(expected, (float, int)):
        require(math.isclose(actual, expected, rel_tol=1e-12, abs_tol=1e-8), "number: " + at)
    else:
        require(actual == expected, "value: " + at)


def main():
    manifest = load(ROOT / "measurements/files.json")
    for name, digest in manifest.items():
        path = ROOT / name
        require(path.is_file() and hashlib.sha256(path.read_bytes()).hexdigest() == digest, "hash: " + name)
    count, results = collect()
    equal(results, load(ROOT / "measurements/expected-summary.json"))
    print(json.dumps(dict(status="pass", formal_programs=count, controller_imported=False,
        scope="Retained formal output semantics, file identity and arithmetic only; not a new path or machine check.")))


if __name__ == "__main__":
    main()
