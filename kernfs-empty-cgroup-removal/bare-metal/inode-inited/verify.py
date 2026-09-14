#!/usr/bin/env python3
"""Recompute the published ordered sample means; no raw trace or local dependencies."""
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

HERE = Path(__file__).resolve().parent


def compare(groups):
    stats = {}
    for point, values in groups.items():
        assert len(values) == 9 and all(math.isfinite(x) and x > 0 for x in values)
        stats[point] = dict(values_ns=values, median_ns=statistics.median(values),
                            cv_percent=statistics.pstdev(values) / statistics.mean(values) * 100,
                            drop_first_median_ns=statistics.median(values[1:]))
    effects, drop = {}, {}
    for a in ('A1', 'A2'):
        for b in ('B1', 'B2'):
            effects[f'{b}/{a}'] = (stats[b]['median_ns'] / stats[a]['median_ns'] - 1) * 100
            drop[f'{b}/{a}'] = (stats[b]['drop_first_median_ns'] / stats[a]['drop_first_median_ns'] - 1) * 100
    drift = {k: (stats[b]['median_ns'] / stats[a]['median_ns'] - 1) * 100
             for k, a, b in [('A', 'A1', 'A2'), ('B', 'B1', 'B2')]}
    stable = all(g['cv_percent'] <= 3 for g in stats.values()) and all(abs(v) <= 2 for v in drift.values())
    all_effects = [*effects.values(), *drop.values()]
    status = ('unstable-not-interpreted' if not stable else
              'faster-at-least-5-percent' if max(all_effects) <= -5 else
              'slower-at-least-5-percent' if min(all_effects) >= 5 else 'stable-no-5-percent-signal')
    return dict(groups=stats, B_over_A_change_percent=effects, drop_first_change_percent=drop,
                repeat_boot_drift_percent=drift, stability_pass=stable, status=status,
                independent_second_matrix=False)


def summarize(rows):
    expected = {'original': {'continuous': ('create_ns', 'remove_ns')},
                'stage-followup': {c: ('mkdir_ns', 'stat_ns', 'rmdir_ns', 'cycle_ns',
                                      'between_stage_ns', 'workload_wall_per_cycle_ns')
                                   for c in ('continuous', 'paced50')}}
    groups, seen = {}, set()
    for row in rows:
        exp, case, metric, point = (row[k] for k in ('experiment', 'case', 'metric', 'point'))
        assert metric in expected[exp][case] and point in ('A1', 'B1', 'B2', 'A2')
        index = int(row['sample'])
        key = exp, case, metric, point, index
        assert key not in seen and 1 <= index <= 9
        assert int(row['operations']) == 128 and int(row['warmups']) == 16
        seen.add(key)
        groups.setdefault((exp, case, metric), {}).setdefault(point, {})[index] = float(row['mean_ns'])
    result = {}
    for exp, cases in expected.items():
        result[exp] = {}
        for case, metrics in cases.items():
            result[exp][case] = {}
            for metric in metrics:
                raw = groups[exp, case, metric]
                assert set(raw) == {'A1', 'B1', 'B2', 'A2'}
                ordered = {}
                for point, values in raw.items():
                    assert set(values) == set(range(1, 10))
                    ordered[point] = [values[i] for i in range(1, 10)]
                result[exp][case][metric] = compare(ordered)
    return result


def equal(actual, expected):
    if isinstance(expected, dict):
        assert set(actual) == set(expected)
        for key in expected:
            equal(actual[key], expected[key])
    elif isinstance(expected, list):
        assert len(actual) == len(expected)
        for a, b in zip(actual, expected):
            equal(a, b)
    elif isinstance(expected, float):
        assert math.isclose(actual, expected, rel_tol=1e-12, abs_tol=1e-9), (actual, expected)
    else:
        assert actual == expected, (actual, expected)


def main():
    manifest = json.loads((HERE / 'provenance.json').read_text())
    for name, digest in manifest['files_sha256'].items():
        assert hashlib.sha256((HERE / name).read_bytes()).hexdigest() == digest, name
    with (HERE / 'measured-rounds.tsv').open() as f:
        rows = list(csv.DictReader(f, delimiter='\t'))
    assert len(rows) == 504  # Metric rows, not 504 independent invocations.
    equal(summarize(rows), json.loads((HERE / 'summary.json').read_text()))
    print('Verified 36 original + 72 follow-up invocations; all 504 metric rows, CVs, drift and drop-first. No experiments pooled.')


if __name__ == '__main__':
    main()
