#!/usr/bin/env python3
"""Recompute all published metrics offline; never run or install a kernel."""
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

HERE = Path(__file__).resolve().parent
METRICS = ('original.mkdir_ns', 'original.rmdir_ns', 'cycle.mkdir_ns',
           'cycle.stat_ns', 'cycle.rmdir_ns', 'cycle.sequence_ns',
           'lookup.first_file_stat_ns', 'lookup.cached_stat_batch_ns_per_call')
POINTS = ('A1', 'B1', 'B2', 'A2')


def compare(values):
    groups = {}
    for point, samples in values.items():
        assert len(samples) == 9 and all(math.isfinite(v) and v > 0 for v in samples)
        groups[point] = dict(values_ns=samples, median_ns=statistics.median(samples),
            cv_percent=statistics.pstdev(samples) / statistics.mean(samples) * 100,
            drop_first_median_ns=statistics.median(samples[1:]))
    def changes(key):
        return {f'{b}/{a}': (groups[b][key] / groups[a][key] - 1) * 100
                for a in ('A1', 'A2') for b in ('B1', 'B2')}
    effects, drop = changes('median_ns'), changes('drop_first_median_ns')
    drift = {r: (groups[r + '2']['median_ns'] / groups[r + '1']['median_ns'] - 1) * 100 for r in ('A', 'B')}
    stable = all(v['cv_percent'] <= 3 for v in groups.values()) and all(abs(v) <= 2 for v in drift.values())
    all_effects = [*effects.values(), *drop.values()]
    status = ('unstable-not-interpreted' if not stable else
              'faster-at-least-5-percent' if max(all_effects) <= -5 else
              'slower-at-least-5-percent' if min(all_effects) >= 5 else 'stable-no-5-percent-signal')
    return dict(groups=groups, B_over_A_change_percent=effects, drop_first_change_percent=drop,
                repeat_boot_drift_percent=drift, stability_pass=stable, status=status,
                independent_second_matrix=False)


def summarize(rows):
    values, seen = {}, set()
    for row in rows:
        metric, point, sample = row['metric'], row['point'], int(row['sample'])
        assert metric in METRICS and point in POINTS and 1 <= sample <= 9
        assert int(row['operations']) == (128 if metric.startswith('original.') else 32)
        assert int(row['warmups']) == 16
        key = metric, point, sample
        assert key not in seen
        seen.add(key)
        values.setdefault(metric, {}).setdefault(point, {})[sample] = float(row['mean_ns'])
    assert len(rows) == len(seen) == 288
    result = {}
    for metric in METRICS:
        assert set(values[metric]) == set(POINTS)
        ordered = {}
        for point in POINTS:
            assert set(values[metric][point]) == set(range(1, 10))
            ordered[point] = [values[metric][point][i] for i in range(1, 10)]
        result[metric] = compare(ordered)
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
    for section in ('files_sha256', 'shared_reproducer_sha256'):
        for name, digest in manifest[section].items():
            assert hashlib.sha256((HERE / name).read_bytes()).hexdigest() == digest, name
    with (HERE / 'measured-rounds.tsv').open() as stream:
        rows = list(csv.DictReader(stream, delimiter='\t'))
    equal(summarize(rows), json.loads((HERE / 'summary.json').read_text()))
    identity = json.loads((HERE / 'identity.json').read_text())
    assert len({b['boot_id'] for b in identity['boots']}) == 4
    assert [b['point'] for b in identity['boots']] == list(POINTS)
    assert len({b['canonical_config_sha256'] for b in identity['kernel_builds'].values()}) == 1
    semantics = json.loads((HERE / 'semantics.json').read_text())
    assert sum(p[m]['rounds'] for p in semantics['first_creation_race'].values()
               for m in ('traced', 'untraced')) == 704
    assert all(p['traced']['observed_write_lock_wait_rounds'] == 0
               for p in semantics['first_creation_race'].values())
    print('Verified 108 invocations, 288 metric rows, all eight metrics, CV/drift/drop-first, file hashes and bounded-check counts. Not a safety proof.')


if __name__ == '__main__':
    main()
