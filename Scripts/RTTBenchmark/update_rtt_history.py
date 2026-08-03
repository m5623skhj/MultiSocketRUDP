#!/usr/bin/env python3
"""Update persisted RTT benchmark history and render the latest trend charts."""

from __future__ import annotations

import argparse
import html
import json
import math
from datetime import datetime, timezone
from pathlib import Path
from statistics import median
from typing import Any, Iterable


SCHEMA_VERSION = 1
CHART_WIDTH = 960
CHART_HEIGHT = 420
PLOT_LEFT = 78
PLOT_RIGHT = 930
PLOT_TOP = 112
PLOT_BOTTOM = 340


def load_history(path: Path | None) -> dict[str, Any]:
    if path is None or not path.exists():
        return {"schemaVersion": SCHEMA_VERSION, "entries": []}

    with path.open("r", encoding="utf-8") as history_file:
        history = json.load(history_file)

    if history.get("schemaVersion") != SCHEMA_VERSION:
        raise ValueError(f"Unsupported history schema: {history.get('schemaVersion')}")
    if not isinstance(history.get("entries"), list):
        raise ValueError("History entries must be an array.")
    return history


def load_results(paths: Iterable[Path]) -> list[dict[str, Any]]:
    results: list[dict[str, Any]] = []
    for path in paths:
        with path.open("r", encoding="utf-8") as result_file:
            result = json.load(result_file)
        if result.get("schemaVersion") != SCHEMA_VERSION:
            raise ValueError(f"Unsupported result schema in {path}.")
        results.append(result)

    if not results:
        raise ValueError("At least one benchmark result is required.")

    commit_shas = {result["commitSha"] for result in results}
    if len(commit_shas) != 1:
        raise ValueError("All benchmark results must belong to the same commit.")
    return sorted(results, key=lambda result: float(result["lossRate"]))


def create_entry(results: list[dict[str, Any]]) -> dict[str, Any]:
    first = results[0]
    recorded_at = max(result["recordedAtUtc"] for result in results)
    scenarios = []
    for result in results:
        scenarios.append(
            {
                "scenarioName": result["scenarioName"],
                "lossRate": result["lossRate"],
                "lossModel": result["lossModel"],
                "sampleCount": result["sampleCount"],
                "runCount": result["runCount"],
                "warmupSampleCount": result["warmupSampleCount"],
                "timeoutMs": result["timeoutMs"],
                "seedBase": result["seedBase"],
                "aggregate": result["aggregate"],
            }
        )

    return {
        "commitSha": first["commitSha"],
        "recordedAtUtc": recorded_at,
        "serverBuild": first["serverBuild"],
        "botTesterBuild": first["botTesterBuild"],
        "ioWorkerSleepMode": first["ioWorkerSleepMode"],
        "environment": first["environment"],
        "scenarios": scenarios,
    }


def upsert_entry(history: dict[str, Any], entry: dict[str, Any]) -> None:
    entries = history["entries"]
    for index, existing in enumerate(entries):
        if existing["commitSha"] == entry["commitSha"]:
            entries[index] = entry
            return
    entries.append(entry)


def find_scenario(entry: dict[str, Any], loss_rate: float) -> dict[str, Any] | None:
    for scenario in entry.get("scenarios", []):
        if math.isclose(float(scenario["lossRate"]), loss_rate, abs_tol=1e-9):
            return scenario
    return None


def percent_delta(current: float, previous: float | None) -> float | None:
    if previous is None or math.isclose(previous, 0.0, abs_tol=1e-15):
        return None
    return (current - previous) / previous * 100.0


def format_delta(delta: float | None) -> str:
    if delta is None:
        return "-"
    return f"{delta:+.2f}%"


def format_rtt(value_ms: float) -> str:
    if abs(value_ms) < 1.0:
        return f"{value_ms * 1000.0:.3f} µs"
    return f"{value_ms:.3f} ms"


def previous_scenario(history: dict[str, Any], entry: dict[str, Any], loss_rate: float) -> dict[str, Any] | None:
    for candidate in reversed(history["entries"]):
        if candidate["commitSha"] == entry["commitSha"]:
            continue
        scenario = find_scenario(candidate, loss_rate)
        if scenario is not None:
            return scenario
    return None


