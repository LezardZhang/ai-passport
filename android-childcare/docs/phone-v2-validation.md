<p align="right"><a href="phone-v2-validation.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Phone v0.2.0 validation — 2026-10-03

Samsung SM-S9280, Android 16/API36, serial `R5CX12FFTFF`. The verified v0.2.0 APK was installed over v0.1.0 using the same signer and `adb install -r`; no data clear, uninstall or personal record/member writes occurred. Android reports versionCode2/versionName0.2.0-local. Cold launch returned `Status: ok`, `LaunchState: COLD`, TotalTime285ms. Machine-readable evidence (local artifact: `../reports/phone-v2.json`).

The phone subsequently locked. Further physical page checks require the owner to unlock it; they are NOT RUN for this version. Historical v0.1.0 UI/playback evidence in [phone-validation](phone-validation.md) does not validate the new v0.2.0 flows. No lock-screen screenshot/content is retained as application evidence.

No Google recognition component, default provider, global font, rotation or volume was changed. No ordinary MiMo key or cloud service credential was configured. The app's native recording/provider adapter exists, but an ordinary API key and real microphone-to-transcript acceptance are still needed. No production update was installed through Android's confirmation UI.

Build: PASS — the verified final APK was installed.

Host tests: PASS — 72 Android core assertions; repository gate reported separately in [delivery](delivery.md).

Device tests: PASS for installation, version inspection and cold-launch response only. Emulator 98 assertions and targeted final-APK UI evidence are separate.

Unverified: physical v2 pages pending unlock; actual Chinese speech/TTS, audio quality, long background/calls/headset, reboot reminders, TalkBack, ordinary provider, cloud identities/shared access and production release channel.
