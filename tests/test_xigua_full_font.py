#!/usr/bin/env python3
"""Check generated LVGL maps against the original font, without font tooling."""

import hashlib
import json
from pathlib import Path
import re
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]
ASSETS = ROOT / "assets" / "fonts"


def source_codepoints(data):
    tables = struct.unpack_from(">H", data, 4)[0]
    cmap = None
    for index in range(tables):
        tag, _, offset, _ = struct.unpack_from(">4sIII", data, 12 + index * 16)
        if tag == b"cmap":
            cmap = offset
            break
    assert cmap is not None, "source has no cmap"
    count = struct.unpack_from(">H", data, cmap + 2)[0]
    for index in range(count):
        _, _, offset = struct.unpack_from(">HHI", data, cmap + 4 + index * 8)
        start = cmap + offset
        if struct.unpack_from(">H", data, start)[0] != 12:
            continue
        groups = struct.unpack_from(">I", data, start + 12)[0]
        result = set()
        for group in range(groups):
            first, last, glyph = struct.unpack_from(">III", data, start + 16 + group * 12)
            result.update(cp for cp in range(max(first, 32), last + 1)
                          if glyph + cp - first != 0)
        return result
    raise AssertionError("source has no format 12 Unicode cmap")


class FullFontTest(unittest.TestCase):
    def test_real_source_and_generated_maps(self):
        manifest = json.loads((ASSETS / "xigua_font_full20.manifest.json").read_text())
        otf = (ASSETS / "SourceHanSansSC-Regular.otf").read_bytes()
        generated = (ASSETS / "xigua_font_full20.c").read_bytes()
        self.assertEqual(hashlib.sha256(otf).hexdigest(), manifest["source_sha256"])
        self.assertEqual(hashlib.sha256(generated).hexdigest(), manifest["generated_sha256"])
        expected = source_codepoints(otf)
        source = generated.decode("utf-8")
        lists = {}
        for name, values in re.findall(
                r"static const uint(?:8|16)_t ((?:unicode|glyph_id_ofs)_list_\d+)\[\] = \{(.*?)\};",
                source, re.S):
            lists[name] = [int(value, 0) for value in re.findall(r"0x[0-9a-fA-F]+|\d+", values)]
        maps = source.split("static const lv_font_fmt_txt_cmap_t cmaps[] =", 1)[1].split("};", 1)[0]
        actual = {}
        for first, length, glyph, name, glyph_offsets, count, kind in re.findall(
                r"\.range_start = (\d+), \.range_length = (\d+), \.glyph_id_start = (\d+),\s*"
                r"\.unicode_list = (\w+), \.glyph_id_ofs_list = (\w+), \.list_length = (\d+), "
                r"\.type = (\w+)", maps):
            first, length, glyph, count = map(int, (first, length, glyph, count))
            if kind in ("LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY", "LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL"):
                offsets = range(length)
            else:
                self.assertEqual(kind, "LV_FONT_FMT_TXT_CMAP_SPARSE_TINY")
                offsets = lists[name]
                self.assertEqual(len(offsets), count)
                self.assertEqual(offsets, sorted(set(offsets)))
            for index, offset in enumerate(offsets):
                delta = index
                if kind == "LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL":
                    delta = lists[glyph_offsets][index]
                    if delta == 0 and index != 0:
                        continue
                self.assertLess(offset, length)
                self.assertNotIn(first + offset, actual)
                actual[first + offset] = glyph + delta
        self.assertEqual(set(actual), expected)
        self.assertEqual(len(actual), 44853)
        self.assertEqual(set(actual.values()), set(range(1, 44854)))
        self.assertTrue(set(range(0x3400, 0x4DC0)) <= set(actual))
        self.assertTrue(set(range(0x4E00, 0xA000)) <= set(actual))
        self.assertNotIn(0x1F600, actual)  # No promise of emoji coverage.
        inventory = {int(line, 16) for line in
                     (ASSETS / "xigua_font_full20.codepoints.txt").read_text().splitlines()}
        self.assertEqual(inventory, expected)
        descriptors = re.findall(r"\.bitmap_index = (\d+)", source)
        self.assertEqual(len(descriptors), len(actual) + 1)
        self.assertGreater(max(map(int, descriptors)), 0xFFFFF)
        self.assertIn(".bitmap_format = 1", source)
        print(f"Full font: {len(actual)} source glyphs; all 27,584 basic/Ext A Han covered")


if __name__ == "__main__":
    unittest.main()
