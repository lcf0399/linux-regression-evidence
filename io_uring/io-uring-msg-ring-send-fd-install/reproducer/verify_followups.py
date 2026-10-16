#!/usr/bin/env python3
"""Read-only replay of the published follow-ups. No kernel or network access."""
import csv
import hashlib
import json
import math
from pathlib import Path
import statistics
import sys
from collections import defaultdict


def need(condition, message):
    if not condition:
        raise ValueError(message)


def read_tsv(path):
    with path.open() as stream:
        return list(csv.DictReader(stream, delimiter='\t'))


def verify(directory):
    provenance = json.loads((directory/'provenance.json').read_text())
    for name, digest in provenance['files_sha256'].items():
        need(hashlib.sha256((directory/name).read_bytes()).hexdigest() == digest,
             'file hash mismatch: ' + name)
    rows = read_tsv(directory/'timing.tsv')
    results = read_tsv(directory/'results.tsv')
    need(len(rows) == provenance['publication']['timing_rows'], 'row count')
    need(len(results) == provenance['publication']['metrics'], 'metric count')
    grouped = defaultdict(list)
    for row in rows:
        need(row['semantic_pass'] == row['timing_evidence'] == '1', 'semantic/timing flag')
        need(row['cpu'] == '2', 'wrong CPU')
        need(int(row['interval_ns']) > 0 and int(row['fill_ns']) > 0, 'nonpositive timing')
        if row['case'].startswith('net-'):
            need(int(row['interval_ns']) == sum(int(row[k]) for k in
                 ('connect_ns', 'fill_ns', 'io_ns', 'cleanup_ns')), 'network interval')
        else:
            phase = 'removal_ns' if row['mode'] == 'reuse' else 'registration_ns'
            zero = 'registration_ns' if row['mode'] == 'reuse' else 'removal_ns'
            need(int(row[zero]) == 0 and int(row[phase]) > 0, 'management phase')
            need(int(row['interval_ns']) >= int(row[phase]) + int(row['fill_ns']), 'management interval')
        grouped[row['experiment'], row['case'], row['point']].append(row)
    for key, data in grouped.items():
        width = 4 if key[1].startswith('net-') else 32
        need(len(data) == 3 + 15*width, 'trial count')
        for i, row in enumerate(data):
            need(int(row['trial']) == i, 'trial ordering')
            need(int(row['warmup']) == int(i < 3), 'warmup flag')
            need(int(row['round']) == (-1 if i < 3 else (i-3)//width), 'group number')
            need(int(row['replicate']) == (i if i < 3 else (i-3)%width), 'replicate')
        if key[1].startswith('net-'):
            for name in ('once_setup_ns', 'once_cleanup_ns'):
                need(len({r[name] for r in data}) == 1, 'one-time cost repeated inconsistently')
    seen = set()
    for result in results:
        exp, case, metric = (result[k] for k in ('experiment', 'case', 'metric'))
        need((exp, case, metric) not in seen, 'duplicate metric')
        seen.add((exp, case, metric))
        info = provenance['experiments'][exp]
        points = [p for p in info['points'] if not p.startswith('qual-')]
        need(len(points) == 3, 'expected control A, variant, control B')
        arrays = []
        for point in points:
            data = grouped[exp, case, point]
            arrays.append([statistics.fmean(int(r[metric]) for r in data if int(r['round']) == g)
                           for g in range(15)])
        stable = True
        for offset in (0, 1):
            samples = [a[offset:] for a in arrays]
            means = [sum(s)/len(s) for s in samples]
            midpoint = (means[0]+means[2])/2
            change = 100*(means[1]-midpoint)/midpoint
            drift = 100*(means[2]-means[0])/means[0]
            maximum = max(100*statistics.stdev(s)/m for s,m in zip(samples, means))
            values = dict(change_percent=change, max_cv_percent=maximum,
                          control_drift_percent=drift)
            if offset:
                values = {'drop_first_'+k:v for k,v in values.items()}
            else:
                values.update(control_a_ns=means[0], variant_ns=means[1],
                              control_b_ns=means[2], control_midpoint_ns=midpoint)
            for name, value in values.items():
                need(math.isclose(float(result[name]), value, rel_tol=1e-10, abs_tol=1e-7),
                     f'{exp}/{case}/{metric}: {name}')
            stable = stable and maximum < 3 and abs(drift) < 2
        need(int(result['stable']) == int(stable), 'stability verdict')
    snapshots = 0
    for exp, info in provenance['experiments'].items():
        for case, records in info['qualification'].items():
            for r in records:
                if r['record_type'] == 'cache' and 'active' in r:
                    need(0 <= r['idle'] <= 128 and r['active'] >= 0, 'cache bounds')
                    need(r['idle'] + r['active'] == r['allocated'] - r['released'], 'node conservation')
                    snapshots += 1
                elif r['record_type'] == 'bulkcounts' and 'uses' in r:
                    need(r['uses'] == r['returned'] and r['active'] == 0 and r['lifecycle_bad'] == 0,
                         'node return/cleanup')
                    need(r['released'] == r['alloc_objects'], 'node leak')
                    need(r['alloc_bytes'] == 32*r['alloc_objects'] and r['alloc_bad_size'] == 0,
                         'object size')
    return dict(directory=directory.name, timing_rows=len(rows), metrics=len(results),
                node_snapshots=snapshots, status='pass')


def main():
    root = Path(__file__).resolve().parents[1]/'bare-metal'
    paths = [Path(p) for p in sys.argv[1:]] or [root/n for n in
             ('original-regression', 'dedicated-slab32', 'shared-slab128')]
    for path in paths:
        print(json.dumps(verify(path), sort_keys=True))


if __name__ == '__main__':
    main()
