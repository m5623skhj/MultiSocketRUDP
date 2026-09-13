import unittest
from channel_history import compatible, render


def result(sha, p95, delivery=100):
    metrics = dict(attempted=10, deliveryPercent=delivery, responsesPerSecond=5,
                   p50Ms=p95, p95Ms=p95, p99Ms=p95)
    return dict(commitSha=sha, scenarioName='unreliable-only', schemaVersion=1,
                sampleCount=10, warmupSampleCount=0, timeoutMs=1000,
                serverThreadCount=1, operatingSystem='test', processorCount=2,
                runs=[dict(unreliable=metrics, reliable=dict(attempted=0))])


class ChannelHistoryTests(unittest.TestCase):
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
