"""Record channel RTT and delivery together; never gate CI on latency changes."""
import argparse
import html
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


def trend_entries(history, scenario):
    entries = [entry for entry in history if entry['scenarioName'] == scenario]
    return [entry for entry in entries if compatible(entry, entries[-1])][-10:] if entries else []


def render_trend(history, scenario):
    """Keep missing RTT points as gaps and show delivery on the same commit axis."""
    entries = trend_entries(history, scenario)
    height = 650 + 24 * len(entries)
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="960" height="{height}" viewBox="0 0 960 {height}">',
           '<rect width="100%" height="100%" rx="12" fill="#0d1117"/>',
           '<style>text{font:13px sans-serif;fill:#c9d1d9}.title{font-size:22px;font-weight:bold}</style>',
           f'<text x="28" y="38" class="title">Unreliable channel: {html.escape(scenario)}</text>',
           '<text x="28" y="66">Latest 10 compatible main measurements · medians across runs</text>']

    def label(x, y, value):
        svg.append(f'<text x="{x}" y="{y}">{html.escape(str(value))}</text>')

    def x(index):
        return 490 if len(entries) == 1 else 80 + 820 * index / max(1, len(entries) - 1)

    for top, bottom, names, colors, unit in (
            (115, 290, ('p95Ms', 'p99Ms'), ('#58a6ff', '#f85149'), 'RTT (ms)'),
            (365, 490, ('deliveryPercent',), ('#3fb950',), 'Responses / attempted (%)')):
        series = [[metric(entry, 'unreliable', name) for entry in entries] for name in names]
        values = [value for values in series for value in values if value is not None]
        maximum = 100 if len(names) == 1 else max(max(values, default=0) * 1.1, 0.001)
        label(28, top - 18, unit)
        for i in range(5):
            y = bottom - (bottom - top) * i / 4
            svg.append(f'<line x1="80" y1="{y}" x2="900" y2="{y}" stroke="#30363d"/>')
            label(15, y + 4, f'{maximum * i / 4:.3f}')
        for values, color, name in zip(series, colors, names):
            previous = None
            for index, value in enumerate(values):
                if value is None:
                    previous = None
                    label(x(index) - 12, (top + bottom) / 2, 'N/A')
                    continue
                point = (x(index), bottom - value / maximum * (bottom - top))
                if previous is not None:
                    svg.append(f'<line x1="{previous[0]}" y1="{previous[1]}" x2="{point[0]}" y2="{point[1]}" stroke="{color}" stroke-width="2"/>')
                svg.append(f'<circle cx="{point[0]}" cy="{point[1]}" r="4" fill="{color}"><title>{name}: {value:.3f}</title></circle>')
                previous = point
        for index, entry in enumerate(entries):
            label(x(index) - 25, bottom + 22, entry['commitSha'][:7])
    label(650, 97, 'P95: blue · P99: red')
    label(28, 548, 'Missing responses are excluded from RTT. Read latency together with response rate.')
    for xpos, text in ((28, 'Commit'), (180, 'P95 ms'), (330, 'P99 ms'), (480, 'Previous P95'), (650, 'Previous P99'), (820, 'Response %')):
        label(xpos, 585, text)
    for index, entry in enumerate(entries):
        y = 615 + index * 24
        label(28, y, entry['commitSha'][:12])
        for name, xpos, delta_x in (('p95Ms', 180, 480), ('p99Ms', 330, 650)):
            value = metric(entry, 'unreliable', name)
            previous = next((old for old in reversed(history[:history.index(entry)])
                             if old['commitSha'] != entry['commitSha'] and compatible(old, entry)), None)
            old_value = metric(previous, 'unreliable', name) if previous else None
            label(xpos, y, 'N/A' if value is None else f'{value:.3f}')
            label(delta_x, y, f'{100 * (value / old_value - 1):+.2f}%' if old_value and value is not None else 'N/A')
        delivery = metric(entry, 'unreliable', 'deliveryPercent')
        label(820, y, 'N/A' if delivery is None else f'{delivery:.3f}')
    svg.append('</svg>')
    return '\n'.join(svg)


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
    parser.add_argument('--output-dir', help='Render official channel trend SVGs in this directory')
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
    if args.output_dir:
        output = Path(args.output_dir)
        output.mkdir(parents=True, exist_ok=True)
        for scenario in ('unreliable-only', 'mixed'):
            (output / f'channel-{scenario}.svg').write_text(render_trend(history[-150:], scenario), encoding='utf-8')


if __name__ == '__main__':
    main()
