#!/usr/bin/env python3

import json
import tempfile
import unittest
from pathlib import Path
from types import SimpleNamespace
from unittest.mock import patch

from update_rtt_history import load_history, populate_commit_subjects, write_outputs


def make_result(commit: str, loss_rate: float, p95: float, p99: float) -> dict:
    return {
        "schemaVersion": 1,
        "commitSha": commit,
        "recordedAtUtc": "2026-08-03T00:00:00+00:00",
        "scenarioName": f"Loss {loss_rate:.0%}",
        "lossRate": loss_rate,
        "lossModel": "bot-tx-and-rx-independent",
        "sampleCount": 100,
        "runCount": 3,
        "warmupSampleCount": 10,
        "timeoutMs": 5000,
        "runTimeoutSeconds": 300,
        "serverThreadCount": 1,
        "seedBase": 1000,
        "serverBuild": "Release /O2",
        "botTesterBuild": "Release / Optimize=true",
        "ioWorkerSleepMode": "NO_USE_IO_WORKER_THREAD_SLEEP_FOR_FRAME",
        "environment": {"operatingSystem": "Windows", "processorCount": 4},
        "aggregate": {
            "medianAverageRttMs": p95 / 2,
            "medianMinRttMs": 0.001,
            "medianP50RttMs": p95 / 2,
            "medianP95RttMs": p95,
            "medianP99RttMs": p99,
            "medianMaxRttMs": p99 * 2,
            "worstMaxRttMs": p99 * 3,
            "medianElapsedSeconds": 1.0,
            "totalRetransmissionSuspectedCount": 0,
        },
        "runs": [],
    }


class UpdateRttHistoryTests(unittest.TestCase):
    @patch("update_rtt_history.subprocess.run")
    def test_populate_commit_subjects_backfills_missing_titles(self, run_mock) -> None:
        run_mock.return_value = SimpleNamespace(returncode=0, stdout="* Fix receive queue\n")
        history = {
            "schemaVersion": 1,
            "entries": [
                {"commitSha": "abc123"},
                {"commitSha": "def456", "commitSubject": "* Existing title"},
            ],
        }

        populate_commit_subjects(history, Path("repository"))

        self.assertEqual("* Fix receive queue", history["entries"][0]["commitSubject"])
        self.assertEqual("* Existing title", history["entries"][1]["commitSubject"])
        run_mock.assert_called_once()

    def test_write_outputs_appends_results_and_renders_charts(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            result_paths = []
            for file_name, result in (
                ("loss-0.json", make_result("abc123", 0.0, 0.05, 0.08)),
                ("loss-10.json", make_result("abc123", 0.1, 20.0, 40.0)),
            ):
                path = root / file_name
                path.write_text(json.dumps(result), encoding="utf-8")
                result_paths.append(path)

            history_path = root / "rtt-history.json"
            chart_directory = root / "charts"
            markdown_path = root / "summary.md"
            write_outputs(
                None,
                result_paths,
                history_path,
                chart_directory,
                markdown_path,
                10,
                "* Improve receive completion handling",
            )

            history = load_history(history_path)
            self.assertEqual(1, len(history["entries"]))
            self.assertEqual("* Improve receive completion handling", history["entries"][0]["commitSubject"])

            chart = (chart_directory / "rtt-loss-0.svg").read_text(encoding="utf-8")
            self.assertIn("P95", chart)
            self.assertIn("2026-08-03", chart)
            self.assertIn("Improve receive completion handling", chart)
            self.assertIn("0.050 ms", chart)

            summary = markdown_path.read_text(encoding="utf-8")
            self.assertIn("Loss 0%", summary)
            self.assertIn("Measured at (UTC)", summary)
            self.assertIn("Improve receive completion handling", summary)

            data_readme = (chart_directory / "README.md").read_text(encoding="utf-8")
            self.assertIn("Recent measurements", data_readme)
            self.assertIn("0.050 ms", data_readme)

    def test_same_commit_replaces_existing_entry(self) -> None:
        with tempfile.TemporaryDirectory() as temporary_directory:
            root = Path(temporary_directory)
            history_path = root / "rtt-history.json"
            chart_directory = root / "charts"
            markdown_path = root / "summary.md"

            for p95 in (0.05, 0.07):
                result_paths = []
                for file_name, result in (
                    ("loss-0.json", make_result("same-sha", 0.0, p95, 0.08)),
                    ("loss-10.json", make_result("same-sha", 0.1, 20.0, 40.0)),
                ):
                    path = root / file_name
                    path.write_text(json.dumps(result), encoding="utf-8")
                    result_paths.append(path)
                write_outputs(history_path if history_path.exists() else None, result_paths, history_path, chart_directory, markdown_path, 10)

            history = load_history(history_path)
            self.assertEqual(1, len(history["entries"]))
            self.assertEqual(0.07, history["entries"][0]["scenarios"][0]["aggregate"]["medianP95RttMs"])


if __name__ == "__main__":
    unittest.main()
