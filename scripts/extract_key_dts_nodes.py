#!/usr/bin/env python3
"""
Extract primary display panel (B5_S6E3FAC_AMF670BS03), S2DOS05 display PMIC,
Synaptics S3916A touchscreen, and MAX77705 PMIC hardware parameters from fdt.dts.
"""
import re
from pathlib import Path

PROJECT_DIR = Path(__file__).resolve().parent.parent
DTS_PATH = PROJECT_DIR / "extracted_hw_info" / "fdt.dts"
OUT_PATH = PROJECT_DIR / "extracted_hw_info" / "key_nodes_extracted.dts"

dts_text = DTS_PATH.read_text(encoding="utf-8", errors="replace")

# Extract specific blocks of interest for mainline DTS authoring
targets = [
    "reserved-memory {",
    "s2dos05_pmic@60 {",
    "i2c@a90000 {",
    "max77705@66 {",
    "tfa98xx@34 {",
    "tfa98xx@35 {",
    "gpio_keys {",
    "S6E3FAC_AMF670BS03",
]

out_lines = ["// Extracted key hardware nodes from SM-F7310 runtime FDT\n\n"]

for t in targets:
    idx = dts_text.find(t)
    if idx == -1:
        out_lines.append(f"// [NOT FOUND] {t}\n\n")
        continue
    # Find start of line
    start = dts_text.rfind("\n", 0, idx) + 1
    # Walk braces to find matching closing brace
    brace_depth = 0
    started = False
    pos = start
    limit = min(len(dts_text), start + 25000)
    while pos < limit:
        ch = dts_text[pos]
        if ch == "{":
            brace_depth += 1
            started = True
        elif ch == "}":
            brace_depth -= 1
            if started and brace_depth == 0:
                end = dts_text.find("\n", pos)
                if end == -1:
                    end = len(dts_text)
                out_lines.append(f"// === SECTION: {t} ===\n")
                out_lines.append(dts_text[start:end+1] + "\n\n")
                break
        pos += 1

OUT_PATH.write_text("".join(out_lines), encoding="utf-8")
print(f"Wrote {OUT_PATH} ({OUT_PATH.stat().st_size} bytes)")
