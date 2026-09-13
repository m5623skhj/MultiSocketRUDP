"""Record channel RTT and delivery together; never gate CI on latency changes."""
import argparse
import json
import statistics
from pathlib import Path


def compatible(left, right):
    keys = ('schemaVersion', 'scenarioName', 'sampleCount', 'warmupSampleCount',
            'timeoutMs', 'serverThreadCount', 'operatingSystem', 'processorCount', 'runCount')
    return all(left.get(key) == right.get(key) for key in keys)


def metric(result, channel, name):
    values = [run[channel][name] for run in result['runs'] if run[channel]['attempted'] > 0]
    values = [value for value in values if value is not None]
    return statistics.median(values) if values else None


def render(results, history):
    lines = ['## Channel RTT / delivery', '',
             'RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.',
             'Values are medians across runs. Throughput includes the fixed drain interval. Latency changes are informational.', '',
             '| Scenario | Channel | Delivery % | Responses/s | p50 ms | p95 ms | p99 ms | Previous p95 delta | Delivery delta (pp) |',
             '|---|---|---:|---:|---:|---:|---:|---:|---:|']
    def fmt(value):
        return 'N/A' if value is None else f'{value:.3f}'
    for result in results:
        previous = next((old for old in reversed(history)
                         if old['commitSha'] != result['commitSha'] and compatible(old, result)), None)
        for channel in ('reliable', 'unreliable'):
            if not any(run[channel]['attempted'] for run in result['runs']):
                continue
            values = [metric(result, channel, name) for name in
                      ('deliveryPercent', 'responsesPerSecond', 'p50Ms', 'p95Ms', 'p99Ms')]
            old_p95 = metric(previous, channel, 'p95Ms') if previous else None
            old_delivery = metric(previous, channel, 'deliveryPercent') if previous else None
            delta = f'{100 * (values[3] / old_p95 - 1):+.2f}%' if old_p95 and values[3] is not None else 'N/A'
            delivery_delta = fmt(values[0] - old_delivery) if old_delivery is not None else 'N/A'
            lines.append('| ' + ' | '.join([result['scenarioName'], channel, *map(fmt, values), delta, delivery_delta]) + ' |')
    baseline = next((r for r in results if r['scenarioName'] == 'reliable-baseline'), None)
    mixed = next((r for r in results if r['scenarioName'] == 'mixed'), None)
    if baseline and mixed:
        base = metric(baseline, 'reliable', 'p95Ms')
        value = metric(mixed, 'reliable', 'p95Ms')
        if base and value is not None:
            lines += ['', f'Reliable p95 under mixed load versus reliable-only: {100 * (value / base - 1):+.2f}%.']
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--history')
    parser.add_argument('--results', nargs='+', required=True)
    parser.add_argument('--output-history', required=True)
    parser.add_argument('--markdown', required=True)
    args = parser.parse_args()
    history = json.loads(Path(args.history).read_text(encoding='utf-8-sig')) if args.history else []
    results = [json.loads(Path(path).read_text(encoding='utf-8-sig')) for path in args.results]
    markdown = render(results, history)
    keys = {(r['commitSha'], r['scenarioName']) for r in results}
    history = [r for r in history if (r['commitSha'], r['scenarioName']) not in keys] + results
    for path, content in ((args.output_history, json.dumps(history[-150:], indent=2)), (args.markdown, markdown)):
        target = Path(path)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding='utf-8')


if __name__ == '__main__':
    main()
