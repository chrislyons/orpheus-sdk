#!/usr/bin/env python3
"""Fail if the diff introduces binary files outside the allowlist."""

from __future__ import annotations

import subprocess
import sys

ALLOWED_PREFIXES = ("tools/conformance/fixtures/",)


def git(*args: str) -> str:
    result = subprocess.run(["git", *args], check=True, capture_output=True, text=True)
    return result.stdout.strip()


def detect_binary_paths(base_ref: str) -> list[str]:
    diff_output = git("diff", "--numstat", f"{base_ref}..HEAD")
    binaries: list[str] = []
    if not diff_output:
        return binaries
    for line in diff_output.splitlines():
        parts = line.split("\t")
        if len(parts) < 3:
            continue
        added, removed, path = parts[0], parts[1], parts[2]
        if added == "-" and removed == "-":
            binaries.append(path)
    return binaries


def main() -> int:
    try:
        base = git("merge-base", "HEAD", "origin/main")
    except subprocess.CalledProcessError:
        print("::warning::Unable to determine origin/main; skipping binary diff check")
        return 0

    binaries = detect_binary_paths(base)
    disallowed = [path for path in binaries if not path.startswith(ALLOWED_PREFIXES)]
    if disallowed:
        print("Binary files detected outside the allowlist:")
        for path in disallowed:
            print(f"  {path}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
