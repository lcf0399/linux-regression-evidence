#!/usr/bin/env python3

import argparse
import csv
import json
import statistics
import sys
from pathlib import Path


PROFILES = {
    "r0_remap",
    "l0_standard_lifecycle",
    "n0_userbacked_lifecycle",
    "a0_anon_map",
    "e0_errors",
}


def as_int(row, key):
    return int(row[key], 0)


def fail(errors, message):
    errors.append(message)


def validate_row(row, profile, expected_events, errors):
    if row["profile"] != profile:
        fail(errors, f"profile mismatch: {row['profile']}")
    if row["schema_version"] != "1":
        fail(errors, "unexpected schema_version")
    if row["semantic_pass"] != "true":
        fail(errors, f"semantic_pass=false in {row['phase']} round {row['round']}")
    if as_int(row, "events") != expected_events:
        fail(errors, f"events mismatch in {row['phase']} round {row['round']}")
    if as_int(row, "successful_events") != expected_events:
        fail(errors, f"successful_events mismatch in round {row['round']}")
    for key in (
        "validation_failures",
        "cleanup_failures",
        "sq_dropped",
        "cq_overflow",
        "outstanding",
    ):
        if as_int(row, key) != 0:
            fail(errors, f"{key} is nonzero in round {row['round']}")
    if as_int(row, "sq_entries") != 64 or as_int(row, "cq_entries") != 128:
        fail(errors, "ring entries differ from the frozen geometry")
    if row["single_mmap"] != "true":
        fail(errors, "IORING_FEAT_SINGLE_MMAP is absent")
    if as_int(row, "ring_bytes") <= 0 or as_int(row, "sqes_bytes") <= 0:
        fail(errors, "invalid mapping geometry")
    if len(row["input_sha256"]) != 64 or len(row["target_sha256"]) != 64:
        fail(errors, "missing source/input identity")

    setup = as_int(row, "setup_count")
    close = as_int(row, "close_count")
    mmaps = as_int(row, "mmap_count")
    munmaps = as_int(row, "munmap_count")
    nops = as_int(row, "nop_count")
    if profile == "r0_remap":
        if (setup, close, nops) != (1, 1, 2):
            fail(errors, "R0 setup/close/NOP counts are wrong")
        if mmaps != expected_events * 2 + 4 or munmaps != mmaps:
            fail(errors, "R0 mmap/munmap balance is wrong")
    elif profile == "l0_standard_lifecycle":
        if (setup, close, nops) != (expected_events,) * 3:
            fail(errors, "L0 lifecycle counts are wrong")
        if mmaps != expected_events * 2 or munmaps != mmaps:
            fail(errors, "L0 mmap/munmap balance is wrong")
    elif profile == "n0_userbacked_lifecycle":
        if (setup, close, nops) != (expected_events,) * 3:
            fail(errors, "N0 lifecycle counts are wrong")
        if mmaps or munmaps:
            fail(errors, "N0 unexpectedly used standard ring mmap")
        if row["user_ring_addr_ok"] != "true" or row["user_sqes_addr_ok"] != "true":
            fail(errors, "N0 user addresses changed or escaped their buffers")
    elif profile == "a0_anon_map":
        if setup or close or nops:
            fail(errors, "A0 unexpectedly used io_uring")
        if mmaps != expected_events * 2 or munmaps != mmaps:
            fail(errors, "A0 mmap/munmap balance is wrong")
    elif profile == "e0_errors":
        if (setup, close, nops) != (2, 2, 1):
            fail(errors, "E0 semantic-check counts are wrong")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--profile", required=True, choices=sorted(PROFILES))
    parser.add_argument(
        "--phase", required=True, choices=("semantic-smoke", "direct-hit", "measured")
    )
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    with open(args.input, newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    errors = []
    expected_rows = 18 if args.phase == "measured" else 1
    if len(rows) != expected_rows:
        fail(errors, f"row count {len(rows)} != {expected_rows}")

    expected_events = 2 if args.profile == "e0_errors" else (
        4096 if args.phase == "measured" and args.profile in {"r0_remap", "a0_anon_map"}
        else 512 if args.phase == "measured"
        else 16 if args.phase == "direct-hit"
        else 8
    )
    for row in rows:
        validate_row(row, args.profile, expected_events, errors)

    if args.phase == "measured" and rows:
        warmups = [row for row in rows if row["phase"] == "warmup"]
        measured = [row for row in rows if row["phase"] == "measured"]
        if len(warmups) != 3 or len(measured) != 15:
            fail(errors, "measured point must contain 3 warmups and 15 measured rounds")
        if any(row["trace_enabled"] != "false" for row in rows):
            fail(errors, "tracing was enabled during formal timing")
    elif args.phase == "direct-hit" and rows:
        measured = rows
        if any(row["trace_enabled"] != "true" for row in rows):
            fail(errors, "trace run did not record trace_enabled=true")
    else:
        measured = rows

    values = [float(row["ns_per_event"]) for row in measured if float(row["ns_per_event"]) > 0]
    mean = statistics.fmean(values) if values else 0.0
    cv = statistics.stdev(values) / mean if len(values) > 1 and mean else 0.0
    audit = {
        "status": "pass" if not errors else "fail",
        "input": str(Path(args.input).name),
        "profile": args.profile,
        "phase": args.phase,
        "rows": len(rows),
        "expected_events_per_row": expected_events,
        "measured_mean_ns_per_event": mean,
        "measured_cv": cv,
        "cv_gate_3pct": cv <= 0.03,
        "errors": errors,
    }
    Path(args.output).write_text(json.dumps(audit, indent=2) + "\n", encoding="utf-8")
    if errors:
        for error in errors:
            print(error, file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