def render_markdown(history_before_update: dict[str, Any], entry: dict[str, Any]) -> str:
    lines = [
        "<!-- RTT_BENCHMARK_SUMMARY_MARKER -->",
        "## RTT Benchmark Result",
        "",
        f"Commit: `{entry['commitSha'][:12]}`  ",
        f"Server: `{entry['serverBuild']}`, IO worker: `{entry['ioWorkerSleepMode']}`  ",
        f"BotTester: `{entry['botTesterBuild']}`",
        "",
        "| Scenario | Samples × Runs | Median Avg | Median P95 | Δ P95 | Median P99 | Δ P99 | Worst Max |",
        "| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |",
    ]

    for scenario in entry["scenarios"]:
        aggregate = scenario["aggregate"]
        previous = previous_scenario(history_before_update, entry, float(scenario["lossRate"]))
        previous_aggregate = previous["aggregate"] if previous is not None else None
        p95_delta = percent_delta(
            float(aggregate["medianP95RttMs"]),
            float(previous_aggregate["medianP95RttMs"]) if previous_aggregate else None,
        )
        p99_delta = percent_delta(
            float(aggregate["medianP99RttMs"]),
            float(previous_aggregate["medianP99RttMs"]) if previous_aggregate else None,
        )
        lines.append(
            "| "
            + " | ".join(
                [
                    scenario["scenarioName"],
                    f"{scenario['sampleCount']:,} × {scenario['runCount']}",
                    format_rtt(float(aggregate["medianAverageRttMs"])),
                    format_rtt(float(aggregate["medianP95RttMs"])),
                    format_delta(p95_delta),
                    format_rtt(float(aggregate["medianP99RttMs"])),
                    format_delta(p99_delta),
                    format_rtt(float(aggregate["worstMaxRttMs"])),
                ]
            )
            + " |"
        )

    lines.extend(
        [
            "",
            "Positive deltas mean RTT increased. The 10% scenario applies loss independently to BotTester TX and RX.",
            "",
        ]
    )
    return "\n".join(lines)


def _axis_range(values: list[float]) -> tuple[float, float]:
    minimum = min(values)
    maximum = max(values)
    span = maximum - minimum
    padding = max(span * 0.20, maximum * 0.05, 0.001)
    return max(0.0, minimum - padding), maximum + padding


def _format_axis_value(value: float, use_microseconds: bool) -> str:
    return f"{value * 1000.0:.1f}" if use_microseconds else f"{value:.2f}"


def _rolling_delta(entries: list[dict[str, Any]], loss_rate: float, metric: str) -> float | None:
    if len(entries) < 2:
        return None
    current_scenario = find_scenario(entries[-1], loss_rate)
    if current_scenario is None:
        return None

    prior_values: list[float] = []
    for entry in reversed(entries[:-1]):
        scenario = find_scenario(entry, loss_rate)
        if scenario is not None:
            prior_values.append(float(scenario["aggregate"][metric]))
        if len(prior_values) == 5:
            break
    if not prior_values:
        return None
    return percent_delta(float(current_scenario["aggregate"][metric]), median(prior_values))


