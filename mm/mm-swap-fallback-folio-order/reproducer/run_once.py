#!/usr/bin/env python3
"""One bounded F02 invocation. Never changes boot, power, swap or global VM policy."""
import argparse
import hashlib
import os
from pathlib import Path
import re
import subprocess
import time

HERE = Path(__file__).resolve().parent
CASES = ('serial-base', 'parallel-base', 'serial-thp', 'parallel-thp')


def need(ok, message):
    if not ok:
        raise RuntimeError(message)


def parse(raw, case):
    def records(tag):
        return [dict(re.findall(r'(\w+)=([^ ]+)', s)) for s in raw.splitlines()
                if s.startswith(tag + ' ')]
    cs, rs, bs = records('CYCLE'), records('RESULT'), records('BARRIER')
    workers = 2 if case.startswith('parallel') else 1
    huge = int(case.endswith('thp'))
    need(len(cs) == 4 * workers and len(rs) == 1 and len(bs) == 4, 'complete output')
    need({(int(c['worker']), int(c['round'])) for c in cs} == {
        (w, r) for w in range(workers) for r in range(4)}, 'complete worker rounds')
    for c in cs:
        need(int(c['workers']) == workers and int(c['huge']) == huge, 'same shape')
        need(int(c['pages']) == int(c['ready']) == int(c['swapped']) == 2048 // workers,
             'all pages swapped; cache ready before refault')
        need(int(c['thp_before']) == huge * 2048 // workers and int(c['thp_after']) == 0,
             'initial page shape and base-page fallback')
        need(int(c['content_ok']) == 1 and int(c['cpu']) == (4 if int(c['worker']) else 2),
             'data and CPU')
        need(all(0 < int(c[k]) < 10_000_000_000 for k in
                 ('pageout_cpu_ns', 'refault_cpu_ns', 'pageout_ns', 'refault_ns')), 'bounded clocks')
    need(rs[0]['family'] == 'F02' and int(rs[0]['reaped']) == workers
         and int(rs[0]['content_ok']) == 1 and int(rs[0]['total_bytes']) == 8388608, 'F02 result')
    need([int(b['round']) for b in bs] == list(range(4)) and all(
        b['includes_checks'] == b['includes_fixed_wait'] == '1' for b in bs), 'barriers')
    cpu = sum(int(c['pageout_cpu_ns']) + int(c['refault_cpu_ns']) for c in cs) / 4 / 1000
    return f'api_worker_cpu_us_per_8MiB={cpu:.6f}\ncycles=4\napplication_gain=not_tested\n'


def run(case, out):
    need(os.geteuid() == 0 and os.sysconf('SC_PAGE_SIZE') == 4096, 'requires root and 4 KiB pages')
    need({2, 4, 16} <= os.sched_getaffinity(0), 'original CPU 2/4/16 layout required')
    cgroot = Path('/sys/fs/cgroup')
    need({'memory', 'pids'} <= set((cgroot / 'cgroup.subtree_control').read_text().split()),
         'cgroup v2 memory and pids controllers must already be enabled; not changing them')
    swaps = Path('/proc/swaps').read_text().splitlines()[1:]
    need(len(swaps) == 1 and swaps[0].split()[0:2] == ['/swap.img', 'file'],
         'requires the original single /swap.img backend; no swapon/swapoff performed')
    need((int(swaps[0].split()[2]) - int(swaps[0].split()[3])) * 1024 > 64 * 1024**2,
         'insufficient free swap')
    available = int(re.search(r'MemAvailable:\s+(\d+)', Path('/proc/meminfo').read_text())[1])
    need(available > 2 * 1024**2, 'keep 2 GiB available memory')
    need(Path('/sys/module/zswap/parameters/enabled').read_text().strip() == 'N', 'zswap must already be off')
    binary = HERE / 'build/remaining'
    need(binary.is_file(), 'run make first')
    out.mkdir(parents=True, exist_ok=False)
    environment = f'kernel={os.uname().release}\ncase={case}\nbinary_sha256={hashlib.sha256(binary.read_bytes()).hexdigest()}\n'
    (out / 'environment.txt').write_text(environment)
    cg = cgroot / f'ks-swap-remain-standalone-{os.getpid()}'
    cg.mkdir()
    try:
        (cg / 'memory.max').write_text('134217728\n')
        (cg / 'memory.swap.max').write_text('67108864\n')
        (cg / 'pids.max').write_text('16\n')
        with (out / 'stdout.log').open('x') as stdout, (out / 'stderr.log').open('x') as stderr:
            result = subprocess.run([str(binary), case], stdout=stdout, stderr=stderr,
                timeout=45, env={**os.environ, 'KS_SWAP_REMAIN_RUN': '1', 'KS_SW_CGROUP': str(cg)})
        need(result.returncode == 0 and not (out / 'stderr.log').read_text(), 'workload failed; do not silently retry')
        (out / 'summary.txt').write_text(parse((out / 'stdout.log').read_text(), case))
    finally:
        # Only this newly created, uniquely named group is affected.
        if 'populated 1' in (cg / 'cgroup.events').read_text():
            (cg / 'cgroup.kill').write_text('1\n')
        for _ in range(50):
            if 'populated 0' in (cg / 'cgroup.events').read_text():
                break
            time.sleep(.1)
        need('populated 0' in (cg / 'cgroup.events').read_text(), 'owned group still populated')
        deferred = (cg / 'memory.swap.current').read_text().strip()
        (out / 'cleanup.txt').write_text(f'populated=0\ndeferred_swap_bytes={deferred}\n')
        cg.rmdir()
    print((out / 'summary.txt').read_text(), end='')
    print('Raw output and cleanup status:', out)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('case', choices=CASES)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    run(args.case, args.out.resolve())
