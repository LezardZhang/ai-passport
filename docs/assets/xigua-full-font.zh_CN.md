<p align="right"><strong>简体中文</strong> · <a href="xigua-full-font.md">English</a></p>

# 西瓜助手完整回复字库

## 覆盖范围与来源

模型回复使用思源黑体 Source Han Sans SC Regular 2.005 提供的全部 44,853 个字符，包含
U+3400–U+4DBF、U+4E00–U+9FFF 的所有字符（27,584 个汉字）、ASCII、标点和源字体中的其他字形。
这是源字体的完整覆盖，不保证覆盖全部 Unicode 或 emoji。源字体没有的字符仍使用 LVGL 的缺字行为。

原始 OTF、SIL OFL 1.1 许可证、生成的 C 源码、字符清单和哈希清单均纳入 `assets/fonts/`。
来源为 [Adobe 2.005 发布版本](https://github.com/adobe-fonts/source-han-sans/tree/2.005R)。
原始字体保留原有保留名称；LVGL 符号为 `xigua_font_full20`。

## 生成方式

新克隆直接编译 Git 中生成好的 C 源码。重新生成需要 Node.js 和 `lv_font_conv` 1.5.3，
可安装在自行选择的临时目录：

```text
npm install --prefix C:/Temp/aihw-font-tools --ignore-scripts --no-audit --no-fund lv_font_conv@1.5.3
python tools/generate_xigua_full_font.py --converter C:/Temp/aihw-font-tools/node_modules/lv_font_conv/lv_font_conv.js
python tests/test_xigua_full_font.py
```

转换使用 20 px、2 bpp、压缩、无字距调整，以及源字体在 U+0020–U+10FFFF 范围内的全部字形。
脚本统一生成文件头并记录哈希；Git 保留生成 C 文件的 LF 换行以保证跨机器哈希稳定。
测试独立读取原始 OTF 的 Unicode cmap 和生成的 LVGL 字符映射，核对全部 44,853 个字形编号、
两个完整汉字区及超过 1 MiB 的位图索引。测试纳入静态验证门禁。

## 接入与资源预算

`main/CMakeLists.txt` 编译字库；`sdkconfig.defaults` 启用
`CONFIG_LV_FONT_FMT_TXT_LARGE`、`CONFIG_LV_USE_FONT_COMPRESSED`，关闭重复的 LVGL 内置中文子集。
位图、描述符和映射均为映射 Flash 中的 `const`，不会把整套字库复制到堆中。
字形解压及屏幕缓冲仍需要工作 RAM。

固定菜单保留 16/20 px 字体，并以完整字库作为回退。AI 和故事回复直接将完整字库设为主字体，
保留其 38 px 行高和 11 px 基线。回复正文移除原先的操作说明前缀，让更多文字可见。
紧凑菜单中的动态回退字形仍需上屏验收，因为回退字形与主字体的度量可能不同。

生成字库目标文件只读数据为 4,050,047 字节，`.data`/`.bss` 为零。
整合应用为 5,773,120 字节，现有 factory 分区为 6,225,920 字节，余量 452,800 字节。
NVS 和 2 MiB 录音临时分区保持原有布局。

AI 界面现使用裁切区域翻页查看回复，回复缓冲为 4096 字节，截断保留完整 UTF-8 字符；
本地截断或服务器达到输出上限时均标明部分回复。需要实机检查任意模型文字、2 bpp 细笔画、裁切，
以及录音与 TLS 同时使用时的堆稳定性。详见 [AI 界面设计](xigua-ui-design.zh_CN.md)。
