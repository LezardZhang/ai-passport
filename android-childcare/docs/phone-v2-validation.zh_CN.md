<p align="right"><strong>简体中文</strong> · <a href="phone-v2-validation.md">English</a></p>

# 手机 0.2.0 版验证 — 2026-10-03

设备为三星 SM-S9280，Android16/API36，序列号 `R5CX12FFTFF`。验证后的 0.2.0 APK 使用相同签名及 `adb install -r` 覆盖 0.1.0；没有清空数据、卸载或写入私人记录/人物。Android 报告 versionCode2/versionName0.2.0-local。冷启动返回 `Status: ok`、`LaunchState: COLD`，TotalTime285ms。机器可读证据（本地产物：`../reports/phone-v2.json`）。

手机随后锁屏。进一步真机页面检查需要主人解锁，本版这些检查为 NOT RUN。[0.1.0 真机材料](phone-validation.zh_CN.md)中的 UI/播放证据不能代替 0.2.0 新流程的验证。没有将锁屏截图/内容保留为应用证据。

未更改 Google 识别组件、默认提供方、全局字体、旋转或音量。未配置普通 MiMo 密钥或云服务凭据。应用已有原生录音及提供方适配，但仍需普通 API 密钥和实际麦克风到转写的验收。未通过 Android 确认界面安装生产更新。

Build: PASS — 已安装验证后的最终 APK。

Host tests: PASS — 72 个 Android 核心断言；仓库门禁另见[交付材料](delivery.zh_CN.md)。

Device tests: 安装、版本检查及冷启动响应 PASS，仅限这些检查。模拟器 98 项断言及最终 APK 页面检查属于独立证据。

Unverified: 解锁后的第二版真机页面，实际中文识别/TTS、音质、长后台/来电/耳机、重启提醒、TalkBack、普通提供方、云身份/共享权限和生产更新频道。
