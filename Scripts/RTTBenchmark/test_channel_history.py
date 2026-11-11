import unittest
import json
import tempfile
from pathlib import Path
from unittest.mock import patch
import xml.etree.ElementTree as ET
from channel_history import compatible, comparison_entries, main, render, render_trend, trend_entries


def result(sha, p95, delivery=100):
    metrics = dict(attempted=10, deliveryPercent=delivery, responsesPerSecond=5,
                   p50Ms=p95, p95Ms=p95, p99Ms=p95)
    return dict(commitSha=sha, scenarioName='unreliable-only', schemaVersion=1,
                sampleCount=10, warmupSampleCount=0, timeoutMs=1000,
                serverThreadCount=1, operatingSystem='test', processorCount=2,
                runs=[dict(unreliable=metrics, reliable=dict(attempted=0))])


class ChannelHistoryTests(unittest.TestCase):
    @staticmethod
    def pair(sha, p95=1):
        unreliable = result(sha, p95)
        unreliable['schemaVersion'] = 3
        reliable = dict(unreliable, scenarioName='reliable-baseline',
                        runs=[dict(reliable=dict(unreliable['runs'][0]['unreliable']),
                                   unreliable=dict(attempted=0))])
        return [reliable, unreliable]

    def test_comparison_requires_same_commit_version_conditions_and_request_counts(self):
        for mismatch in ('commitSha', 'schemaVersion', 'sampleCount', 'timeoutMs',
                         'warmupSampleCount', 'serverThreadCount', 'operatingSystem',
                         'processorCount', 'runCount'):
            pair = self.pair('new')
            pair[0][mismatch] = 'different'
            self.assertEqual([], comparison_entries(pair), mismatch)
        pair = self.pair('new')
        pair[0]['runs'][0]['reliable']['attempted'] = 1
        self.assertEqual([], comparison_entries(pair))
        self.assertEqual([], comparison_entries([result('old', 100)]))
        interval_pair = self.pair('interval')
        for entry in interval_pair:
            entry['schemaVersion'] = 2
        self.assertEqual([], comparison_entries(interval_pair))
        self.assertEqual([], comparison_entries(self.pair('new')[:1]))

    def test_comparison_keeps_latest_ten_pairs_and_shows_both_channels(self):
        history = [entry for i in range(12) for entry in self.pair(str(i), i + 1)]
        history += [result('legacy', 999)]
        self.assertEqual([str(i) for i in range(2, 12)],
                         [r['commitSha'] for r in comparison_entries(history)])
        svg = render_trend(history, None)
        ET.fromstring(svg)
        self.assertIn('reliable p95Ms: 12.000', svg)
        self.assertIn('unreliable p99Ms: 12.000', svg)
        self.assertIn('R response %', svg)
        self.assertNotIn('legacy', svg)
        self.assertIn('Awaiting a complete pair', render_trend([], None))

    def test_comparison_missing_rtt_breaks_all_latency_lines(self):
        history = self.pair('a') + self.pair('b', None) + self.pair('c', 2)
        root = ET.fromstring(render_trend(history, None))
        ns = {'s': 'http://www.w3.org/2000/svg'}
        latency_lines = [line for line in root.findall('s:line', ns)
                         if line.get('stroke') != '#30363d' and float(line.get('y1')) < 300]
        self.assertEqual([], latency_lines)

    def test_version_change_does_not_reuse_old_baseline(self):
        current = self.pair('new')[1]
        self.assertFalse(compatible(result('old', 2), current))
        self.assertNotIn('-50.00%', render([current], [result('old', 2)]))

    def test_cli_preserves_legacy_version_and_writes_comparison_artifact(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            old = root / 'old.json'
            old.write_text(json.dumps([result('same', 2)]), encoding='utf-8')
            files = []
            for index, entry in enumerate(self.pair('same')):
                file = root / f'{index}.json'
                file.write_text(json.dumps(entry), encoding='utf-8')
                files.append(str(file))
            args = ['channel_history.py', '--history', str(old), '--results', *files,
                    '--output-history', str(root / 'history.json'),
                    '--markdown', str(root / 'summary.md'), '--output-dir', str(root)]
            with patch('sys.argv', args):
                main()
            self.assertEqual(3, len(json.loads((root / 'history.json').read_text())))
            ET.fromstring((root / 'channel-comparison.svg').read_text(encoding='utf-8'))

    def test_trend_uses_latest_ten_matching_conditions_and_scenario(self):
        history = [result(str(i), i + 1) for i in range(12)]
        incompatible = result('different', 100)
        incompatible['sampleCount'] = 99
        mixed = result('mixed', 100)
        mixed['scenarioName'] = 'mixed'
        history[5:5] = [incompatible, mixed]
        self.assertEqual([str(i) for i in range(2, 12)],
                         [r['commitSha'] for r in trend_entries(history, 'unreliable-only')])
        self.assertIn('+50.00%', render_trend(history, 'unreliable-only'))

    def test_missing_rtt_breaks_line_and_preserves_zero_delivery(self):
        svg = render_trend([result('a', 1), result('b', None, 0), result('c', 2)], 'unreliable-only')
        root = ET.fromstring(svg)
        ns = {'s': 'http://www.w3.org/2000/svg'}
        rtt_lines = [line for line in root.findall('s:line', ns)
                     if line.get('stroke') in ('#58a6ff', '#f85149')]
        self.assertEqual([], rtt_lines)
        self.assertIn('deliveryPercent: 0.000', svg)
        self.assertIn('N/A', svg)
        self.assertNotIn('-100.00%', svg)

    def test_single_empty_and_escaped_trends_are_valid_svg(self):
        for history in ([], [result('<&', None, 0)], [result('one', 0)]):
            ET.fromstring(render_trend(history, 'unreliable-only'))

    def test_latency_and_delivery_are_compared_together(self):
        report = render([result('new', 2, 80)], [result('old', 1)])
        self.assertIn('+100.00%', report)
        self.assertIn('-20.000', report)

    def test_missing_responses_are_not_zero_latency(self):
        report = render([result('new', None, 0)], [result('old', 1)])
        self.assertIn('N/A', report)
        self.assertNotIn('-100.00%', report)

    def test_incompatible_conditions_and_same_commit_are_not_baselines(self):
        old, new = result('old', 1), result('new', 2)
        old['sampleCount'] = 20
        self.assertFalse(compatible(old, new))
        self.assertNotIn('+100.00%', render([new], [old]))
        self.assertNotIn('+100.00%', render([new], [result('new', 1)]))


if __name__ == '__main__':
    unittest.main()
