"""Keep compositor FPS and changing display settings out of Quest comparisons."""
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

TOOL = Path(__file__).resolve().parents[1] / 'Tools' / 'Summarize-VrApi.py'


def sample(second, pid=123, fps=51, hz=90, scale=.8, app=10):
    return (f'10-17 08:00:{second:02}.000 {pid} 234 I VrApi : '
            f'FPS={fps}/{hz},App={app}ms,GPU%=0.7,SF={scale},Stale=39\n')


class SummaryTests(unittest.TestCase):
    def run_summary(self, lines, *args):
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / 'logcat.txt'
            log.write_text(''.join(lines), encoding='utf-8')
            return subprocess.run([sys.executable, str(TOOL), str(log), '--pid', '123', *args],
                                  capture_output=True, text=True)

    def test_only_app_samples(self):
        result = self.run_summary([sample(1), sample(1, pid=999, fps=90), sample(2)],
                                  '--min-samples', '2', '--expected-hz', '90', '--expected-scale', '.8')
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report['samples'], 2)
        self.assertEqual(report['mean']['fps'], 51)

    def test_short_capture_rejected(self):
        self.assertNotEqual(self.run_summary([sample(1)], '--min-samples', '2').returncode, 0)

    def test_refresh_change_rejected(self):
        self.assertIn('Display settings changed', self.run_summary([sample(1), sample(2, hz=72)]).stderr)

    def test_resolution_change_rejected(self):
        self.assertIn('Display settings changed', self.run_summary([sample(1), sample(2, scale=.7)]).stderr)

    def test_reference_mismatch_rejected(self):
        for args in [('--expected-hz', '72'), ('--expected-scale', '.7')]:
            self.assertIn('differ from reference', self.run_summary([sample(1)], *args).stderr)

    def test_invalid_app_duration_does_not_discard_fps(self):
        result = self.run_summary([sample(1, app=1e14), sample(2, app=9)])
        self.assertEqual(result.returncode, 0, result.stderr)
        report = json.loads(result.stdout)
        self.assertEqual(report['mean']['fps'], 51)
        self.assertEqual(report['mean']['appMs'], 9)
        self.assertEqual(report['validAppSamples'], 1)


if __name__ == '__main__':
    unittest.main()
