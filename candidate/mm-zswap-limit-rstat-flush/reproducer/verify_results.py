#!/usr/bin/env python3
"""Recompute compact evidence offline. No workload, network or privileged actions."""
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[1]
POINTS = ('00-parent', '01-child', '02-parent')
CASES = ('finite1', 'finite8', 'unlimited8')
METRICS = ('pageout_cpu_ns', 'restore_verify_cpu_ns', 'program_cpu_ns', 'program_wall_ns')


def need(value, reason):
    if not value:
        raise ValueError(reason)


def read(name, folder=''):
    with (ROOT / 'bare-metal' / folder / name).open() as stream:
        return list(csv.DictReader(stream, delimiter='\t'))


def checked_samples(rows, points=POINTS, cases=CASES):
    expected = {(p, phase, str(i), case) for p in points for case in cases
                for phase, count in (('warmup', 2), ('measured', 9)) for i in range(count)}
    actual = [(r['point'], r['phase'], r['repetition'], r['case']) for r in rows]
    need(len(actual) == len(set(actual)) == len(expected) and set(actual) == expected, 'sample coverage')
    for r in rows:
        need(r['data'] == 'pass' and int(r['bytes']) == 33554432, 'data/work')
        need(int(r['swapped_pages']) == 8192 and int(r['stored_left']) == 0, 'page state')
        need(int(r['workers']) == (1 if r['case'].endswith('1') else 8), 'workers')
        need(0 < int(r['compressed_bytes']) < 33554432, 'compression')
        need(29360128 <= int(r['batched_zswapped_bytes']) <= 33554432, 'batched state')
        pending = int(r['pending_slot_bytes'])
        need(0 <= pending <= 5242880 and pending % 4096 == 0, 'pending slot boundary')
        need(all(int(r[m]) > 0 for m in METRICS), 'positive timing')
    if 'sequence' in rows[0]:
        for point in points:
            need([int(r['sequence']) for r in rows if r['point'] == point]
                 == list(range(len(cases) * 11)), 'recorded execution order')
    return rows


def stats(values):
    mean = statistics.mean(values)
    sd = statistics.stdev(values)
    return dict(n=len(values), mean=mean, median=statistics.median(values), sd=sd,
                cv_percent=100*sd/mean, min=min(values), max=max(values))


def close(actual, expected, name):
    need(math.isclose(float(actual), expected, rel_tol=1e-12, abs_tol=1e-6), name)


def checked_admissions(rows, points, cases, prototype=None):
    expected = {(p,c,k) for p in points for c in cases for k in ('state','path')}
    need(len(rows) == len(expected) and {(r['point'],r['case'],r['kind']) for r in rows}
         == expected, 'admission coverage')
    for row in rows:
        need(row['data'] == 'pass' and int(row['stored_left']) == 0, 'admission data/cleanup')
        need(int(row['swapped_pages']) == int(row['stored_pages']) == 8192, 'admission work')
        if row['kind'] == 'path':
            need(int(row['stores']) == int(row['may_calls']) == 8192, 'store path')
            flushes = 0 if row['case'].startswith('unlimited') or row['point'] == prototype else 8192
            need(int(row['limit_flushes']) == flushes, 'limit path')
        else:
            need(all(row[k] == 'NA' for k in ('stores','may_calls','limit_flushes')), 'unprobed is unknown')


def verify_dataset(folder='', points=POINTS, cases=CASES, prototype=None):
    samples = checked_samples(read('samples.tsv', folder), points, cases)
    summaries = read('summary.tsv', folder)
    labels = ('baseline_a', 'prototype', 'baseline_b', 'baseline_combined') if prototype else (
        'parent_a', 'child', 'parent_b', 'parent_combined')
    need(len(summaries) == len(cases)*len(METRICS) and {(r['case'], r['metric']) for r in summaries}
         == {(c, m) for c in cases for m in METRICS}, 'summary coverage')
    for row in summaries:
        groups = [[int(r[row['metric']]) for r in samples if r['phase'] == 'measured'
                   and r['case'] == row['case'] and r['point'] == point] for point in points]
        all_stats = dict(zip(labels, map(stats, groups + [groups[0] + groups[2]])))
        for group, values in all_stats.items():
            for field, value in values.items():
                close(row[group+'_'+field], value, group+'_'+field)
        parent, child = all_stats[labels[3]]['mean'], all_stats[labels[1]]['mean']
        close(row['delta_ns'], child-parent, 'absolute difference')
        close(row['delta_percent'], 100*(child/parent-1), 'relative difference')
        close(row['return_drift_percent'],
              100*(all_stats[labels[2]]['mean']/all_stats[labels[0]]['mean']-1), 'return drift')
    admissions = read('admissions.tsv', folder)
    checked_admissions(admissions, points, cases, prototype)
    # Ensure missing, duplicated and semantically invalid observations are rejected.
    for bad in (samples[:-1], samples+[samples[0]], [dict(samples[0], data='fail')]+samples[1:]):
        try:
            checked_samples(bad, points, cases)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid evidence accepted')
    for change in ({'stores': '8191'}, {'limit_flushes': '1'}):
        bad = [dict(row, **change) if row['kind'] == 'path' else row for row in admissions]
        try:
            checked_admissions(bad, points, cases, prototype)
        except ValueError:
            pass
        else:
            raise AssertionError('invalid path evidence accepted')
    print(f'PASS: {folder or "exact-commit"}: {len(cases)*27} measured, '
          f'{len(cases)*6} warmups, {len(admissions)} admissions; {len(summaries)} summaries')


