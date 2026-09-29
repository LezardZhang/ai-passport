**English** · [简体中文](xigua-mimo.zh_CN.md)

# Xigua MiMo configuration

The Token Plan endpoint is `https://token-plan-cn.xiaomimimo.com/v1`.
The device stores the complete `/chat/completions` URL, its credential and the
daily model in runtime configuration. Credentials are never compiled into the
firmware or stored in the model profile. A segmented update with the unchanged
partition layout preserves this configuration. The existing NVS storage is not
encrypted.

## Model roles

The shared, non-secret [profile](../../tools/xigua_mimo_profiles.json) configures:

| Role | Model | Current integration |
| --- | --- | --- |
| Daily interaction | `mimo-v2.6-flash` | Device USB text requests and host tool |
| Complex analysis | `mimo-v2.6-pro` | Explicit host request |
| Analysis alternative | `mimo-v2.5-pro` | Explicit host request; no automatic retry/failover |
| General/multimodal model | `mimo-v2.5` | Host text input currently; image input not implemented |
| Speech recognition | `mimo-v2.5-asr` | Host WAV/MP3 file input |
| Standard speech | `mimo-v2.5-tts` | Host WAV output; `mimo_default` voice |
| Voice cloning | `mimo-v2.5-tts-voiceclone` | Optional host request with supplied reference audio |
| Voice design | `mimo-v2.5-tts-voicedesign` | Optional host request with a voice description |

The board does not yet implement microphone capture, ASR upload, AI response
display or cloud speech playback. Assigning these model names does not enable
that missing pipeline. Its local white noise, rain, waves and timer alert remain
independent of the cloud. The device currently uses one configured text model;
the host role profile does not silently change its NVS or select Pro on-device.

## Host integration tool

Run [xigua_mimo.py](../../tools/xigua_mimo.py) with Python. The tool reads
`MIMO_API_KEY` or prompts without echo. Do not put the key in a command argument
or committed file. Calls generate usage; there are no automatic retries. Audio
input is limited to 1 MB and responses to 2 MB, with a 45-second request timeout.
HTTPS redirects are refused, service errors are sanitized, and existing output
files are never overwritten.

```text
python tools/xigua_mimo.py models
python tools/xigua_mimo.py daily --text "Reply OK only."
python tools/xigua_mimo.py analysis --text "Compare these two schedules."
python tools/xigua_mimo.py tts --text "Ready." --output build/ready.wav
python tools/xigua_mimo.py asr --audio build/ready.wav
python tools/xigua_mimo.py voicedesign --style "A warm, calm voice" --text "Ready." --output build/designed.wav
python tools/xigua_mimo.py voiceclone --audio reference.wav --text "Ready." --output build/cloned.wav
```

On 2026-09-29, authenticated model listing returned all eight configured models.
The daily text request returned HTTP 200. Standard TTS produced valid 24 kHz,
16-bit mono WAV, and ASR transcribed that synthetic sample successfully. These
were host cloud checks, not microphone/speaker acceptance. Pro, the two older
text models, voice clone and voice design were listed but not generation-tested.
The device acknowledged saving its AI configuration; Wi-Fi was still unconfigured.

## API references

The [official Token Plan setup](https://mimo.mi.com/docs/zh-CN/tokenplan/integration/mimo-desktop)
identifies the separate Token Plan base URL and credentials.
[ASR](https://mimo.mi.com/docs/en-US/api/audio/Speech-Recognition) and
[TTS](https://mimo.mi.com/docs/en-US/api/audio/tts) use Chat Completions with
different message schemas. ASR sends an input-audio data URL. TTS sends target
text in an assistant message and receives Base64 audio; voice clone requires a
reference sample, while voice design uses a description instead of a voice ID.
