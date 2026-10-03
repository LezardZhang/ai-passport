<p align="right"><a href="phone-v4-validation.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Phone 0.4.0 validation - 2026-10-03

Samsung SM-S9280, Android 16 / API 36, serial R5CX12FFTFF. Same-signer `adb install -r` installed code 4 / 0.4.0-local without data clearing, uninstallation or synthetic record/person writes. Canonical installed APK SHA256 `e81687b9b1533a8f1ca01d1773b21217d2ecb7fe1ae7d289dff919168114ea46` equals the delivered APK. All non-signature APK entries also match the build used for page acceptance. Install (local artifact: `../reports/phone-v4-install.log`), version (local artifact: `../reports/phone-v4-version.txt`), hash verification (local artifact: `../reports/phone-v4.json`).

Physical UI: PASS 13 read-only assertions on the unlocked phone: Care composer, daily timeline, direct feeding/diaper entries, caregiver choice, milk amount, family roster/baby entry and caregiver registration name/relationship fields. Every opened form was cancelled; the phone was returned to Care. UI result (local artifact: `../reports/phone-v4-ui.json`). Private physical screenshots/XML remain local and are excluded from the delivery archive.

Physical microphone/provider/human recognition: NOT RUN for this iteration. Emulator acceptance includes real PCM capture/65-second recording/Send/retry/cancel, 36 actual-backend attribution assertions and native rotation/night/200% font checks. These do not establish physical speech accuracy or production synchronization. Production extension deployment, shared roster/accounts, minimum devices, accessibility, background audio and capacity remain unverified. Build: PASS. Host tests: PASS. Device tests: PASS with these specific limits. See [delivery](delivery.md).
