#!/usr/bin/env python3
"""Check fixed Xigua UI strings against generated LVGL cmap coverage.

Use --inventory to update the converter character inventory. This verifies
descriptors only; actual widget rendering still requires a device check.
"""
import argparse
import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
UI_SOURCES = (
    ROOT / "main/xigua/xigua_ui.c",
    ROOT / "main/xigua/xigua_keyboard.c",
)
FONT = ROOT / "assets/fonts/xigua_font_16.c"
INVENTORY = ROOT / "assets/fonts/xigua_characters.txt"


def characters():
    text = ""
    for source in UI_SOURCES:
        strings = re.findall(r'"(?:[^"\\]|\\.)*"', source.read_text(encoding="utf-8"))
        # C literals used here have JSON-compatible escapes; no arbitrary user text.
        text += "".join(json.loads(s) for s in strings)
    return {ord(c) for c in text if ord(c) >= 32} | set(range(32, 127))


def coverage(path):
    text = path.read_text(encoding="utf-8")
    descriptors = re.search(r'glyph_dsc\[\] = \{(.*?)\n\};', text, re.S)
    if not descriptors:
        raise ValueError(f"No LVGL glyph descriptors in {path}")
    glyphs = [tuple(map(int, match)) for match in re.findall(
        r'\.bitmap_index = (\d+), \.adv_w = (\d+), \.box_w = (\d+), \.box_h = (\d+)',
        descriptors.group(1))]
    arrays = {}
    for name, body in re.findall(r'const uint16_t (unicode_list_\d+)\[\] = \{(.*?)\};', text, re.S):
        body = re.sub(r'/\*.*?\*/', '', body, flags=re.S)
        arrays[name] = [int(n, 0) for n in re.findall(r'0x[\da-fA-F]+|\b\d+\b', body)]
    cmap = re.search(r'cmaps\[\] =\s*\{(.*?)\n\};', text, re.S)
    if not cmap:
        raise ValueError(f"No LVGL cmaps in {path}")
    codes = set()
    for block in re.findall(r'\{([^{}]+)\}', cmap.group(1), re.S):
        start = int(re.search(r'\.range_start = (\d+)', block).group(1))
        length = int(re.search(r'\.range_length = (\d+)', block).group(1))
        glyph_start = int(re.search(r'\.glyph_id_start = (\d+)', block).group(1))
        name = re.search(r'\.unicode_list = (\w+)', block).group(1)
        if name != "NULL":
            offsets = arrays[name]
            listed = int(re.search(r'\.list_length = (\d+)', block).group(1))
            assert listed == len(offsets), "Sparse cmap length mismatch"
            assert "SPARSE_TINY" in block and ".glyph_id_ofs_list = NULL" in block
            mapped = [(start + offset, glyph_start + index)
                      for index, offset in enumerate(offsets)]
        else:
            assert "FORMAT0_TINY" in block, "Unsupported cmap needs explicit decoding"
            mapped = [(code, glyph_start + code - start)
                      for code in range(start, start + length)]
        for code, glyph_id in mapped:
            assert code not in codes, f"Duplicate cmap entry U+{code:04X}"
            assert 0 < glyph_id < len(glyphs), f"Missing descriptor for U+{code:04X}"
            _, advance, width, height = glyphs[glyph_id]
            assert advance > 0, f"Empty glyph advance for U+{code:04X}"
            assert code == 32 or width > 0 and height > 0, f"Empty bitmap for U+{code:04X}"
            codes.add(code)
    return codes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--inventory", action="store_true")
    parser.add_argument("--font", type=Path, default=FONT)
    args = parser.parse_args()
    needed = characters()
    if args.inventory:
        INVENTORY.parent.mkdir(parents=True, exist_ok=True)
        INVENTORY.write_text("".join(chr(c) for c in sorted(needed)), encoding="utf-8")
        print(f"Inventory: {len(needed)} code points")
        return
    supported = coverage(args.font)
    # Explicit negative case ensures accidental broad/unconditional success fails.
    assert 0x9F98 not in supported, "Expected U+9F98 to be absent from fixed UI subset"
    missing = needed - supported
    if missing:
        print("Missing glyphs:", " ".join(f"U+{c:04X}({chr(c)})" for c in sorted(missing)))
        raise SystemExit(1)
    print(f"PASS: {len(needed)} fixed UI/ASCII code points; negative U+9F98 absent")


if __name__ == "__main__":
    main()