def verify_configs(folder, names):
    configs = [(ROOT/'bare-metal'/folder/name).read_text().splitlines() for name in names]
    need([[line for line in config if not line.startswith('CONFIG_LOCALVERSION=')]
          for config in configs][0] ==
         [line for line in configs[1] if not line.startswith('CONFIG_LOCALVERSION=')], 'matched configs')


def verify_eight_repeat():
    """Separate measured-only export; do not pool it with the earlier batches."""
    folder = 'counter-eight-repeat'
    rows = read('samples.tsv', folder)
    points = ('00-baseline', '01-prototype', '02-baseline')
    cases = ('finite8', 'unlimited8')
    expected = {(point, case, str(i)) for point in points for case in cases for i in range(9)}
    actual = [(row['point'], row['case'], row['repetition']) for row in rows]
    need(len(actual) == len(set(actual)) == 54 and set(actual) == expected, 'repeat sample inventory')
    metrics = ('pageout_cpu_us_per_32mib', 'restore_verify_cpu_us', 'program_cpu_us', 'program_wall_us')
    for row in rows:
        need(all(math.isfinite(float(row[key])) and float(row[key]) > 0 for key in metrics),
             'positive finite repeat timings')
        need(int(row['preparation_stat_reads']) == 1, 'all repeated calls passed first preparation read')
    summaries = read('summary.tsv', folder)
    need(len(summaries) == 8 and {(row['case'], row['metric']) for row in summaries} ==
         {(case, metric) for case in cases for metric in metrics}, 'repeat summary inventory')
    for row in summaries:
        groups = [[float(value[row['metric']]) for value in rows
                   if value['point'] == point and value['case'] == row['case']] for point in points]
        values = list(map(stats, groups))
        for label, value in zip(('baseline_a', 'prototype', 'baseline_b'), values):
            close(row[label + '_mean_us'], value['mean'], 'repeat mean')
            close(row[label + '_cv_percent'], value['cv_percent'], 'repeat sample CV')
        baseline = statistics.mean(groups[0] + groups[2])
        close(row['change_percent'], 100 * (values[1]['mean'] / baseline - 1), 'repeat change')
        close(row['return_drift_percent'], 100 * (values[2]['mean'] / values[0]['mean'] - 1),
              'repeat return drift')
    identity = json.loads((ROOT / 'bare-metal' / folder / 'identity.json').read_text())
    need(identity['measured'] == 54 and identity['admissions'] == 18, 'repeat recorded scope')
    need(hashlib.sha256((ROOT / 'reproducer/zswap_case_statwait.c').read_bytes()).hexdigest() ==
         identity['source_sha256'], 'exact repeat source')
    need(hashlib.sha256((ROOT / 'prototype/hierarchical-with-rstat.patch').read_bytes()).hexdigest() ==
         identity['patch_sha256'], 'same measured derivative')
    print('PASS: counter-eight-repeat: all54 measured observations and8 summaries; '
          'source/patch identity, no pooling or new kernel experiment')


def verify():
    verify_dataset()
    points = ('00-baseline', '01-prototype', '02-baseline')
    verify_dataset('counter-mm-pair', points, CASES, '01-prototype')
    verify_dataset('counter-unlimited1', points, ('unlimited1',), '01-prototype')
    verify_eight_repeat()
    for manifest in ('source-sha256.tsv', 'counter-source-sha256.tsv'):
        for row in read(manifest):
            path = (ROOT/row['file']).resolve()
            need(path.is_relative_to(ROOT), 'checksum path within bundle')
            need(hashlib.sha256(path.read_bytes()).hexdigest() == row['sha256'], 'source/data checksum')
    verify_configs('', ('parent.config','child.config'))
    verify_configs('counter-mm-pair', ('baseline.config','prototype.config'))
    builds = {r['role']: r for r in read('builds.tsv', 'counter-mm-pair')}
    for folder, source in (('counter-mm-pair', 'zswap_case.c'),
                           ('counter-unlimited1', 'zswap_case_unlimited1.c')):
        identities = read('points.tsv', folder)
        need([r['point'] for r in identities] == list(points), 'point identity coverage')
        need(len({r['binary_sha256'] for r in identities}) == 1, 'same measured binary per batch')
        for row in identities:
            role = 'prototype' if row['point'] == '01-prototype' else 'baseline'
            need(row['role'] == role and row['kernelrelease'] == builds[role]['kernelrelease'], 'kernel identity')
            need(row['source_sha256'] == hashlib.sha256((ROOT/'reproducer'/source).read_bytes()).hexdigest(),
                 'measured program identity')
    boundaries = read('boundaries.tsv', 'counter-mm-pair')
    cases = {'leaf-zero', 'ancestor-zero', 'leaf-near-reuse', 'ancestor-near',
             'dynamic-limit-precheck', 'cross-cpu', 'concurrent-add-free', 'offline-live-objects'}
    passed = [r for r in boundaries if r['status'] == 'pass']
    failed = [r for r in boundaries if r['status'] != 'pass']
    need(len(passed) == 16 and {(r['role'],r['case']) for r in passed}
         == {(role,case) for role in ('baseline','prototype') for case in cases}, 'boundary coverage')
    need(len(failed) == 1 and failed[0] == dict(role='baseline', case='dynamic-limit',
         batch='boundary-r2', status='failed-test-premise-not-a-kernel-pass'), 'retain failed premise')
    print('PASS: source/data hashes, matched configurations, build/program identity; '
          '16 bounded checks and 1 failed test premise retained (not a new kernel validation)')


if __name__ == '__main__':
    verify()
