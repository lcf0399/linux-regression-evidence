#!/usr/bin/env python3
"""Validate msg_ring result schema and semantic gates, never performance."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


PROFILES = {
    "d0": "D0_DIRECT_DATA",
    "r0": "R0_DEFERRED_REMOTE_DATA",
    "f0": "F0_DIRECT_SEND_FD",
}
OPS = {
    "d0": {"semantic-smoke": 1024, "direct-hit-trace": 512, "measured": 262144},
    "r0": {"semantic-smoke": 1024, "direct-hit-trace": 512, "measured": 262144},
    "f0": {"semantic-smoke": 256, "direct-hit-trace": 128, "measured": 4096},
}
ZERO_FIELDS = (
    "bad_results",
    "bad_flags",
    "bad_user_data",
    "duplicate_events",
    "missing_events",
    "pair_mismatches",
    "sentinel_mismatches",
    "timeouts",
    "cleanup_failures",
    "source_dropped",
    "source_overflow",
    "target_dropped",
    "target_overflow",
    "source_outstanding",
    "target_outstanding",
)


def value(row: dict[str, str], field: str, errors: list[str], index: int) -> int:
    try:
        return int(row[field])
    except (KeyError, TypeError, ValueError):
        errors.append(f"row{index}:{field}=missing_or_invalid")
        return -1


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--profile", required=True, choices=PROFILES)
    parser.add_argument(
        "--phase",
        required=True,
        choices=("semantic-smoke", "direct-hit-trace", "measured"),
    )
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    lines = [
        line
        for line in args.input.read_text().splitlines()
        if line and not line.startswith("#")
    ]
    rows = list(csv.DictReader(lines, delimiter="\t"))
    expected_rows = 15 if args.phase == "measured" else 1
    expected_ops = OPS[args.profile][args.phase]
    expected_batches = expected_ops // 64
    errors: list[str] = []

    if len(rows) != expected_rows:
        errors.append(f"row_count={len(rows)} expected={expected_rows}")
    for index, row in enumerate(rows):
        if row.get("schema") != "1":
            errors.append(f"row{index}:schema={row.get('schema')}")
        if row.get("profile") != PROFILES[args.profile]:
            errors.append(f"row{index}:profile={row.get('profile')}")
        if row.get("phase") != args.phase:
            errors.append(f"row{index}:phase={row.get('phase')}")
        if row.get("semantic_pass") != "1":
            errors.append(f"row{index}:semantic_pass={row.get('semantic_pass')}")
        if value(row, "round", errors, index) != index:
            errors.append(f"row{index}:round={row.get('round')}")
        for field in ("logical_ops", "sqes", "source_cqes", "target_cqes", "successful_ops"):
            if value(row, field, errors, index) != expected_ops:
                errors.append(f"row{index}:{field}={row.get(field)} expected={expected_ops}")
        if value(row, "source_enters", errors, index) != expected_batches:
            errors.append(
                f"row{index}:source_enters={row.get('source_enters')} expected={expected_batches}"
            )
        expected_target_enters = expected_batches if args.profile == "r0" else 0
        if value(row, "target_enters", errors, index) != expected_target_enters:
            errors.append(
                f"row{index}:target_enters={row.get('target_enters')} expected={expected_target_enters}"
            )
        if args.profile == "f0":
            for field in ("verify_reads", "slots_installed", "slots_verified"):
                if value(row, field, errors, index) != expected_ops:
                    errors.append(f"row{index}:{field}={row.get(field)} expected={expected_ops}")
            if value(row, "verify_enters", errors, index) != expected_batches:
                errors.append(
                    f"row{index}:verify_enters={row.get('verify_enters')} expected={expected_batches}"
                )
        else:
            for field in ("verify_enters", "verify_reads", "slots_installed", "slots_verified"):
                if value(row, field, errors, index) != 0:
                    errors.append(f"row{index}:{field}={row.get(field)} expected=0")
        for field in ZERO_FIELDS:
            if value(row, field, errors, index) != 0:
                errors.append(f"row{index}:{field}={row.get(field)} expected=0")
        elapsed = value(row, "elapsed_ns", errors, index)
        if args.phase == "measured" and elapsed <= 0:
            errors.append(f"row{index}:elapsed_ns_not_positive")
        if args.phase != "measured" and elapsed != 0:
            errors.append(f"row{index}:non_timing_phase_elapsed={elapsed}")
        if row.get("cpu") != "2":
            errors.append(f"row{index}:cpu={row.get('cpu')}")

    report = {
        "status": "pass" if not errors else "fail",
        "scope": "schema and semantic gates only; no performance interpretation",
        "input": str(args.input),
        "profile": args.profile,
        "phase": args.phase,
        "rows": len(rows),
        "errors": errors,
    }
    with args.output.open("x") as output:
        json.dump(report, output, indent=2)
        output.write("\n")
    print(json.dumps(report, separators=(",", ":")))
    if errors:
        raise SystemExit(1)


if __name__ == "__main__":
    main()
