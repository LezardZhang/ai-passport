<p align="right"><a href="xigua-font.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Xigua UI Font

`xigua_ui_regular.ttf` is a licensed, static Regular (weight 400) subset of Noto
Sans SC, version 2.004 (font metadata `Version 2.04;241114210130;non-release`).
The source was the existing `NotoSansSC-VF.ttf` Windows font. The font's embedded
copyright is © 2014–2021 Adobe, with Reserved Font Name “Source”. Its embedded
license is SIL OFL 1.1; the original license is preserved in
[`OFL-NotoSansSC.txt`](OFL-NotoSansSC.txt). The upstream family is distributed by
[Google Fonts](https://github.com/google/fonts/tree/main/ofl/notosanssc).

The reusable source subset was produced with fontTools 4.66.0 by instantiating
`wght=400`, then subsetting the character inventory with all original name and
license records retained. It contains only the fixed UI character set plus
printable ASCII; it does not support arbitrary Chinese input. When adding new
characters, regenerate the source subset from the original licensed font first.

`xigua_font_16.c` is generated with lv_font_conv **1.5.3**, 16 px, 4 bpp,
uncompressed LVGL format; its line height is 20 px. It is compiled as a separate
source in the main component and selected by every small Xigua label. Large
numbers use the existing Montserrat 20 and contain only ASCII numbers/units.
The writable application descriptor falls back to Montserrat 14 for symbols.

Recreate the bitmap font from the checked-in subset:

```text
python assets/fonts/generate_xigua_font.py \
  --source-font /path/to/NotoSansSC-VF.ttf \
  --converter /path/to/lv_font_conv/lv_font_conv.js
python tools/check_xigua_fonts.py
```

The generator updates [`xigua_characters.txt`](xigua_characters.txt) from all C
string literals in `main/xigua/xigua_ui.c` and `main/xigua/xigua_keyboard.c`,
includes printable ASCII, rebuilds the reusable TTF subset from the original
licensed font, and runs the cmap and bitmap descriptor checks. The current M3
inventory contains 304 unique code points. All 304 are covered, and U+9F98 is
explicitly absent as a negative check. The earlier M0-M1 built-in Source Han
subset lacked 61 of that stage's 252 characters. The bitmap font C file is about
225 KB of source, and the reusable TTF source is about 127 KB; inspect firmware
size output for actual compiled Flash use. Rendering, alignment and runtime heap
on the 240 × 320 board remain a separate device acceptance check.
