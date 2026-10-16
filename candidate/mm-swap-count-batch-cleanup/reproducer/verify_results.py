#!/usr/bin/env python3
"""Offline arithmetic/identity check. Never runs a benchmark or modifies data."""
import csv
import hashlib
from pathlib import Path
import statistics as st

ROOT = Path(__file__).resolve().parents[1]
POINTS = ('original-a', 'patched', 'original-b')


def require(value, message):
    if not value:
        raise ValueError(message)


def table(name):
    with (ROOT / 'bare-metal' / name).open() as stream:
        return list(csv.DictReader(stream, delimiter='\t'))


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def key(row):
    return row['point'], row['case'], int(row['invocation'])


def close(a, b, label):
    require(abs(float(a) - b) < 1e-8, label)


def main():
    samples, stages = table('samples.tsv'), table('stages.tsv')
    require(len(samples) == 102 and len(stages) == 306, 'all retained rows')
    expected = {(p, c, n) for p in POINTS for c in ('low', 'anchor') for n in range(17)}
    require({key(r) for r in samples} == expected, 'complete sample keys')
    for row in samples:
        k = key(row)
        require(int(row['warmup']) == int(k[2] < 2), 'warmup classification')
        clocks = [r for r in stages if key(r) == k]
        require(len(clocks) == 3 and {int(r['stage']) for r in clocks} == {1, 2, 3}, 'three stages')
        require(all(r['warmup'] == row['warmup'] and r['cpu'] == '2' for r in clocks), 'stage identity')
        require(all(int(r['wall_ns']) > 0 for r in clocks), 'positive timer')
        close(row['mean_ns'], sum(int(r['wall_ns']) for r in clocks) / 3, 'invocation mean')
        noisy = any(any(int(r[f]) for f in ('nvcsw', 'nivcsw', 'minflt', 'majflt')) for r in clocks)
        require(int(row['noisy']) == int(noisy), 'noise retained')
    summaries = table('summary.tsv')
    require(len(summaries) == 2 and {r['case'] for r in summaries} == {'anchor', 'low'}, 'summary cases')
    for summary in summaries:
        cells = [[r for r in samples if r['point'] == p and r['case'] == summary['case'] and r['warmup'] == '0'] for p in POINTS]
        require(all(len(c) == 15 for c in cells), 'formal counts')
        positions = [[int(r['anchor_index']) for r in c] for c in cells]
        require(positions[0] == positions[1] == positions[2], 'matched slot positions')
        values = [[float(r['mean_ns']) for r in c] for c in cells]
        means = [st.mean(v) for v in values]
        base = st.mean(values[0] + values[2])
        for field, value in zip(('baseline_a_ns', 'prototype_ns', 'baseline_b_ns'), means):
            close(summary[field], value, field)
        close(summary['baseline_combined_ns'], base, 'pooled baseline')
        close(summary['change_pct'], 100 * (means[1] / base - 1), 'change')
        close(summary['return_drift_pct'], 100 * (means[2] / means[0] - 1), 'drift')
        for field, values_ in zip(('baseline_a_cv_pct', 'prototype_cv_pct', 'baseline_b_cv_pct'), values):
            close(summary[field], 100 * st.pstdev(values_) / st.mean(values_), field)
    builds = table('builds.tsv')
    require(len(builds) == 3 and {b['point'] for b in builds} == set(POINTS), 'build rows')
    for build in builds:
        require(build['commit'] == '2967f3ccdb5fae956f1b085370ec292bab5c7e25', 'fixed MM commit')
        kind = 'prototype' if build['point'] == 'patched' else 'baseline'
        require(sha(ROOT / 'bare-metal' / (kind + '.config')) == build['config_sha256'], 'config hash')
    configs = [(ROOT / 'bare-metal' / (k + '.config')).read_bytes().splitlines() for k in ('baseline', 'prototype')]
    require([x for x in configs[0] if not x.startswith(b'CONFIG_LOCALVERSION=')] == [x for x in configs[1] if not x.startswith(b'CONFIG_LOCALVERSION=')], 'matched configuration')
    require(sha(ROOT / 'prototype/batch-cleanup.patch') == 'e04de8724708dfd4cc19daa7f8b33fa7c872db26b079797d001fe27349ddcf0a', 'tested patch')
    require(sha(ROOT / 'reproducer/shared_probe.c') == '91d09753882a618a7665a0c474308c497049b7648b2fe31e7d62922838977562', 'tested source')
    print('PASS: 90 formal + 12 warmups, 306 stages, arithmetic, matched slots/configs, tested patch/source')


if __name__ == '__main__':
    main()
