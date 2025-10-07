#!/usr/bin/env python3
"""Ensure NOMINMAX is defined before including windows.h in abi_link tests."""

from __future__ import annotations

import pathlib
import sys


def check_file(path: pathlib.Path) -> bool:
    content = path.read_text(encoding="utf-8").splitlines()
    seen_nom = False
    for index, line in enumerate(content, start=1):
        stripped = line.strip()
        if stripped.startswith("#define NOMINMAX"):
            seen_nom = True
        if stripped.startswith("#include") and "windows.h" in stripped.lower():
            if not seen_nom:
                print(f"{path}:{index}: windows.h included without prior NOMINMAX definition")
                return False
            return True
    return True


def main() -> int:
    root = pathlib.Path("tests/abi_link")
    if not root.exists():
        return 0

    success = True
    for path in sorted(root.rglob("*.cpp")):
        success &= check_file(path)
    for path in sorted(root.rglob("*.h")):
        success &= check_file(path)
    return 0 if success else 1


if __name__ == "__main__":
    sys.exit(main())
