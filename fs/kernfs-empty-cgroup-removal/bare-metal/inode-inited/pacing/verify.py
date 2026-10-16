#!/usr/bin/env python3
"""Offline checks for all pacing samples; no kernel execution or local logs."""
import argparse
import csv
import hashlib
import importlib.util
import json
import math
from pathlib import Path

HERE = Path(__file__).resolve().parent
spec = importlib.util.spec_from_file_location('inited_verify', HERE.parent / 'verify.py')
base = importlib.util.module_from_spec(spec)
spec.loader.exec_module(base)
PRIMARY = ('cycle_ns', 'mkdir_ns', 'rmdir_ns', 'stat_ns')
METRICS = tuple(sorted((*PRIMARY, 'between_stage_ns', 'workload_wall_per_cycle_ns',
    *(prefix + metric for prefix in ('first_', 'first4_', 'tail_') for metric in PRIMARY))))
CASES = {f'w{warm}-p{pace}': (warm, pace) for warm in (0, 16) for pace in (0, 50, 200)}
POINTS = ('A1', 'B1', 'B2', 'A2')
FIELDS = ('case', 'point', 'sample', 'warmups', 'pace_ms', 'operations', *METRICS)


def summarize(rows):
    groups, seen = {}, set()
    for row in rows:
        assert set(row) == set(FIELDS), 'unexpected columns'
        case, point, sample = row['case'], row['point'], int(row['sample'])
        assert case in CASES and point in POINTS and 1 <= sample <= 9
        assert (int(row['warmups']), int(row['pace_ms'])) == CASES[case]
        assert int(row['operations']) == 32
        key = case, point, sample
        assert key not in seen, 'duplicate sample'
        seen.add(key)
        for metric in METRICS:
            value = float(row[metric])
            assert math.isfinite(value) and value > 0, (key, metric)
            groups.setdefault((case, metric), {}).setdefault(point, {})[sample] = value
    assert len(seen) == 216
    result = {}
    for case in CASES:
        result[case] = {}
        for metric in METRICS:
            raw = groups[case, metric]
            assert set(raw) == set(POINTS)
            assert all(set(values) == set(range(1, 10)) for values in raw.values())
            result[case][metric] = base.compare({point: [raw[point][i] for i in range(1, 10)]
                                                for point in POINTS})
    return result


def primary(results):
    return {case: {metric: results[case][metric] for metric in PRIMARY} for case in CASES}


def verify():
    manifest = json.loads((HERE / 'provenance.json').read_text())
    for name, digest in {**manifest['files_sha256'], **manifest['shared_files_sha256']}.items():
        assert hashlib.sha256((HERE / name).read_bytes()).hexdigest() == digest, name
    with (HERE / 'measured-rounds.tsv').open() as f:
        results = summarize(list(csv.DictReader(f, delimiter='\t')))
    base.equal(primary(results), json.loads((HERE / 'summary.json').read_text()))
    return results


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--all-metrics', action='store_true', help='print recomputed initial/tail metrics too')
    args = parser.parse_args()
    results = verify()
    if args.all_metrics:
        print(json.dumps(results, indent=2))
    else:
        print('Verified 216 invocations, 3888 metric values and 432 groups; '
              'primary summary, CVs, drift and drop-first. No experiments pooled.')
