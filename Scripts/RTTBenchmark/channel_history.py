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
    values = [run[channel].get(name) for run in result['runs'] if run[channel]['attempted'] > 0]
    values = [value for value in values if value is not None]
    return statistics.median(values) if values else None


def display_metric(value, name):
    """Convert RTT only for display; history and comparison arithmetic stay in ms."""
    return value * 1000 if value is not None and name.endswith('Ms') else value


def format_metric(value, name):
    if value is None:
        return 'N/A'
    return f'{display_metric(value, name):.1f}' if name.endswith('Ms') else f'{value:.3f}'


def trend_entries(history, scenario):
    entries = [entry for entry in history if entry['scenarioName'] == scenario]
    return [entry for entry in entries if compatible(entry, entries[-1])][-10:] if entries else []


def comparison_entries(history):
    """Pair only version-3 standalone results from the same commit and conditions."""
    pairs = []
    for unreliable in history:
        if unreliable['scenarioName'] != 'unreliable-only' or unreliable['schemaVersion'] != 3:
            continue
        reliable = next((entry for entry in reversed(history)
                         if entry['scenarioName'] == 'reliable-baseline'
                         and entry['commitSha'] == unreliable['commitSha']
                         and compatible(dict(entry, scenarioName='unreliable-only'), unreliable)), None)
        if reliable is None or len(reliable['runs']) != len(unreliable['runs']):
            continue
        if not all(run[channel]['attempted'] == entry['sampleCount']
                   for entry, channel in ((reliable, 'reliable'), (unreliable, 'unreliable'))
                   for run in entry['runs']):
            continue
        pairs.append(dict(unreliable, runs=[dict(reliable=r['reliable'], unreliable=u['unreliable'])
                                           for r, u in zip(reliable['runs'], unreliable['runs'])]))
    return [entry for entry in pairs if compatible(entry, pairs[-1])][-10:] if pairs else []


def render_trend(history, scenario):
    """Keep missing RTT points as gaps and show delivery on the same commit axis."""
    comparison = scenario is None
    entries = comparison_entries(history) if comparison else trend_entries(history, scenario)
    title = 'Reliable vs unreliable · sequential Echo RTT' if comparison else f'Unreliable channel: {scenario}'
    height = 650 + 24 * len(entries)
    svg = [f'<svg xmlns="http://www.w3.org/2000/svg" width="960" height="{height}" viewBox="0 0 960 {height}">',
           '<rect width="100%" height="100%" rx="12" fill="#0d1117"/>',
           '<style>text{font:13px sans-serif;fill:#c9d1d9}.title{font-size:22px;font-weight:bold}</style>',
           f'<text x="28" y="38" class="title">{html.escape(title)}</text>',
           '<text x="28" y="66">Latest 10 compatible main measurements · medians across runs</text>']

    def label(x, y, value):
        svg.append(f'<text x="{x}" y="{y}">{html.escape(str(value))}</text>')

    def x(index):
        return 490 if len(entries) == 1 else 80 + 820 * index / max(1, len(entries) - 1)

    for top, bottom, names, colors, unit in (
            (115, 290, ('p95Ms', 'p99Ms'), ('#58a6ff', '#f85149'), 'RTT (µs)'),
            (365, 490, ('deliveryPercent',), ('#3fb950',), 'Responses / attempted (%)')):
        channels = ('reliable', 'unreliable') if comparison else ('unreliable',)
        specs = [(channel, name) for channel in channels for name in names]
        if comparison:
            colors = ('#58a6ff', '#79c0ff', '#f0883e', '#ffa657') if len(names) == 2 else ('#58a6ff', '#f0883e')
        series = [[display_metric(metric(entry, channel, name), name) for entry in entries] for channel, name in specs]
        values = [value for values in series for value in values if value is not None]
        maximum = 100 if len(names) == 1 else max(max(values, default=0) * 1.1, 1.0)
        label(28, top - 18, unit)
        for i in range(5):
            y = bottom - (bottom - top) * i / 4
            svg.append(f'<line x1="80" y1="{y}" x2="900" y2="{y}" stroke="#30363d"/>')
            label(15, y + 4, f'{maximum * i / 4:.3f}' if len(names) == 1 else f'{maximum * i / 4:.1f}')
        for values, color, (channel, name) in zip(series, colors, specs):
            previous = None
            for index, value in enumerate(values):
                if value is None:
                    previous = None
                    label(x(index) - 12, (top + bottom) / 2, 'N/A')
                    continue
                point = (x(index), bottom - value / maximum * (bottom - top))
                if previous is not None:
                    dash = ' stroke-dasharray="6 4"' if comparison and name == 'p99Ms' else ''
                    svg.append(f'<line x1="{previous[0]}" y1="{previous[1]}" x2="{point[0]}" y2="{point[1]}" stroke="{color}" stroke-width="2"{dash}/>')
                tooltip = f'{channel} {name[:-2].upper()}: {value:.1f} µs' if name.endswith('Ms') else f'{channel} {name}: {value:.3f}%'
                svg.append(f'<circle cx="{point[0]}" cy="{point[1]}" r="4" fill="{color}"><title>{tooltip}</title></circle>')
                previous = point
        for index, entry in enumerate(entries):
            label(x(index) - 25, bottom + 22, entry['commitSha'][:7])
    if comparison:
        label(300, 97, 'Reliable: blue · Unreliable: orange · P95: solid · P99: dashed')
        label(28, 532, 'v3 · Same 9-byte Echo · Next request after response/timeout · No fixed send interval')
        if entries:
            latest = entries[-1]
            label(28, 574, f"Per channel/run: {latest['sampleCount']} requests · {latest['warmupSampleCount']} warmup · {len(latest['runs'])} runs · timeout {latest['timeoutMs']} ms")
        else:
            label(28, 574, 'Awaiting a complete pair of compatible v3 standalone measurements.')
    else:
        label(650, 97, 'P95: blue · P99: red')
    label(28, 552 if comparison else 548, 'Missing responses are excluded from RTT. Read latency together with response rate.')
    headers = ((28, 'Commit'), (180, 'R P95 µs'), (300, 'R P99 µs'), (420, 'U P95 µs'), (540, 'U P99 µs'), (670, 'R response %'), (820, 'U response %')) if comparison else ((28, 'Commit'), (180, 'P95 µs'), (330, 'P99 µs'), (480, 'Previous P95'), (650, 'Previous P99'), (820, 'Response %'))
    for xpos, text in headers:
        label(xpos, 600 if comparison else 585, text)
    for index, entry in enumerate(entries):
        y = (630 if comparison else 615) + index * 24
        label(28, y, entry['commitSha'][:12])
        if comparison:
            for channel, name, xpos in (('reliable', 'p95Ms', 180), ('reliable', 'p99Ms', 300),
                                       ('unreliable', 'p95Ms', 420), ('unreliable', 'p99Ms', 540),
                                       ('reliable', 'deliveryPercent', 670), ('unreliable', 'deliveryPercent', 820)):
                value = metric(entry, channel, name)
                label(xpos, y, format_metric(value, name))
            continue
        for name, xpos, delta_x in (('p95Ms', 180, 480), ('p99Ms', 330, 650)):
            value = metric(entry, 'unreliable', name)
            previous = next((old for old in reversed(history[:history.index(entry)])
                             if old['commitSha'] != entry['commitSha'] and compatible(old, entry)), None)
            old_value = metric(previous, 'unreliable', name) if previous else None
            label(xpos, y, format_metric(value, name))
            label(delta_x, y, f'{100 * (value / old_value - 1):+.2f}%' if old_value and value is not None else 'N/A')
        delivery = metric(entry, 'unreliable', 'deliveryPercent')
        label(820, y, 'N/A' if delivery is None else f'{delivery:.3f}')
    svg.append('</svg>')
    return '\n'.join(svg)