def render_svg(history: dict[str, Any], loss_rate: float, max_points: int) -> str:
    entries = [entry for entry in history["entries"] if find_scenario(entry, loss_rate) is not None][-max_points:]
    if not entries:
        raise ValueError(f"No history exists for loss rate {loss_rate}.")

    p95_values = [float(find_scenario(entry, loss_rate)["aggregate"]["medianP95RttMs"]) for entry in entries]
    p99_values = [float(find_scenario(entry, loss_rate)["aggregate"]["medianP99RttMs"]) for entry in entries]
    all_values = p95_values + p99_values
    use_microseconds = max(all_values) < 1.0
    axis_min, axis_max = _axis_range(all_values)

    def x_position(index: int) -> float:
        if len(entries) == 1:
            return (PLOT_LEFT + PLOT_RIGHT) / 2.0
        return PLOT_LEFT + (PLOT_RIGHT - PLOT_LEFT) * index / (len(entries) - 1)

    def y_position(value: float) -> float:
        ratio = (value - axis_min) / (axis_max - axis_min)
        return PLOT_BOTTOM - ratio * (PLOT_BOTTOM - PLOT_TOP)

    title = "RTT trend — packet loss 0%" if math.isclose(loss_rate, 0.0) else "RTT trend — BotTester TX/RX loss 10%"
    p95_rolling = _rolling_delta(entries, loss_rate, "medianP95RttMs")
    p99_rolling = _rolling_delta(entries, loss_rate, "medianP99RttMs")
    subtitle = (
        f"Latest vs previous 5 measurements median: P95 {format_delta(p95_rolling)}, "
        f"P99 {format_delta(p99_rolling)}"
    )

    svg = [
        f'<svg xmlns="http://www.w3.org/2000/svg" width="{CHART_WIDTH}" height="{CHART_HEIGHT}" viewBox="0 0 {CHART_WIDTH} {CHART_HEIGHT}">',
        '<rect width="100%" height="100%" fill="#0d1117" rx="12"/>',
        '<style>text{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;fill:#c9d1d9}.axis{font-size:12px}.label{font-size:11px}.title{font-size:22px;font-weight:600}.subtitle{font-size:13px}.legend{font-size:13px;font-weight:600}</style>',
        f'<text x="28" y="38" class="title">{html.escape(title)}</text>',
        f'<text x="28" y="65" class="subtitle">{html.escape(subtitle)}</text>',
        '<line x1="730" y1="36" x2="760" y2="36" stroke="#58a6ff" stroke-width="3"/><text x="768" y="41" class="legend">P95</text>',
        '<line x1="830" y1="36" x2="860" y2="36" stroke="#f85149" stroke-width="3"/><text x="868" y="41" class="legend">P99</text>',
    ]

    for grid_index in range(6):
        ratio = grid_index / 5.0
        y = PLOT_BOTTOM - ratio * (PLOT_BOTTOM - PLOT_TOP)
        value = axis_min + ratio * (axis_max - axis_min)
        svg.append(f'<line x1="{PLOT_LEFT}" y1="{y:.2f}" x2="{PLOT_RIGHT}" y2="{y:.2f}" stroke="#30363d" stroke-width="1"/>')
        svg.append(f'<text x="{PLOT_LEFT - 10}" y="{y + 4:.2f}" text-anchor="end" class="axis">{_format_axis_value(value, use_microseconds)}</text>')

    unit = "µs" if use_microseconds else "ms"
    svg.append(f'<text x="18" y="{(PLOT_TOP + PLOT_BOTTOM) / 2:.2f}" transform="rotate(-90 18 {(PLOT_TOP + PLOT_BOTTOM) / 2:.2f})" text-anchor="middle" class="axis">RTT ({unit})</text>')

    for index, entry in enumerate(entries):
        x = x_position(index)
        date_label = datetime.fromisoformat(entry["recordedAtUtc"].replace("Z", "+00:00")).strftime("%m-%d")
        svg.append(f'<text x="{x:.2f}" y="367" text-anchor="middle" class="label">{html.escape(entry["commitSha"][:7])}</text>')
        svg.append(f'<text x="{x:.2f}" y="384" text-anchor="middle" class="label">{date_label}</text>')

    for values, color in ((p95_values, "#58a6ff"), (p99_values, "#f85149")):
        points = " ".join(f"{x_position(index):.2f},{y_position(value):.2f}" for index, value in enumerate(values))
        svg.append(f'<polyline points="{points}" fill="none" stroke="{color}" stroke-width="3" stroke-linejoin="round" stroke-linecap="round"/>')
        for index, value in enumerate(values):
            svg.append(f'<circle cx="{x_position(index):.2f}" cy="{y_position(value):.2f}" r="4" fill="{color}"><title>{format_rtt(value)}</title></circle>')

    svg.append('<text x="930" y="405" text-anchor="end" class="label">Latest 10 official main measurements · adjusted Y axis</text>')
    svg.append("</svg>")
    return "\n".join(svg)


def render_data_readme(entry: dict[str, Any]) -> str:
    return "\n".join(
        [
            "# RTT benchmark history",
            "",
            "The JSON file keeps the complete official history. Charts render the latest 10 `main` measurements.",
            "",
            "## Packet loss 0%",
            "",
            "![RTT loss 0%](./rtt-loss-0.svg)",
            "",
            "## BotTester TX/RX loss 10%",
            "",
            "![RTT loss 10%](./rtt-loss-10.svg)",
            "",
            f"Last updated by `{entry['commitSha']}` at {entry['recordedAtUtc']}.",
            "",
        ]
    )


def write_outputs(
    history_path: Path | None,
    result_paths: list[Path],
    output_history_path: Path,
    output_directory: Path,
    markdown_path: Path,
    max_points: int,
) -> None:
    history = load_history(history_path)
    history_before_update = json.loads(json.dumps(history))
    results = load_results(result_paths)
    entry = create_entry(results)
    upsert_entry(history, entry)

    output_history_path.parent.mkdir(parents=True, exist_ok=True)
    output_directory.mkdir(parents=True, exist_ok=True)
    markdown_path.parent.mkdir(parents=True, exist_ok=True)

    output_history_path.write_text(
        json.dumps(history, ensure_ascii=False, indent=2) + "\n",
        encoding="utf-8",
    )
    markdown_path.write_text(render_markdown(history_before_update, entry), encoding="utf-8")
    (output_directory / "rtt-loss-0.svg").write_text(render_svg(history, 0.0, max_points), encoding="utf-8")
    (output_directory / "rtt-loss-10.svg").write_text(render_svg(history, 0.1, max_points), encoding="utf-8")
    (output_directory / "README.md").write_text(render_data_readme(entry), encoding="utf-8")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--history", type=Path)
    parser.add_argument("--results", type=Path, nargs="+", required=True)
    parser.add_argument("--output-history", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--markdown", type=Path, required=True)
    parser.add_argument("--max-points", type=int, default=10)
    args = parser.parse_args()
    if args.max_points <= 0:
        parser.error("--max-points must be positive")
    return args


def main() -> int:
    args = parse_args()
    write_outputs(
        args.history,
        args.results,
        args.output_history,
        args.output_dir,
        args.markdown,
        args.max_points,
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
