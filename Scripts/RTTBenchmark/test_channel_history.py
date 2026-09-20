import unittest
import xml.etree.ElementTree as ET
from channel_history import compatible, render, render_trend, trend_entries


def result(sha, p95, delivery=100):
    metrics = dict(attempted=10, deliveryPercent=delivery, responsesPerSecond=5,
                   p50Ms=p95, p95Ms=p95, p99Ms=p95)
    return dict(commitSha=sha, scenarioName='unreliable-only', schemaVersion=1,
                sampleCount=10, warmupSampleCount=0, timeoutMs=1000,
                serverThreadCount=1, operatingSystem='test', processorCount=2,
                runs=[dict(unreliable=metrics, reliable=dict(attempted=0))])


class ChannelHistoryTests(unittest.TestCase):
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