def render(results, history):
    lines = ['## Channel RTT / delivery', '',
             'RTT is measured per request ID, including send queue delay. Missing responses are excluded from RTT and included in delivery loss.',
             'Values are medians across runs. Standalone v3 waits for each response/timeout; mixed throughput includes the drain interval. RTT includes send queue delay. Latency changes are informational.', '',
             '| Scenario | Channel | Delivery % | Responses/s | p50 µs | p95 µs | p99 µs | Timeouts | Previous p95 delta | Delivery delta (pp) |',
             '|---|---|---:|---:|---:|---:|---:|---:|---:|---:|']
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
            # Only v3 standalone measurements wait for each request's deadline.
            values.append(metric(result, channel, 'timedOut') if result['schemaVersion'] >= 3 else None)
            old_p95 = metric(previous, channel, 'p95Ms') if previous else None
            old_delivery = metric(previous, channel, 'deliveryPercent') if previous else None
            delta = f'{100 * (values[3] / old_p95 - 1):+.2f}%' if old_p95 and values[3] is not None else 'N/A'
            delivery_delta = fmt(values[0] - old_delivery) if old_delivery is not None else 'N/A'
            names = ('deliveryPercent', 'responsesPerSecond', 'p50Ms', 'p95Ms', 'p99Ms', 'timedOut')
            displayed = [format_metric(value, name) for value, name in zip(values, names)]
            lines.append('| ' + ' | '.join([result['scenarioName'], channel, *displayed, delta, delivery_delta]) + ' |')
    baseline = next((r for r in results if r['scenarioName'] == 'reliable-baseline'), None)
    mixed = next((r for r in results if r['scenarioName'] == 'mixed'), None)
    if baseline and mixed and baseline['schemaVersion'] == mixed['schemaVersion']:
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
    keys = {(r['commitSha'], r['scenarioName'], r['schemaVersion']) for r in results}
    history = [r for r in history if (r['commitSha'], r['scenarioName'], r['schemaVersion']) not in keys] + results
    for path, content in ((args.output_history, json.dumps(history[-150:], indent=2)), (args.markdown, markdown)):
        target = Path(path)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(content, encoding='utf-8')
    if args.output_dir:
        output = Path(args.output_dir)
        output.mkdir(parents=True, exist_ok=True)
        (output / 'channel-comparison.svg').write_text(render_trend(history[-150:], None), encoding='utf-8')
        for scenario in ('unreliable-only', 'mixed'):
            (output / f'channel-{scenario}.svg').write_text(render_trend(history[-150:], scenario), encoding='utf-8')


if __name__ == '__main__':
    main()
