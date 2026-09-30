#!/usr/bin/env python3
"""Regenerate the tracked complete font using a pinned local lv_font_conv."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets" / "fonts"
HEADER = """/* Source Han Sans SC Regular 2.005; SIL OFL 1.1 (see SourceHanSansSC-LICENSE.txt).
 * lv_font_conv 1.5.3: --size 20 --bpp 2 --format lvgl --no-kerning
 * --range 0x20-0x10FFFF; compression enabled; 44,853 source codepoints.
 * Generated from SourceHanSansSC-Regular.otf. */"""


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--converter", required=True, type=Path,
                        help="path to lv_font_conv@1.5.3/lv_font_conv.js")
    parser.add_argument("--node", default="node")
    args = parser.parse_args()
    package = json.loads((args.converter.parent / "package.json").read_text())
    if package["version"] != "1.5.3":
        parser.error("lv_font_conv 1.5.3 is required")
    output = ASSETS / "xigua_font_full20.c"
    subprocess.run([args.node, str(args.converter), "--font",
                    str(ASSETS / "SourceHanSansSC-Regular.otf"),
                    "--range", "0x20-0x10FFFF", "--size", "20", "--format", "lvgl",
                    "--no-kerning", "--bpp", "2", "--lv-include", "lvgl.h",
                    "--lv-font-name", "xigua_font_full20", "--output", str(output)], check=True)
    source = output.read_text(encoding="utf-8")
    source = re.sub(r"^/\*.*?\*/", HEADER, source, count=1, flags=re.S)
    output.write_bytes(source.encode("utf-8"))
    points = sorted({int(cp, 16) for cp in re.findall(r"/\* U\+([0-9A-F]+)", source)})
    (ASSETS / "xigua_font_full20.codepoints.txt").write_bytes(
        "".join(f"{cp:06X}\n" for cp in points).encode("ascii"))
    path = ASSETS / "xigua_font_full20.manifest.json"
    manifest = json.loads(path.read_text())
    manifest["codepoints"] = len(points)
    manifest["generated_sha256"] = hashlib.sha256(output.read_bytes()).hexdigest()
    manifest["source_sha256"] = hashlib.sha256(
        (ASSETS / "SourceHanSansSC-Regular.otf").read_bytes()).hexdigest()
    path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"Generated {len(points)} codepoints; run tests/test_xigua_full_font.py")


if __name__ == "__main__":
    main()
