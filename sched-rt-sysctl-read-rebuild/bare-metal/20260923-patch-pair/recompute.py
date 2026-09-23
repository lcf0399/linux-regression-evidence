#!/usr/bin/env python3
"""Offline verification of this bundle; does not execute or modify the kernel."""

import csv
import hashlib
import json
import math
from pathlib import Path
import re
import statistics

ROOT = Path(__file__).resolve().parent
SLOTS = ("base-a", "patch", "base-b")
CASES = {"period": (1000000, 8), "runtime": (950000, 7), "rr": (100, 4)}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def compare(samples, sensitivity=True):
    points = {
        slot: {"n": len(values), "mean": statistics.mean(values),
               "cv_pct": 100 * statistics.stdev(values) / statistics.mean(values)}
        for slot, values in samples.items()
    }
    midpoint = (points["base-a"]["mean"] + points["base-b"]["mean"]) / 2
    drift = 100 * abs(points["base-a"]["mean"] - points["base-b"]["mean"]) / midpoint
    each = {s: 100 * (1 - points["patch"]["mean"] / points[s]["mean"])
            for s in ("base-a", "base-b")}
    stable = max(p["cv_pct"] for p in points.values()) <= 3 and drift <= 2
    result = dict(points=points, base_midpoint=midpoint, base_drift_pct=drift,
                  patch_reduction_pct=100 * (1 - points["patch"]["mean"] / midpoint),
                  saved_ns=midpoint - points["patch"]["mean"], vs_each_base_pct=each,
                  stable=stable, stable_improvement=stable and min(each.values()) >= 5)
    if sensitivity:
        result["drop_first"] = compare({s: v[1:] for s, v in samples.items()}, False)
        result["stable_improvement"] &= result["drop_first"]["stable_improvement"]
    return result


def equivalent(actual, expected, path="summary"):
    if isinstance(expected, dict):
        require(isinstance(actual, dict) and actual.keys() == expected.keys(), path)
        for key in expected:
            equivalent(actual[key], expected[key], path + "." + key)
    elif type(expected) is float:
        require(math.isfinite(actual) and math.isclose(actual, expected, rel_tol=1e-12,
                                                     abs_tol=1e-10), path)
    else:
        require(type(actual) is type(expected) and actual == expected, path)


def read_tsv(name):
    with (ROOT / name).open(newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def main():
    rows = read_tsv("measurements.tsv")
    inventory = set()
    samples = {c: {m: {s: [] for s in SLOTS} for m in ("wall_ns", "thread_cpu_ns")}
               for c in CASES}
    for row in rows:
        slot, case, number = row["boot"], row["case"], int(row["round"])
        key = (slot, case, number)
        require(slot in SLOTS and case in CASES and 0 <= number < 12, "sample identity")
        require(key not in inventory, "duplicate sample")
        inventory.add(key)
        expected, width = CASES[case]
        require(row["phase"] == ("warmup" if number < 3 else "measured"), "sample phase")
        require(int(row["cpu"]) == 2 and int(row["reads"]) == 4096, "sample shape")
        require(int(row["expected"]) == expected and int(row["bytes_per_read"]) == width
                and int(row["total_bytes"]) == 4096 * width
                and row["semantic_pass"] == "true", "read semantics")
        for metric in samples[case]:
            value = int(row[metric])
            require(value > 0, "non-positive timing")
            if number >= 3:
                samples[case][metric][slot].append((number, value / 4096))
    require(inventory == {(s, c, n) for s in SLOTS for c in CASES for n in range(12)},
            "incomplete samples")
    results = {c: {m + "_per_read": compare({s: [v for _, v in sorted(a)]
                                            for s, a in data.items()})
                   for m, data in metrics.items()} for c, metrics in samples.items()}
    equivalent(results, json.loads((ROOT / "summary.json").read_text()))

    seen = set()
    for row in read_tsv("read-probes.tsv"):
        slot, case, seq = row["boot"], row["case"], int(row["seq"])
        key = (slot, case, seq)
        require(slot in SLOTS and case in CASES and key not in seen and 0 <= seq < 8,
                "read probe inventory")
        seen.add(key)
        rt = int(case != "rr")
        maintenance = int(bool(rt) and slot != "patch")
        want = dict(returned_bytes=CASES[case][1], rt_handler=rt, rr_handler=1-rt,
                    rebuild=maintenance, partition=maintenance, account=maintenance,
                    build=0, locks=rt + maintenance, unlocks=rt + maintenance)
        require(all(int(row[k]) == v for k, v in want.items()), "read probe counts")
    require(len(seen) == 72, "missing read probe")

    checks = read_tsv("write-checks.tsv")
    baseline = {}
    seen = set()
    for row in checks:
        slot, phase, seq = row["boot"], row["phase"], int(row["seq"])
        key = (slot, phase, seq)
        require(slot in SLOTS and phase in ("semantic", "probe") and 0 <= seq < 12
                and key not in seen, "write inventory")
        seen.add(key)
        behavior = {k: row[k] for k in ("name", "kind", "returned_bytes", "errno",
                                        "period", "runtime")}
        require(behavior == baseline.setdefault(seq, behavior), "write behavior differs")
        success = seq < 6
        require(int(row["errno"]) == (0 if success else 22), "write errno")
        if not success:
            require(int(row["period"]) == 1000000 and int(row["runtime"]) == 950000
                    and int(row["returned_bytes"]) == -1, "rejected write changed state")
        if phase == "probe":
            maintenance = int(success or slot != "patch")
            want = dict(rt_handler=1, rebuild=maintenance, partition=maintenance,
                        account=maintenance, update=int(success))
            require(all(int(row[k]) == v for k, v in want.items()), "write probe counts")
        else:
            require(all(row[k] == "NA" for k in ("rt_handler", "rebuild", "partition",
                                                 "account", "update")), "unexpected probe")
    require(len(seen) == 72, "missing write checks")

    identity = json.loads((ROOT / "identity.json").read_text())
    configs = []
    for name in ("base", "patched"):
        content = (ROOT / (name + ".config")).read_bytes()
        build = identity["builds"]["base" if name == "base" else "patch"]
        require(hashlib.sha256(content).hexdigest() == build["config_sha256"], "config hash")
        symbols = {}
        for line in content.decode().splitlines():
            match = re.fullmatch(r"(CONFIG_\w+)=(.*)", line)
            disabled = re.fullmatch(r"# (CONFIG_\w+) is not set", line)
            if match:
                symbols[match[1]] = match[2]
            elif disabled:
                symbols[disabled[1]] = "n"
        symbols.pop("CONFIG_LOCALVERSION")
        configs.append(symbols)
    require(configs[0] == configs[1] and len(configs[0]) == 11069, "config symbols differ")
    for name, value in identity["binaries"].items():
        source = ROOT.parent.parent / "reproducer" / (name + ".c")
        require(hashlib.sha256(source.read_bytes()).hexdigest() == value["source_sha256"],
                "reproducer source hash")
    print("PASS: 108 samples (81 measured), 72 read probes, 72 write checks, configs and sources")
    for case, metrics in results.items():
        row = metrics["wall_ns_per_read"]
        print(f"{case}: reduction={row['patch_reduction_pct']:.6f}%, "
              f"base drift={row['base_drift_pct']:.6f}%")


if __name__ == "__main__":
    main()
