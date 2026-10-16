#!/usr/bin/env python3
"""Read-only arithmetic, inventory and artifact check for this result bundle.

This does not execute the workload or repeat live-kernel permission tests.
"""
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

HERE = Path(__file__).resolve().parent
SERIES = {
    'socketpair-prototype': (['a', 'b', 'a2'], ['unconfined']),
    'two-process-prototype': (
        ['a', 'b', 'a2'],
        ['unconfined', 'sender-confined', 'receiver-confined', 'both-confined']),
    'regression-retest': (
        ['aa-parent-a', 'aa-child', 'aa-latest', 'aa-parent-b'], ['unconfined']),
}


def require(ok, message):
    if not ok:
        raise RuntimeError(message)


def read_tsv(name):
    with (HERE / name).open(newline='') as stream:
        return list(csv.DictReader(stream, delimiter='\t'))


def cv(values):
    return 100 * statistics.stdev(values) / statistics.mean(values)


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def main():
    samples = read_tsv('samples.tsv')
    groups = {}
    seen = set()
    measured = warmup = 0
    for row in samples:
        key = (row['series'], row['point'], row['case'], int(row['invocation']))
        identity = (*key, int(row['round']))
        require(identity not in seen, 'duplicate sample')
        seen.add(identity)
        require(row['semantic_pass'] == '1', 'sample semantic failure')
        require(int(row['messages']) == 65536 and int(row['elapsed_ns']) > 0,
                'sample timing/shape mismatch')
        number = int(row['round'])
        first = 3 if row['series'] == 'two-process-prototype' else 0
        expected_phase = 'warmup' if number < first else 'measured'
        require(row['phase'] == expected_phase, 'incorrect phase')
        if row['phase'] == 'warmup':
            warmup += 1
        else:
            measured += 1
        groups.setdefault(key, []).append(row)

    expected_keys = {
        (series, point, case, invocation)
        for series, (points, cases) in SERIES.items()
        for point in points for case in cases for invocation in range(3)
    }
    require(set(groups) == expected_keys, 'missing or additional sample group')
    values = {}
    for key, rows in groups.items():
        rounds = 18 if key[0] == 'two-process-prototype' else 15
        rows.sort(key=lambda row: int(row['round']))
        require([int(row['round']) for row in rows] == list(range(rounds)),
                'missing or reordered round')
        values[key] = [int(row['elapsed_ns']) / int(row['messages'])
                       for row in rows if row['phase'] == 'measured']
    require((measured, warmup) == (855, 108), 'wrong sample totals')

    def point(series, name, case):
        parts = [values[(series, name, case, rep)] for rep in range(3)]
        flat = [value for part in parts for value in part]
        return dict(mean=statistics.mean(flat),
                    drop=statistics.mean(value for part in parts for value in part[1:]),
                    pooled_cv=cv(flat), group_cv=max(map(cv, parts)))

    summary = read_tsv('summary.tsv')
    expected_summary = {('socketpair-prototype', 'unconfined'),
                        ('regression-retest', 'controlled-delta'),
                        ('regression-retest', 'whole-release')}
    expected_summary.update(('two-process-prototype', c)
                            for c in SERIES['two-process-prototype'][1])
    require(len(summary) == 7 and {(r['series'], r['case']) for r in summary}
            == expected_summary, 'summary inventory mismatch')
    for row in summary:
        series, case = row['series'], row['case']
        if series == 'regression-retest':
            test = 'aa-child' if case == 'controlled-delta' else 'aa-latest'
            names, sample_case = ['aa-parent-a', test, 'aa-parent-b'], 'unconfined'
        else:
            names, sample_case = ['a', 'b', 'a2'], case
        a, b, c = [point(series, name, sample_case) for name in names]
        mid = (a['mean'] + c['mean']) / 2
        drift = (100 * (c['mean'] / a['mean'] - 1) if series == 'regression-retest'
                 else 100 * abs(c['mean'] - a['mean']) / mid)
        recomputed = dict(
            reference_a_ns=a['mean'], test_ns=b['mean'], reference_b_ns=c['mean'],
            reference_midpoint_ns=mid, delta_ns=b['mean'] - mid,
            change_percent=100 * (b['mean'] / mid - 1),
            reference_drift_percent=drift,
            max_group_cv_percent=max(p['group_cv'] for p in [a, b, c]),
            max_pooled_cv_percent=max(p['pooled_cv'] for p in [a, b, c]),
            drop_first_change_percent=100 * (b['drop'] / ((a['drop'] + c['drop']) / 2) - 1))
        for field, value in recomputed.items():
            require(math.isclose(value, float(row[field]), rel_tol=1e-11, abs_tol=1e-9),
                    f'{series}/{case}: {field} differs')
        print(f"{series}/{case}: {recomputed['change_percent']:+.6f}%")

    identity = json.loads((HERE / 'run-identity.json').read_text())
    require(len(identity['points']) == 10 and
            len({p['boot_id'] for p in identity['points']}) == 10, 'boot inventory')
    for series, (points, _) in SERIES.items():
        entries = [p for p in identity['points'] if p['series'] == series]
        require([p['point'] for p in entries] == points, 'boot order')
        require(len({p['binary_sha256'] for p in entries}) == 1, 'binary mismatch')
        require(all(p['status'] == p['cleanup'] == 'pass' and '(full)' in p['actual_preempt']
                    for p in entries), 'runtime identity or cleanup failure')
    require(digest(HERE / 'early-unconfined.patch') ==
            identity['builds']['prototype']['patch_sha256'], 'patch changed')
    require(digest(HERE / 'reproducer/af_unix_live.c') ==
            identity['workload']['two_process_source_sha256'], 'live source changed')
    require(digest(HERE / identity['workload']['socketpair_source']) ==
            identity['workload']['socketpair_source_sha256'], 'socketpair source changed')
    for name, expected in identity.get('packaged_checks_sha256', {}).items():
        require(digest(HERE / 'reproducer' / name) == expected, 'check source changed')
    print('PASS: 855 measured rows, 108 retained warm-up rows, 7 comparisons, 10 boots')
    print('Timing/identity verification only; live security outcomes are summarized in validation.json.')


if __name__ == '__main__':
    main()
