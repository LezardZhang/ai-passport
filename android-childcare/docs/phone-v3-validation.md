<p align="right"><a href="phone-v3-validation.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Phone 0.3.0 validation — 2026-10-03

Samsung SM-S9280, Android16/API36, serial R5CX12FFTFF. Same-signer `adb install -r` installed final code3/0.3.0-local without clearing data, uninstalling the app or writing personal records/people. APK SHA256 `acf748d827a7cb94345e9b9f993fea7f1333b2878f3342e491d798994c736d59`. Install (local artifact: `../reports/phone-v3-install.log`), version (local artifact: `../reports/phone-v3-version.txt`), launch (local artifact: `../reports/phone-v3-launch.txt`).

Live provider acceptance: **NOT RUN**. Log (local artifact: `../reports/phone-v3-live-provider.log`): temporary same-signer instrumentation uses a separate disposable database and generated Chinese WAV, tests actual models/chat/ASR and150mL routing without saving a record. Its test package and waveform are removed afterward. This is provider/Android acceptance, not human microphone accuracy.

Physical pages and microphone start/cancel: **NOT RUN (phone locked)**. Phone relocked after successful install. No lock-screen contents retained and no lock bypass attempted.

No Google component, global font/rotation/volume or other application was changed. Build: PASS. Host tests: PASS111. Device tests: PASS354 emulator assertions plus installed phone/provider status above. Unverified: human speech/acoustic quality, TTS, minimum devices, calls/headsets/background/reboot reminders/TalkBack, capacity and production identity/release services.
