<p align="right"><strong>简体中文</strong> · <a href="phone-v3-validation.md">English</a></p>

# 真机0.3.0验证 — 2026-10-03

三星SM-S9280，Android16/API36，序列号R5CX12FFTFF。同签名`adb install -r`安装最终code3/0.3.0-local，不清数据、不卸载主应用、不写私人记录/人物。APK SHA256 `acf748d827a7cb94345e9b9f993fea7f1333b2878f3342e491d798994c736d59`。安装（本地产物：`../reports/phone-v3-install.log`）、版本（本地产物：`../reports/phone-v3-version.txt`）、启动（本地产物：`../reports/phone-v3-launch.txt`）。

真实提供方验收：**NOT RUN**。日志（本地产物：`../reports/phone-v3-live-provider.log`）：临时同签名测试用独立一次性数据库和生成中文WAV验证真实模型/对话/ASR及150mL分流，不保存记录；随后移除测试包及波形。这是Android/提供方验证，不代表人声麦克风准确率。

真机页面及麦克风开始/取消：**NOT RUN (phone locked)**。安装后手机再次锁屏，未保留锁屏内容、不绕过锁屏。

未改变Google组件、全局字号/旋转/音量或其他应用。Build: PASS；Host tests: PASS111；Device tests: PASS354模拟器断言及以上真机安装/接口状态。Unverified:人声/音质、TTS、最低设备、来电/耳机/长后台/重启提醒/TalkBack、容量及生产身份/发布服务。
