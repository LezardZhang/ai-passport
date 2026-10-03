<p align="right"><strong>简体中文</strong> · <a href="phone-v4-validation.md">English</a></p>

# 手机 0.4.0 验证 - 2026-10-03

三星 SM-S9280，Android 16 / API 36，序列号 R5CX12FFTFF。使用同签名 `adb install -r` 安装 code 4 / 0.4.0-local，没有清数据、卸载或写入合成人物/记录。手机实际安装 APK 的 SHA256 `e81687b9b1533a8f1ca01d1773b21217d2ecb7fe1ae7d289dff919168114ea46` 与交付包一致；所有非签名条目也与页面验收时版本一致。安装（本地产物：`../reports/phone-v4-install.log`）、版本（本地产物：`../reports/phone-v4-version.txt`）、哈希验证（本地产物：`../reports/phone-v4.json`）。

真机界面：PASS，已解锁手机上 13 项只读断言，包括统一照护输入、每日时间线、直接喂奶/尿布入口、照护者选择、奶量、家庭名单/宝宝入口，以及人物登记中的姓名和关系。每个打开的表单都取消，最后回到照护页。界面结果（本地产物：`../reports/phone-v4-ui.json`）。私人实机截图/XML仅留本地，不包含在交付归档中。

本轮真机麦克风/提供方/人声识别：NOT RUN。模拟器验证包含真实 PCM、65 秒录音、发送/重试/取消、36 项真实后端归属断言，以及旋转/夜间/200% 字体检查；不代表真机识别准确率或生产同步已验收。生产扩展部署、共享名单/账户、最低设备、无障碍、后台音频和容量仍待验证。Build：PASS；Host tests：PASS；Device tests：PASS，范围限定如上。见[交付](delivery.zh_CN.md)。
