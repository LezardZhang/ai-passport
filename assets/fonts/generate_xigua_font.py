#!/usr/bin/env python3
"""Recreate the checked-in Xigua bitmap font using pinned lv_font_conv 1.5.3."""
import argparse
import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--converter", required=True, type=Path,
                        help="Path to lv_font_conv 1.5.3/lv_font_conv.js")
    parser.add_argument("--node", default="node")
    args = parser.parse_args()
    package = json.loads((args.converter.parent / "package.json").read_text(encoding="utf-8"))
    if package.get("version") != "1.5.3":
        raise SystemExit("Expected pinned lv_font_conv 1.5.3")
    subprocess.run([sys.executable, str(ROOT / "tools/check_xigua_fonts.py"), "--inventory"], check=True)
    assets = ROOT / "assets/fonts"
    symbols = (assets / "xigua_characters.txt").read_text(encoding="utf-8")
    subprocess.run([
        args.node, str(args.converter), "--font", str(assets / "xigua_ui_regular.ttf"),
        "--symbols", symbols, "--size", "16", "--bpp", "4", "--format", "lvgl",
        "--no-compress", "--lv-font-name", "xigua_font_16", "--lv-include", "lvgl.h",
        "--output", str(assets / "xigua_font_16.c"),
    ], check=True)
    subprocess.run([sys.executable, str(ROOT / "tools/check_xigua_fonts.py")], check=True)


if __name__ == "__main__":
    main()
