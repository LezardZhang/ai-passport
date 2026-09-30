<p align="right"><a href="xigua-full-font.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Xigua complete reply font

## Coverage and provenance

The model reply uses all 44,853 codepoints supplied by Source Han Sans SC
Regular 2.005, including every character in U+3400–U+4DBF and U+4E00–U+9FFF
(27,584 Han characters), ASCII, punctuation, and the other source glyphs.
This is the complete source font, not a promise of every Unicode character
or emoji. Missing source glyphs still use LVGL's missing-character behavior.

The original OTF, SIL OFL 1.1 license, generated C source, codepoint inventory,
and hash manifest are tracked in `assets/fonts/`. The source is from
[Adobe's 2.005 release](https://github.com/adobe-fonts/source-han-sans/tree/2.005R).
The reserved family name remains unchanged for the original font; the LVGL
symbol is `xigua_font_full20`.

## Generation

Normal clones build the tracked generated C directly. Regeneration needs Node.js
and `lv_font_conv` 1.5.3, installed in a chosen temporary directory:

```text
npm install --prefix C:/Temp/aihw-font-tools --ignore-scripts --no-audit --no-fund lv_font_conv@1.5.3
python tools/generate_xigua_full_font.py --converter C:/Temp/aihw-font-tools/node_modules/lv_font_conv/lv_font_conv.js
python tests/test_xigua_full_font.py
```

Conversion uses 20 px, 2 bpp, compression, no kerning, and the source's complete
U+0020–U+10FFFF range. The script normalizes the generated header and records
hashes. Git preserves LF in the generated C to keep the hash stable across hosts.
The test independently reads the OTF Unicode cmap and the generated LVGL maps,
checks all 44,853 mapped glyph IDs, both complete Han ranges, and bitmap indices
larger than 1 MiB. It runs in the static validation gate.

## Integration and resource budget

`main/CMakeLists.txt` compiles the font. `sdkconfig.defaults` enables
`CONFIG_LV_FONT_FMT_TXT_LARGE` and `CONFIG_LV_USE_FONT_COMPRESSED`, and disables
LVGL's duplicate built-in Chinese subset. The font's bitmap, descriptors, and
maps are `const` in mapped Flash; the complete font is not copied into heap.
Glyph decompression and display buffers still consume working RAM.

Fixed menu fonts remain at 16/20 px and fall back to the complete font. AI and
story replies select the complete font as their primary font, preserving its
38 px line height and 11 px baseline. Reply bodies omit the former instruction
prefix so more text fits. Dynamic fallback glyphs in compact menus still need
visual acceptance, because fallback metrics can differ from the primary font.

The generated font object occupies 4,050,047 bytes of read-only data and no
`.data`/`.bss`. The integrated application is 5,775,616 bytes in the existing
6,225,920-byte factory partition, leaving 450,304 bytes. NVS and the 2 MiB
temporary voice partition retain their layout.

The AI UI now pages replies in a clipped viewport, with a 4096-byte reply buffer
and UTF-8-safe truncation. Server output limits also mark replies as partial. Check arbitrary
model text, thin strokes at 2 bpp, clipping, and heap stability during voice/TLS
use on the physical screen. See the [AI UI design](xigua-ui-design.md).
