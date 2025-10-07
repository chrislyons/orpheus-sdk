#!/usr/bin/env python3
"""Run Orpheus conformance scenarios and compare outputs to golden fixtures."""
from __future__ import annotations

import argparse
import json
import pathlib
import shutil
import subprocess
import sys
from typing import List

SCRIPT_DIR = pathlib.Path(__file__).parent
sys.path.insert(0, str(SCRIPT_DIR))

from compare_json import compare_json  # noqa: E402
from compare_wav import compare_wav  # noqa: E402


class ConformanceCase:
    def __init__(
        self,
        name: str,
        session: pathlib.Path,
        minhost_args: List[str],
        expected_json: pathlib.Path,
        expected_wav: pathlib.Path,
    ) -> None:
        self.name = name
        self.session = session
        self.minhost_args = minhost_args
        self.expected_json = expected_json
        self.expected_wav = expected_wav


def find_minhost(build_dir: pathlib.Path) -> pathlib.Path:
    suffixes = ["", ".exe"]
    candidates = []
    for suffix in suffixes:
        candidates.extend(
            [
                build_dir / "adapters" / "minhost" / f"orpheus_minhost{suffix}",
                build_dir / f"orpheus_minhost{suffix}",
            ]
        )
    for candidate in candidates:
        if candidate.exists():
            return candidate
    raise FileNotFoundError("Unable to locate orpheus_minhost. Specify --minhost.")


def load_cases(fixtures_dir: pathlib.Path) -> List[ConformanceCase]:
    session_dir = fixtures_dir.parents[1] / "fixtures"
    solo_session = session_dir / "solo_click.json"
    render_fixture_dir = fixtures_dir / "render_click"
    return [
        ConformanceCase(
            name="render_click_solo",
            session=solo_session,
            minhost_args=[
                "--json",
                "--sr",
                "44100",
                "--bd",
                "16",
                "render-click",
            ],
            expected_json=render_fixture_dir / "solo_click.render.json",
            expected_wav=render_fixture_dir / "solo_click.render.wav.b64",
        ),
    ]


def run_case(
    case: ConformanceCase,
    minhost: pathlib.Path,
    output_dir: pathlib.Path,
    diff_dir: pathlib.Path,
) -> bool:
    case_out_dir = output_dir / case.name
    case_out_dir.mkdir(parents=True, exist_ok=True)

    json_path = case_out_dir / "actual.json"
    wav_path = case_out_dir / "actual.wav"

    args = [
        str(minhost),
        "--session",
        str(case.session),
        *case.minhost_args,
        "--out",
        "actual.wav",
    ]

    result = subprocess.run(
        args,
        capture_output=True,
        text=True,
        check=False,
        cwd=case_out_dir,
    )

    json_path.write_text(result.stdout, encoding="utf-8")

    if result.returncode != 0:
        failure_log = case_out_dir / "stderr.log"
        failure_log.write_text(result.stderr, encoding="utf-8")
        return False

    json_diff = diff_dir / f"{case.name}.json.diff"
    wav_diff = diff_dir / f"{case.name}.wav.diff"

    json_ok = compare_json(json_path, case.expected_json, json_diff)
    wav_ok = compare_wav(wav_path, case.expected_wav, wav_diff)

    if json_ok and json_diff.exists():
        json_diff.unlink()
    if wav_ok and wav_diff.exists():
        wav_diff.unlink()

    return json_ok and wav_ok


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Run Orpheus conformance checks.")
    parser.add_argument("--build-dir", type=pathlib.Path, required=True)
    parser.add_argument("--fixtures", type=pathlib.Path, required=True)
    parser.add_argument("--output", type=pathlib.Path, required=True)
    parser.add_argument("--diff", type=pathlib.Path, required=True)
    parser.add_argument("--minhost", type=pathlib.Path)
    args = parser.parse_args(argv)

    build_dir = args.build_dir.resolve()
    fixtures_dir = args.fixtures.resolve()
    output_dir = args.output.resolve()
    diff_dir = args.diff.resolve()

    if diff_dir.exists():
        shutil.rmtree(diff_dir)
    diff_dir.mkdir(parents=True, exist_ok=True)
    output_dir.mkdir(parents=True, exist_ok=True)

    minhost = args.minhost.resolve() if args.minhost else find_minhost(build_dir)

    cases = load_cases(fixtures_dir)
    failures = []
    for case in cases:
        success = run_case(case, minhost, output_dir, diff_dir)
        if not success:
            failures.append(case.name)

    if failures:
        summary_path = diff_dir / "summary.json"
        summary = {"failed": failures, "total": len(cases)}
        summary_path.write_text(json.dumps(summary, indent=2), encoding="utf-8")
        return 1

    # No failures; clean up diff directory to avoid uploading empty artifacts.
    shutil.rmtree(diff_dir)
    return 0


if __name__ == "__main__":
    sys.exit(main())
