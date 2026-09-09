#!/usr/bin/env python3
"""Generate a diagnostic-only renamed production TruePeakMeter with forced-inline process."""
from pathlib import Path
import re
import sys

root = Path(sys.argv[1]) if len(sys.argv) > 1 else Path(__file__).resolve().parents[1]
src = root / "src/core/routing/true_peak_meter.h"
out = Path(sys.argv[2]) if len(sys.argv) > 2 else root / "diagnostic-output/forced_true_peak_meter.h"
text = src.read_text(encoding="utf-8")
text = text.replace("TruePeakMeter", "ForcedTruePeakMeter")
text = text.replace("\nprivate:", "\npublic:", 1)
text = re.sub(r"\n(\s*)float process\(float sample\)", r"\n\1__forceinline float process(float sample)", text, count=1)
text = text.replace("#pragma once", "#pragma once\n// GENERATED TEMPORARY DIAGNOSTIC COPY; never install or commit as production API.")
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(text, encoding="utf-8", newline="\n")
print(f"generated {out} from {src}")