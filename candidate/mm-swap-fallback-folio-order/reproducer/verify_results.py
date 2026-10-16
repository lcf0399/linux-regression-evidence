#!/usr/bin/env python3
"""Offline independent check of the curated CPU data; no kernel changes."""
import csv
from pathlib import Path
import statistics

ROOT = Path(__file__).resolve().parents[1]
CASES = ('serial-base', 'parallel-base', 'serial-thp', 'parallel-thp')
POINTS = ('baseline-a', 'prototype', 'baseline-b')


def rows(path):
    with path.open() as stream:
        return list(csv.DictReader(stream, delimiter='\t'))


def verify(root=ROOT):
    samples = rows(root / 'bare-metal/samples.tsv')
    cycles = rows(root / 'bare-metal/cycles.tsv')
    summaries = rows(root / 'bare-metal/summary.tsv')
    assert len(samples) == 132 and sum(int(s['warmup']) == 0 for s in samples) == 108
    keys = {(s['point'], s['case'], s['invocation']) for s in samples}
    assert len(keys) == 132
    assert keys == {(p, c, str(i)) for p in POINTS for c in CASES for i in range(11)}
    assert len(cycles) == 792
    for s in samples:
        key = s['point'], s['case'], s['invocation']
        cs = [c for c in cycles if (c['point'], c['case'], c['invocation']) == key]
        workers = 2 if s['case'].startswith('parallel') else 1
        assert len(cs) == 4 * workers
        assert {(int(c['round']), int(c['worker'])) for c in cs} == {
            (r, w) for r in range(4) for w in range(workers)}
        assert int(s['warmup']) == int(int(s['invocation']) < 2)
        for c in cs:
            assert c['warmup'] == s['warmup'] and int(c['content_ok']) == 1
            assert int(c['pages']) == int(c['swapped']) == int(c['ready']) == 2048 // workers
            assert int(c['thp_before']) == (2048 // workers if s['case'].endswith('thp') else 0)
            assert int(c['thp_after']) == 0
        po = sum(int(c['pageout_cpu_ns']) for c in cs) / 8192
        rf = sum(int(c['refault_cpu_ns']) for c in cs) / 8192
        assert float(s['api_cpu_ns_per_page']) == po + rf
        assert float(s['pageout_cpu_ns_per_page']) == po
        assert float(s['refault_cpu_ns_per_page']) == rf
        assert int(s['deferred_swap_bytes']) == 0
    assert len(summaries) == 20
    for r in summaries:
        values = [[float(s[r['metric']]) for s in samples if s['case'] == r['case']
                   and s['point'] == p and s['warmup'] == '0'] for p in POINTS]
        assert all(len(v) == 9 for v in values)
        means = [statistics.mean(v) for v in values]
        base = statistics.mean(values[0] + values[2])
        expected = dict(baseline_a=means[0], prototype=means[1], baseline_b=means[2],
                        baseline_combined=base, change_pct=100 * (means[1] / base - 1),
                        return_drift_pct=100 * (means[2] / means[0] - 1),
                        max_point_cv_pct=max(100 * statistics.pstdev(v) / statistics.mean(v) for v in values))
        for k, value in expected.items():
            assert abs(float(r[k]) - value) < 1e-9, (r['case'], r['metric'], k)
    return 'pass: 108 formal + 24 warmups, 792 worker cycles, all 20 summaries'


if __name__ == '__main__':
    print(verify())
