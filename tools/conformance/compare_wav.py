#!/usr/bin/env python3
"""Compare two WAV files and write a textual diff summary."""

from __future__ import annotations

import argparse
import base64
import binascii
import io
import pathlib
import sys
import wave


def format_params(params: wave._wave_params) -> str:  # type: ignore[attr-defined]
    return (
        f"channels={params.nchannels}, sample_width={params.sampwidth}, "
        f"frame_rate={params.framerate}, frames={params.nframes}, "
        f"compression={params.comptype}/{params.compname}"
    )


def _decode_wav_bytes(path: pathlib.Path, payload: bytes) -> bytes:
    suffixes = {suffix.lower() for suffix in path.suffixes}
    if ".b64" in suffixes or ".base64" in suffixes:
        try:
            return base64.b64decode(payload, validate=True)
        except binascii.Error as exc:  # pragma: no cover - validated in tests
            raise SystemExit(f"failed to decode base64 data from {path}: {exc}") from exc
    return payload


def load_wav(path: pathlib.Path) -> tuple[wave._wave_params, bytes]:  # type: ignore[attr-defined]
    try:
        raw = path.read_bytes()
    except OSError as exc:  # pragma: no cover - handled via SystemExit
        raise SystemExit(f"failed to read {path}: {exc}") from exc

    decoded = _decode_wav_bytes(path, raw)

    try:
        with wave.open(io.BytesIO(decoded), "rb") as wf:
            params = wf.getparams()
            data = wf.readframes(wf.getnframes())
    except (OSError, wave.Error) as exc:  # pragma: no cover - handled via SystemExit
        raise SystemExit(f"failed to read {path}: {exc}") from exc
    return params, data


def describe_difference(expected: tuple[wave._wave_params, bytes], actual: tuple[wave._wave_params, bytes]) -> str:  # type: ignore[attr-defined]
    expected_params, expected_data = expected
    actual_params, actual_data = actual
    lines: list[str] = []

    if expected_params != actual_params:
        lines.append("parameter mismatch:")
        lines.append(f"  expected: {format_params(expected_params)}")
        lines.append(f"  actual:   {format_params(actual_params)}")

    if expected_data != actual_data:
        lines.append("audio payload differs")
        length = min(len(expected_data), len(actual_data))
        for index in range(length):
            if expected_data[index] != actual_data[index]:
                lines.append(f"  first mismatch at byte {index}")
                break
        if len(expected_data) != len(actual_data):
            lines.append(
                f"  frame byte lengths differ: expected={len(expected_data)}, actual={len(actual_data)}"
            )

    return "\n".join(lines)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--expected", required=True, help="Path to the golden WAV file")
    parser.add_argument("--actual", required=True, help="Path to the produced WAV file")
    parser.add_argument("--output", required=True, help="Directory where diff artifacts are written")
    args = parser.parse_args(argv)

    output_dir = pathlib.Path(args.output)
    output_dir.mkdir(parents=True, exist_ok=True)

    expected = load_wav(pathlib.Path(args.expected))
    actual = load_wav(pathlib.Path(args.actual))

    diff_text = describe_difference(expected, actual)

    diff_path = output_dir / "diff.txt"
    if not diff_text:
        if diff_path.exists():
            diff_path.unlink()
        return 0

    try:
        diff_path.write_text(diff_text + "\n", encoding="utf-8")
    except OSError as exc:
        raise SystemExit(f"failed to write {diff_path}: {exc}") from exc
    return 1


if __name__ == "__main__":
    sys.exit(main())
