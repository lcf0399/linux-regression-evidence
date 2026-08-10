#!/usr/bin/env python3
"""Strict validator for io_uring_nop_round TSV output."""

from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path


INPUT_SHA256 = "9d3695aa28124023d59e7a87ea19ed0069a48d0219198abad1523a9e39b6b233"
PROFILE_NAMES = {
    "plain": "n0_plain_nop",
    "inject": "i0_inject_result",
    "formal": "formal_plain_inject",
    "v7-semantic": "v7_new_flags_semantic",
}


def require(row: dict[str, str], field: str, wanted: str, where: str) -> None:
    if row.get(field) != wanted:
        raise ValueError(f"{where}: {field}={row.get(field)!r}, expected {wanted!r}")


def validate(path: Path, profile: str, phase: str) -> dict[str, object]:
    with path.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    expected_rows = 66 if phase == "measured" else 1
    if len(rows) != expected_rows:
        raise ValueError(f"{path}: expected {expected_rows} rows, got {len(rows)}")

    counts: dict[str, int] = {}
    measured = 0
    for index, row in enumerate(rows):
        where = f"{path}: row {index + 2}"
        require(row, "schema_version", "1", where)
        require(row, "cpu", "2", where)
        require(row, "semantic_pass", "1", where)
        require(row, "input_sha256", INPUT_SHA256, where)
        for field in (
            "validation_failures", "duplicate_cqes", "unexpected_cqes",
            "bad_cqe_flags", "sq_dropped", "cq_overflow", "outstanding",
            "cleanup_failures",
        ):
            require(row, field, "0", where)
        requested = int(row["requested_ops"])
        submitted = int(row["submitted_sqes"])
        completed = int(row["completed_cqes"])
        successful = int(row["successful_ops"])
        if not (requested == submitted == completed == successful):
            raise ValueError(f"{where}: operation accounting mismatch")

        actual_profile = row["profile"]
        counts[actual_profile] = counts.get(actual_profile, 0) + 1
        if phase == "measured":
            require(row, "run_mode", "point", where)
            if row["phase"] == "measured":
                measured += 1
                require(row, "requested_ops", "262144", where)
                require(row, "trace_enabled", "0", where)
                value = float(row["ns_per_op"])
                if not math.isfinite(value) or value <= 0:
                    raise ValueError(f"{where}: invalid ns_per_op={value}")
            elif row["phase"] != "warmup":
                raise ValueError(f"{where}: unexpected phase={row['phase']}")
        elif phase == "direct-hit-trace":
            require(row, "run_mode", "trace", where)
            require(row, "phase", "direct-hit", where)
            require(row, "trace_enabled", "1", where)
            require(row, "requested_ops", "4096", where)
            require(row, "profile", PROFILE_NAMES[profile], where)
        elif phase == "semantic-v7":
            require(row, "run_mode", "semantic", where)
            require(row, "phase", "semantic-v7", where)
            require(row, "profile", PROFILE_NAMES[profile], where)
            require(row, "requested_ops", "9", where)
        else:
            require(row, "run_mode", "smoke", where)
            require(row, "phase", "smoke", where)
            require(row, "requested_ops", "128", where)
            require(row, "profile", PROFILE_NAMES[profile], where)

    if phase == "measured":
        if measured != 60:
            raise ValueError(f"{path}: expected 60 measured rows, got {measured}")
        if counts != {"n0_plain_nop": 33, "i0_inject_result": 33}:
            raise ValueError(f"{path}: unexpected profile row counts: {counts}")
    return {
        "status": "pass",
        "input": str(path),
        "profile": profile,
        "phase": phase,
        "rows": len(rows),
        "measured_rows": measured,
        "profile_rows": counts,
        "kernel_releases": sorted({row["kernel_release"] for row in rows}),
        "boot_ids": sorted({row["boot_id"] for row in rows}),
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--profile", choices=PROFILE_NAMES, required=True)
    parser.add_argument(
        "--phase",
        choices=("semantic-smoke", "direct-hit-trace", "semantic-v7", "measured"),
        required=True,
    )
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = validate(args.input, args.profile, args.phase)
    args.output.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")
    print(f"validation={result['status']}")


if __name__ == "__main__":
    main()
