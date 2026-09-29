#!/usr/bin/env python3
"""Recreate the checked-in Xigua bitmap font using pinned lv_font_conv 1.5.3."""
import argparse
import json
import subprocess
import sys
from pathlib import Path

from fontTools import __version__ as FONTTOOLS_VERSION
from fontTools import subset
from fontTools.ttLib import TTFont
from fontTools.varLib.instancer import instantiateVariableFont

ROOT = Path(__file__).resolve().parents[2]
ASSETS = ROOT / "assets/fonts"


def make_source_subset(source, symbols):
    if FONTTOOLS_VERSION != "4.66.0":
        raise SystemExit("Expected pinned fontTools 4.66.0")
    missing = sorted({ord(c) for c in symbols} - set(TTFont(str(source)).getBestCmap()))
    if missing:
        values = " ".join(f"U+{c:04X}" for c in missing)
        raise SystemExit(f"Source font is missing required glyphs: {values}")
    font = TTFont(str(source), recalcBBoxes=False, recalcTimestamp=False)
    instantiateVariableFont(font, {"wght": 400}, inplace=True, optimize=True)
    options = subset.Options()
    options.name_IDs = ["*"]
    options.name_legacy = True
    options.name_languages = ["*"]
    options.layout_features = ["*"]
    options.notdef_outline = True
    options.recommended_glyphs = True
    subsetter = subset.Subsetter(options=options)
    subsetter.populate(text=symbols)
    subsetter.subset(font)
    font.save(ASSETS / "xigua_ui_regular.ttf")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--converter", required=True, type=Path,
                        help="Path to lv_font_conv 1.5.3/lv_font_conv.js")
    parser.add_argument("--source-font", required=True, type=Path,
                        help="Path to the licensed NotoSansSC-VF.ttf source font")
    parser.add_argument("--node", default="node")
    args = parser.parse_args()
    package = json.loads((args.converter.parent / "package.json").read_text(encoding="utf-8"))
    if package.get("version") != "1.5.3":
        raise SystemExit("Expected pinned lv_font_conv 1.5.3")
    subprocess.run([sys.executable, str(ROOT / "tools/check_xigua_fonts.py"), "--inventory"], check=True)
    symbols = (ASSETS / "xigua_characters.txt").read_text(encoding="utf-8")
    make_source_subset(args.source_font, symbols)
    subprocess.run([
        args.node, str(args.converter), "--font", str(ASSETS / "xigua_ui_regular.ttf"),
        "--symbols", symbols, "--size", "16", "--bpp", "4", "--format", "lvgl",
        "--no-compress", "--lv-font-name", "xigua_font_16", "--lv-include", "lvgl.h",
        "--output", str(ASSETS / "xigua_font_16.c"),
    ], check=True)
    subprocess.run([sys.executable, str(ROOT / "tools/check_xigua_fonts.py")], check=True)


if __name__ == "__main__":
    main()
