<p align="right"><strong>简体中文</strong> · <a href="xigua-font.md">English</a></p>

# 西瓜助手界面字体

`xigua_ui_regular.ttf` 是 Noto Sans SC 的授权静态 Regular（字重 400）子集，
版本为 2.004（字体元数据 `Version 2.04;241114210130;non-release`）。
源文件是 Windows 已有的 `NotoSansSC-VF.ttf` 字体。字体内嵌版权为
© 2014–2021 Adobe，保留字体名称为“Source”，许可证为 SIL OFL 1.1。
原始许可证保存在 [`OFL-NotoSansSC.txt`](OFL-NotoSansSC.txt)，上游字体由
[Google Fonts](https://github.com/google/fonts/tree/main/ofl/notosanssc) 分发。

可复用源子集使用 fontTools 4.66.0 生成：先将 `wght=400` 实例化，再按字符
清单提取子集，保留全部原始名称及许可证记录。它只覆盖固定界面字符和
可打印 ASCII，不支持任意中文输入。增加新字符时，应先从原始授权字体
重新生成源子集。

`xigua_font_16.c` 使用 lv_font_conv **1.5.3** 生成，字号 16 px、4 bpp，
采用未压缩 LVGL 格式，行高 20 px。它作为 main 组件的独立源文件编译，
所有西瓜助手小字号标签均显式选用该字体。大号数字使用已有的
Montserrat 20，仅显示 ASCII 数字及单位。应用持有可写字体描述符，
并使用 Montserrat 14 作为符号回退字体。

从已提交的字体源子集重新生成位图字体：

```text
python assets/fonts/generate_xigua_font.py \
  --source-font /path/to/NotoSansSC-VF.ttf \
  --converter /path/to/lv_font_conv/lv_font_conv.js
python tools/check_xigua_fonts.py
```

生成器从 `main/xigua/xigua_ui.c` 和 `main/xigua/xigua_keyboard.c` 的所有 C
字符串提取字符，加入可打印 ASCII，从原始授权字体重建可复用 TTF 子集，
更新 [`xigua_characters.txt`](xigua_characters.txt)，然后检查 cmap 和位图描述符。
当前 M3 清单有 304 个独立码点，全部覆盖，并明确验证 U+9F98 不在子集中，
作为反例。此前 M0–M1 的内置思源黑体子集缺少当时 252 个字符中的 61 个。位图字体 C 源文件约
225 KB，可复用 TTF 源子集约 127 KB；实际编译后的 Flash 占用应查看
固件大小输出。240 × 320 实机上的显示、对齐和运行时堆内存仍需要
单独验收。
