<p align="right"><strong>简体中文</strong> · <a href="phone-validation.md">English</a></p>

# 真机验证 — 2026-10-03

本文是0.1.0/版本号1的历史真机测试。新版0.2.0的原生MiMo适配及人物/更新机制另见交付说明；下文“尚未实现MiMo”仅针对旧版0.1.0。

设备：三星 SM-S9280，Android 16 / API 36，USB ADB 已授权。安装前独立包 `cn.xigua.childcare` 不存在，成人健康应用已安装。核对 APK SHA-256 `577b9fe2c1fec91ee56fb8de8c978c58412c289155d60e36cf9365542746ec7e` 后安装成功；MainActivity 冷启动返回 `Status: ok`，启动耗时 306 ms。

## 有范围的验收结果

| 检查 | 结果 |
| --- | --- |
| 安装、冷启动、原生中文页面 | PASS；首页截图（本地产物：`../reports/phone/phone-home.png`） |
| 喂养表单打开/取消 | PASS，未保存记录；表单（本地产物：`../reports/phone/phone-feeding-form.png`） |
| 陪伴与音频入口 | PASS，在真实视口内滚动到入口后可操作 |
| 离线轻雨声、暂停、继续、停止 | 播放器/UI 状态 PASS；播放器（本地产物：`../reports/phone/phone-audio-playing.png`） |
| 播放时按 Home | 两秒观察窗口内 PASS，PlaybackService 保持 `isForeground=true`；服务证据（本地产物：`../reports/phone/phone-background-service.txt`） |
| 返回应用与今日 | PASS，停止播放后返回首页 |
| 个人记录/成人应用 | 未写入或清空记录，未修改成人应用；未改变全局字体/旋转/音量 |
| 停止后的内存采样 | PSS 115,196 KiB（约 112.5 MiB）；采样（本地产物：`../reports/phone/phone-memory.txt`）。仅少量数据下的单点，不能证明峰值或无泄漏 |

机器可读结果（本地产物：`../reports/phone/phone-smoke.json`）：11 个真机 UI/播放操作通过。首次测试脚本未找到视口下方的音频按钮；滚动后完成查找，没有修改应用。音质仍需要人实际确认；播放器进入“播放中”不能证明可听音质或输出路由正确。

## 原生语音依赖发现

主用户目前没有启用的 Android `android.speech.RecognitionService` 服务，也没有标准语音识别 Activity。包含停用组件的查询能找到现有 Google 识别组件，但它当前不是可用默认提供方。Google 应用本身已安装/启用，因此这是服务/组件可用性问题，不是 APK 安装失败。三星 TTS 服务可用，但还未测试中文语音覆盖、真实合成和音质。

未修改 Google 组件或系统默认语音设置。启用其他应用组件的选择已呈给用户；用户随后询问主流接入方式。Android 公共接口调用系统配置的识别服务，不强制 Google。现有硬件应用已用 MiMo ASR，因此建议考虑应用自行录音并调用 MiMo。**此 APK 尚未实现该路线**，目前仍为 SpeechRecognizer 加键盘/输入法回退；MiMo 需要联网和已配置的 API 鉴权。

来源：[Android SpeechRecognizer](https://developer.android.com/reference/android/speech/SpeechRecognizer.html)、[MiMo 识别](https://mimo.mi.com/docs/zh-CN/quick-start/usage-guide/audio/Speech-Recognition)。

Build：PASS，同一已验证 APK 已安装，应用代码未改变。

Host tests：PASS，之前的 33 个断言和仓库主机门禁，代码未改变。

Device tests：上述有范围的真实安装/UI/播放检查 PASS，完整真机验收未完成。

Unverified：这部手机的录音转写、中文 TTS/扬声器音质、来电/焦点/耳机、长后台、重启提醒、TalkBack、真机离线同步/冲突和生产后端/真实 AI。模拟器数据库/协议/焦点回归仍是独立证据，详见[交付](delivery.zh_CN.md)。
