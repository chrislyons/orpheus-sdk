#!/usr/bin/env python3
"""Compare two JSON files and emit a unified diff when they differ."""

from __future__ import annotations

import argparse
import difflib
import json
import pathlib
import sys
from typing import Iterable


def load_text(path: pathlib.Path) -> str:
    try:
        return path.read_text(encoding="utf-8")
    except OSError as exc:
        raise SystemExit(f"failed to read {path}: {exc}") from exc


def write_text(path: pathlib.Path, content: str) -> None:
    try:
        path.write_text(content, encoding="utf-8")
    except OSError as exc:
        raise SystemExit(f"failed to write {path}: {exc}") from exc


def canonical_lines(text: str) -> Iterable[str]:
    try:
        parsed = json.loads(text)
    except json.JSONDecodeError:
        return text.splitlines(keepends=True)
    formatted = json.dumps(parsed, indent=2, sort_keys=True)
    return (line + "\n" for line in formatted.splitlines())


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected", required=True, help="Path to the golden JSON file")
    parser.add_argument("--actual", required=True, help="Path to the produced JSON file")
    parser.add_argument("--output", required=True, help="Directory where diff artifacts are written")
    args = parser.parse_args(argv)

    expected_path = pathlib.Path(args.expected)
    actual_path = pathlib.Path(args.actual)
    output_dir = pathlib.Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)

    expected_text = load_text(expected_path)
    actual_text = load_text(actual_path)

    if expected_text == actual_text:
        diff_path = output_dir / "diff.patch"
        if diff_path.exists():
            diff_path.unlink()
        return 0

    expected_lines = list(canonical_lines(expected_text))
    actual_lines = list(canonical_lines(actual_text))

    diff = difflib.unified_diff(
        expected_lines,
        actual_lines,
        fromfile=str(expected_path),
        tofile=str(actual_path),
    )
    diff_text = "".join(diff)
    if not diff_text:
        diff_text = "(no textual diff available)\n"

    write_text(output_dir / "diff.patch", diff_text)
    return 1


if __name__ == "__main__":
    sys.exit(main())
