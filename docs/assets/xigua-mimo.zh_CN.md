[English](xigua-mimo.md) · **简体中文**

# 西瓜 MiMo 配置

Token Plan 地址为 `https://token-plan-cn.xiaomimimo.com/v1`。设备运行时保存完整
`/chat/completions` 地址、凭据和日常模型。凭据不编译进固件，不写入模型配置文件。
分区布局不变的分段升级保留这些配置。当前 NVS 存储没有加密。

## 模型分工

共享的[非敏感配置](../../tools/xigua_mimo_profiles.json)定义：

| 用途 | 模型 | 当前接入状态 |
| --- | --- | --- |
| 日常交互 | `mimo-v2.6-flash` | 设备 USB 文本请求及电脑工具 |
| 复杂分析 | `mimo-v2.6-pro` | 电脑端明确调用 |
| 分析备选 | `mimo-v2.5-pro` | 电脑端明确调用；不自动重试或切换 |
| 通用/多模态模型 | `mimo-v2.5` | 工具目前仅支持文本输入，未实现图片输入 |
| 语音识别 | `mimo-v2.5-asr` | 电脑端 WAV/MP3 文件输入 |
| 标准语音合成 | `mimo-v2.5-tts` | 电脑端输出 WAV；默认 `mimo_default` 音色 |
| 声音克隆 | `mimo-v2.5-tts-voiceclone` | 可选，电脑端需提供参考音频 |
| 音色设计 | `mimo-v2.5-tts-voicedesign` | 可选，电脑端需提供音色描述 |

设备尚未实现麦克风录音、ASR 上传、AI 回答显示及云语音播放。填写模型名称不会自动
补齐这条链路。本地白噪声、雨声、海浪和计时提醒独立于云服务。设备当前只使用一个
配置好的文本模型；电脑端分工配置不会暗中修改 NVS 或让设备自动切换 Pro。

## 电脑端联调工具

使用 Python 运行 [xigua_mimo.py](../../tools/xigua_mimo.py)。工具读取 `MIMO_API_KEY`
环境变量，或通过隐藏输入提示获取 Key；不要把 Key 写进命令参数或提交文件。
调用会产生用量，不自动重试。音频输入最多 1 MB，响应最多 2 MB，请求超时 45 秒。
拒绝 HTTPS 重定向，服务错误信息经过清理，已有输出文件不会被覆盖。

```text
python tools/xigua_mimo.py models
python tools/xigua_mimo.py daily --text "Reply OK only."
python tools/xigua_mimo.py analysis --text "Compare these two schedules."
python tools/xigua_mimo.py tts --text "Ready." --output build/ready.wav
python tools/xigua_mimo.py asr --audio build/ready.wav
python tools/xigua_mimo.py voicedesign --style "A warm, calm voice" --text "Ready." --output build/designed.wav
python tools/xigua_mimo.py voiceclone --audio reference.wav --text "Ready." --output build/cloned.wav
```

2026-09-29 的鉴权模型列表包含上述八个模型。日常文本调用返回 HTTP 200。标准 TTS
生成有效的 24 kHz、16 bit、单声道 WAV，ASR 成功转写该合成样例。这是电脑端云接口
检查，不是设备麦克风/扬声器验收。Pro、两个旧版文本模型、声音克隆和音色设计仅确认
出现在模型列表，未实测生成。设备已确认保存 AI 配置，但 Wi-Fi 当时仍未配置。

## 接口依据

[官方 Token Plan 配置说明](https://mimo.mi.com/docs/zh-CN/tokenplan/integration/mimo-desktop)
列出了独立的 Token Plan 地址和凭据。
[ASR](https://mimo.mi.com/docs/en-US/api/audio/Speech-Recognition) 与
[TTS](https://mimo.mi.com/docs/en-US/api/audio/tts) 均使用 Chat Completions，但消息结构不同。
ASR 发送音频 data URL；TTS 在 assistant 消息中给出待播报文本并返回 Base64 音频。
声音克隆需要参考样本，音色设计使用描述而非 voice ID。
