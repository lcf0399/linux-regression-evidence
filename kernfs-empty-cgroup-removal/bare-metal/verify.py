#!/usr/bin/env python3
"""Verify report measurements and identities; never run a workload or contact a host."""
import csv
import hashlib
import io
import json
import math
from pathlib import Path
import statistics

HERE = Path(__file__).resolve().parent
ARMS = ('original-cb0', 'original-cb1', 'control-scx', 'control-disabled')
EXPERIMENTS = {
    'exact-r1': (('base-A', 'guard-A', 'notify-A', 'notify-B', 'guard-B', 'base-B'), ARMS),
    'exact-r2': (('base-A', 'guard-A', 'notify-A', 'notify-B', 'guard-B', 'base-B'), ARMS),
    'root-reuse-r1': (('notify-A', 'prototype-A', 'prototype-B', 'notify-B'), ARMS),
    'mainline-r1': (('old-A', 'latest', 'old-B'), ('control-disabled',)),
}
MEASURE_FIELDS = ('experiment', 'point', 'arm', 'sample', 'operations', 'warmups',
                  'create_mean_ns', 'remove_mean_ns')
POINT_FIELDS = ('experiment', 'point', 'arm', 'metric', 'median_ns', 'cv_percent',
                'drop_first_median_ns')
COMPARE_FIELDS = ('experiment', 'arm', 'metric', 'contrast', 'min_change_percent',
                  'max_change_percent', 'min_drop_first_percent', 'max_drop_first_percent',
                  'max_cv_percent', 'old_drift_percent', 'new_drift_percent',
                  'stability_pass', 'signal_direction', 'signal_pass')


def require(ok, message):
    if not ok:
        raise ValueError(message)


def tsv(fields, rows):
    stream = io.StringIO()
    writer = csv.DictWriter(stream, fieldnames=fields, delimiter='\t', lineterminator='\n')
    writer.writeheader()
    writer.writerows(rows)
    return stream.getvalue()


def distribution(values):
    require(len(values) == 9 and all(math.isfinite(v) and v > 0 for v in values),
            'nine finite positive sample means required')
    return {'median_ns': statistics.median(values),
            'cv_percent': statistics.pstdev(values) / statistics.mean(values) * 100,
            'drop_first_median_ns': statistics.median(values[1:])}


def summarize(rows):
    expected = {(e, p, a, i) for e, (points, arms) in EXPERIMENTS.items()
                for p in points for a in arms for i in range(1, 10)}
    indexed = {}
    for row in rows:
        require(set(row) == set(MEASURE_FIELDS), 'measurement columns differ')
        key = (row['experiment'], row['point'], row['arm'], int(row['sample']))
        require(key in expected and key not in indexed, 'unknown or duplicate sample: ' + str(key))
        require(int(row['operations']) == 128 and int(row['warmups']) == 16, 'workload shape differs')
        indexed[key] = row
    require(set(indexed) == expected, 'missing samples')
    point_rows, groups = [], {}
    for experiment, (points, arms) in EXPERIMENTS.items():
        for point in points:
            for arm in arms:
                for metric in ('create_ns', 'remove_ns'):
                    field = metric.replace('_ns', '_mean_ns')
                    values = [float(indexed[experiment, point, arm, i][field]) for i in range(1, 10)]
                    stats = distribution(values)
                    groups[experiment, point, arm, metric] = stats
                    point_rows.append(dict(experiment=experiment, point=point, arm=arm,
                                           metric=metric, **stats))
    comparisons = []
    for experiment, (_, arms) in EXPERIMENTS.items():
        if experiment.startswith('exact-'):
            pairs = [(n + '-over-' + o, (o + '-A', o + '-B'), (n + '-A', n + '-B'), 'slower')
                     for o, n in (('base', 'guard'), ('guard', 'notify'), ('base', 'notify'))]
        elif experiment == 'root-reuse-r1':
            pairs = [('prototype-over-notify', ('notify-A', 'notify-B'),
                      ('prototype-A', 'prototype-B'), 'faster')]
        else:
            pairs = [('latest-over-v7.2', ('old-A', 'old-B'), ('latest',), 'either')]
        for arm in arms:
            for metric in ('create_ns', 'remove_ns'):
                for contrast, old, new, direction in pairs:
                    get = lambda point: groups[experiment, point, arm, metric]
                    change = lambda newer, older, field: (get(newer)[field] / get(older)[field] - 1) * 100
                    changes = [change(n, o, 'median_ns') for n in new for o in old]
                    drops = [change(n, o, 'drop_first_median_ns') for n in new for o in old]
                    old_drift = change(old[-1], old[0], 'median_ns')
                    new_drift = change(new[-1], new[0], 'median_ns') if len(new) > 1 else ''
                    cv = max(get(p)['cv_percent'] for p in (*old, *new))
                    stable = cv <= 3 and abs(old_drift) <= 2 and (new_drift == '' or abs(new_drift) <= 2)
                    values = changes + drops
                    if direction == 'either':
                        direction_used = 'slower' if min(values) >= 5 else 'faster' if max(values) <= -5 else 'none'
                    else:
                        direction_used = direction
                    signal = stable and ((direction_used == 'slower' and min(values) >= 5) or
                                         (direction_used == 'faster' and max(values) <= -5))
                    comparisons.append(dict(experiment=experiment, arm=arm, metric=metric, contrast=contrast,
                        min_change_percent=min(changes), max_change_percent=max(changes),
                        min_drop_first_percent=min(drops), max_drop_first_percent=max(drops),
                        max_cv_percent=cv, old_drift_percent=old_drift, new_drift_percent=new_drift,
                        stability_pass=stable, signal_direction=direction_used, signal_pass=signal))
    return point_rows, comparisons


def main():
    manifest = json.loads((HERE / 'provenance.json').read_text())
    for relative, digest in manifest['files_sha256'].items():
        path = (HERE.parent / relative).resolve()
        require(path.is_relative_to(HERE.parent.resolve()), 'manifest path escapes bundle')
        require(hashlib.sha256(path.read_bytes()).hexdigest() == digest, 'SHA mismatch: ' + relative)
    with (HERE / 'measured-rounds.tsv').open() as stream:
        rows = list(csv.DictReader(stream, delimiter='\t'))
    points, comparisons = summarize(rows)
    require((HERE / 'point-summary.tsv').read_text() == tsv(POINT_FIELDS, points), 'point statistics differ')
    require((HERE / 'comparison-summary.tsv').read_text() == tsv(COMPARE_FIELDS, comparisons), 'comparisons differ')
    identity = json.loads((HERE / 'identity.json').read_text())
    boots = identity['boots']
    expected = {(e, p) for e, (ps, _) in EXPERIMENTS.items() for p in ps}
    require({(b['experiment'], b['point']) for b in boots} == expected and len(boots) == 19,
            'boot identity inventory differs')
    require(len({b['boot_id'] for b in boots}) == 19, 'boots are not distinct')
    for b in boots:
        require('(full)' in b['preempt'] and str(b['no_turbo']) == '1', 'runtime settings differ')
        require(set(b['governors']) == {'performance'}, 'governor differs')
        require(b['leaf_control_sha256'] == identity['workload']['measured_binary_sha256'], 'binary differs')
    print('Verified 603 samples, 19 distinct boots, 134 point statistics and 58 comparisons; '
          'source/data hashes match. No new timing or full raw-trace audit.')


if __name__ == '__main__':
    main()
