#!/usr/bin/env python3
"""Validate raw io_uring cancel workload rows without interpreting timing."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


ERROR_FIELDS = (
    "cancel_other",
    "unexpected_cqes",
    "duplicate_cqes",
    "bad_results",
    "bad_flags",
    "eventfd_state_failures",
    "timeouts",
    "sq_dropped",
    "cq_overflow",
    "outstanding",
)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--profile", required=True)
    parser.add_argument("--phase", required=True, choices=("semantic-smoke", "direct-hit-trace", "measured"))
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    with args.input.open(encoding="utf-8", newline="") as handle:
        rows = list(csv.DictReader(handle, delimiter="\t"))
    expected_rows = 15 if args.phase == "measured" else 1
    errors: list[str] = []
    if len(rows) != expected_rows:
        errors.append(f"row_count={len(rows)} expected={expected_rows}")
    for index, row in enumerate(rows):
        if row.get("profile") != args.profile:
            errors.append(f"row {index}: profile={row.get('profile')}")
        if row.get("semantic_pass") != "1":
            errors.append(f"row {index}: semantic_pass={row.get('semantic_pass')}")
        for field in ERROR_FIELDS:
            if row.get(field) != "0":
                errors.append(f"row {index}: {field}={row.get(field)}")
        if args.phase == "measured":
            if row.get("phase") != "measured":
                errors.append(f"row {index}: phase={row.get('phase')}")
            if int(row.get("timed_ns", "0")) <= 0:
                errors.append(f"row {index}: timed_ns is not positive")
            if float(row.get("ns_per_attempt", "0")) <= 0:
                errors.append(f"row {index}: ns_per_attempt is not positive")
        else:
            expected_phase = "trace" if args.phase == "direct-hit-trace" else "smoke"
            if row.get("phase") != expected_phase:
                errors.append(f"row {index}: phase={row.get('phase')}")
    audit = {
        "status": "pass" if not errors else "fail",
        "input": str(args.input),
        "profile": args.profile,
        "phase": args.phase,
        "rows": len(rows),
        "errors": errors,
    }
    args.output.write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(audit, indent=2))
    return 0 if not errors else 1


if __name__ == "__main__":
    raise SystemExit(main())
