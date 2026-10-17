import unittest

import run_once
import verify_results


class CuratedDataTests(unittest.TestCase):
    def setUp(self):
        self.rows = verify_results.rows(verify_results.ROOT / 'bare-metal/cycles.tsv')

    def raw(self, case='serial-thp'):
        rows = [r for r in self.rows if r['point'] == 'prototype' and r['case'] == case and r['invocation'] == '2']
        workers = 2 if case.startswith('parallel') else 1
        text = ''.join('CYCLE ' + ' '.join(k + '=' + v for k, v in r.items()
            if k not in ('point', 'case', 'invocation', 'warmup')) + '\n' for r in rows)
        text += ''.join(f'BARRIER round={i} includes_checks=1 includes_fixed_wait=1\n' for i in range(4))
        return text + f'RESULT family=F02 reaped={workers} total_bytes=8388608 content_ok=1\n'

    def test_all_curated_results(self):
        self.assertTrue(verify_results.verify().startswith('pass:'))

    def test_four_shapes(self):
        for case in run_once.CASES:
            with self.subTest(case=case):
                self.assertIn('api_worker_cpu_us_per_8MiB=', run_once.parse(self.raw(case), case))

    def test_missing_cycle(self):
        with self.assertRaises(RuntimeError):
            run_once.parse(self.raw().split('\n', 1)[1], 'serial-thp')

    def test_cold_cache(self):
        with self.assertRaises(RuntimeError):
            run_once.parse(self.raw().replace('ready=2048', 'ready=2047', 1), 'serial-thp')

    def test_wrong_page_shape(self):
        with self.assertRaises(RuntimeError):
            run_once.parse(self.raw().replace('thp_after=0', 'thp_after=2048', 1), 'serial-thp')

    def test_bad_data(self):
        with self.assertRaises(RuntimeError):
            run_once.parse(self.raw().replace('content_ok=1', 'content_ok=0', 1), 'serial-thp')

    def test_wrong_cpu(self):
        with self.assertRaises(RuntimeError):
            run_once.parse(self.raw().replace('cpu=2', 'cpu=3', 1), 'serial-thp')

    def test_zero_clock(self):
        import re
        bad = re.sub(r'pageout_cpu_ns=\d+', 'pageout_cpu_ns=0', self.raw(), count=1)
        with self.assertRaises(RuntimeError):
            run_once.parse(bad, 'serial-thp')


if __name__ == '__main__':
    unittest.main()
