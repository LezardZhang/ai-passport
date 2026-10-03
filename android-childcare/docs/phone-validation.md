<p align="right"><a href="phone-validation.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Physical phone validation — 2026-10-03

This document records the historical v0.1.0/code1 phone test. The newer v0.2.0 native MiMo adapter and people/update changes are documented separately in delivery; statements below about an unimplemented MiMo route apply only to v0.1.0.

Device: Samsung SM-S9280, Android 16 / API 36, USB ADB authorized. The independently signed package `cn.xigua.childcare` was absent before installation; the adult-health app was present. Verified APK SHA-256 `577b9fe2c1fec91ee56fb8de8c978c58412c289155d60e36cf9365542746ec7e`, installed successfully, then cold-started MainActivity with `Status: ok` and 306 ms activity launch time.

## Bounded acceptance results

| Check | Result |
| --- | --- |
| Install, cold launch, Chinese native page | PASS; home screenshot (local artifact: `../reports/phone/phone-home.png`) |
| Feeding form open/cancel | PASS; no record was saved; form (local artifact: `../reports/phone/phone-feeding-form.png`) |
| Companion and audio entry | PASS after scrolling to the entry in the actual phone viewport |
| Offline rain, Pause, Continue, Stop | PASS for player/UI state; player (local artifact: `../reports/phone/phone-audio-playing.png`) |
| Home during playback | PASS for a bounded two-second observation: PlaybackService remained `isForeground=true`; service evidence (local artifact: `../reports/phone/phone-background-service.txt`) |
| Return to app and Today | PASS; stopped playback and returned to home |
| Personal records/adult application | No record writes/clears or adult application changes; no global font/rotation/volume changes |
| Memory sample after Stop | PSS 115,196 KiB (about 112.5 MiB); sample (local artifact: `../reports/phone/phone-memory.txt`). Single small-dataset point, not a peak/leak proof |

Machine-readable result (local artifact: `../reports/phone/phone-smoke.json`): 11 phone UI/playback operations passed. An initial driver lookup missed the audio button below the viewport; scrolling resolved the lookup without an application change. Sound quality still needs human confirmation; a player reaching Playing is not proof of audible quality or correct route.

## Native speech dependency found

The primary user currently has no enabled service for the Android `android.speech.RecognitionService` action and no standard recognition Activity. A query including disabled components finds the existing Google recognition component; it is not currently an available default provider. The Google application itself is installed/enabled, so this is a service/component availability issue rather than an APK install failure. Samsung TTS service is available, but Chinese voice coverage/actual synthesis and sound have not yet been tested.

No Google component or system default voice setting was changed. Enabling another application's component was presented for user choice. The user then asked which approach is standard; Android's public API invokes the configured recognition service, rather than mandating Google. The existing hardware application already uses MiMo ASR, so an application-owned recording/MiMo route was recommended for consideration. It is **not implemented in this APK**; current recognition remains SpeechRecognizer plus keyboard/IME fallback. MiMo would require network access and its configured API authorization.

Sources: [Android SpeechRecognizer](https://developer.android.com/reference/android/speech/SpeechRecognizer.html), [MiMo recognition](https://mimo.mi.com/docs/zh-CN/quick-start/usage-guide/audio/Speech-Recognition).

Build: PASS (same verified APK installed; no application code change).

Host tests: PASS (previous 33 assertions and repository host gate; unchanged code).

Device tests: PASS for the bounded physical install/UI/media checks above; full phone acceptance is incomplete.

Unverified: microphone-to-transcript on this phone, Chinese TTS/speaker quality, call/focus/headset behavior, long background playback, reboot reminders, TalkBack, physical offline-sync/conflict scenarios and production backend/real AI. Emulator database/protocol/focus regressions remain separate evidence. See [delivery](delivery.md).
