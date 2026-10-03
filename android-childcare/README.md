<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Xigua Childcare for Android

Native private household app, package `cn.xigua.childcare`, minimum Android8/API26, target35. Version **0.6.6-cloud/code13** uses the original local signer and SQLite schema3; upgrade preserves records, caregivers and pending operations. The phone shell is one private family/one baby: Care, Records and Family roots, with Settings for AI, sync, backup and updates.

## Install

APK (local artifact: `build/apk/xigua-childcare-0.6.6-cloud.apk`): `adb -s SERIAL install -r build/apk/xigua-childcare-0.6.6-cloud.apk`.
Preserve `.local/signing/childcare-local.jks` for compatible future updates. It is excluded from the source archive. A fresh clone with another signing key cannot overwrite the installed app.

## Daily use

| Entrance | Purpose |
| --- | --- |
| Care | One microphone/keyboard entrance routes records, questions, stories, audio, reminders and history. Every proposed record opens a confirmation editor before saving. |
| Records | Vertical daily timeline, actual caregiver/time/action, milk volume and diaper details; direct entry, local totals, retrospective correction and pagination. |
| Family | Baby details and caregiver title registration/edit/select/deactivate/restore. |

No Bluetooth or Wi-Fi provisioning: Android owns networking. Care tools contain media/reminders/handoff; user operations use Android document selection, notifications, media focus and installer.

## Voice and flexible AI

Tap Speak, then **Send while recording** to stop, transcribe and dispatch once. Stop Recording returns editable text. Send during transcription waits for that attempt; duplicate/late/cancelled results cannot write records. Permission grant continues automatically. Background cancels the active microphone/submission. Failed completed audio remains one private retry draft; Retry Recognition or Cancel Voice is available. The composer survives periodic refresh while recording/transcribing.

AudioRecord writes16kHz mono16-bit PCM without a fixed60-second recording cutoff. Disk limits protect phone storage. Long recordings are transcribed sequentially as bounded WAV segments; no full audio/base64 heap copy. This implementation uses app-owned capture plus the selected cloud ASR, without requiring Google's recognition component.

Settings → **AI Services and Models** saves up to20 named Base URL/API-key profiles. Chat, ASR, TTS, vision and JEV judgment each choose their service and model. Fetch Models calls authenticated `GET /models`; returned IDs are selectable, with manual model-ID entry when discovery is unavailable. Auth supports Bearer and API-Key. ASR supports MiMo audio messages or compatible multipart `/audio/transcriptions`. Model discovery does not prove speech compatibility; connection checks and actual calls expose failures. Editing defaults affects later operations; active requests retain their configuration.

The private APK intentionally includes the owner-authorized key with **https://token-plan-cn.xiaomimimo.com/v1**. Default chat is `mimo-v2.6-flash`, ASR `mimo-v2.5-asr`. Key values do not enter reports. Existing populated v2 settings migrate into a separate profile. Cloud backup requires its own service credentials. The APK update channel is a separate service from Cloud Backup: new builds use `/android-updates/`; the client keeps `/cloud-backup/android-childcare-updates/` as a bounded transition fallback for older releases.

Chinese/decimal feeding quantities are supported; blank quantity means unknown. Negated/compound commands ask for clarification. Retrospective timestamps and sleep end times need explicit confirmation. Caregiver registration is one sentence with one field: “I am the baby’s ___”, such as Dad, Grandpa or Grandma. No separate name or phone number is required. New records and their cloud attribution use this title; existing members use their relationship, falling back to their old name when absent. Each activity chooses its actual caregiver and stores that title at the time. The name snapshot synchronizes with compatible backends; rename/deactivation cannot rewrite history. These profiles are not cloud accounts, invitations or access control. One baby identity is supported.

## Data, media and maintenance

SQLite event/outbox transactions retain UUID retries, revisions, tombstones and generation fences. Optional Personal API backup captures service/profile identity with each intent. New protocol discovery accepts profile objects; missing/changed identity quarantines old queues instead of rebinding them. Existing-row editors and sleep completion inherit original identity. Explicit conflict recovery creates a new intent. Production backup is not accepted by local fixture tests. The phone now exports a real ZIP archive with a manifest, NDJSON snapshots, integrity-ready structure and an explicit media inventory; restore is intentionally withheld until transactional validation is shipped. CSV remains an internal compatibility format only.

One foreground MediaPlayer/MediaSession handles playback, focus, headset removal and stop. Imports allow128MiB/item,256MiB total; generated rain works offline. Restart does not autoplay; saved-position recovery is explicit. TTS uses an installed Chinese voice. Reminders/handoff remain local; scheduling depends on Android.

Version Maintenance automatically checks the independent private HTTPS channel at `/android-updates/`, falls back to the legacy `/cloud-backup/android-childcare-updates/` manifest and download when needed, shows release notes, downloads and validates newer same-package/exact-signer APKs, then requests Android installation confirmation. It preserves local data and clears installed candidates. Legacy blank update settings migrate independently from provider profiles. Checks run at most once per six hours; download/install remain explicit. [Cloud updates and deployment](docs/cloud-updates.md). The new primary route is live, and the private publisher now uses it. Both routes now expose code13 / 0.6.6-cloud with the new primary APK URL; the legacy alias remains available so older clients can obtain a transition-capable APK.

Samsung SM-S9280 was upgraded through ADB to code12 / 0.6.5-cloud with the original signer and application data retained. Both live cloud aliases return the same authenticated manifest and APK hash. Earlier code5/code6 cloud-download checks remain historical evidence in [cloud acceptance](docs/cloud-acceptance.md) and [bootstrap acceptance](docs/phone-v5-validation.md).

The owner's standing authorization for scoped APK maintenance, token configuration and private-project Git synchronization is recorded in [cloud operation](docs/cloud-updates.md#standing-owner-authorization). It retains the existing scope and current acceptance limitations. The [sync/storage audit](docs/sync-storage-audit.md) confirms record operations and explicit ZIP export use separate flows.

## Build and validation

JDK17, Android SDK35/build-tools35.0.1/platform-tools, Python3; `JAVA_HOME` and `ANDROID_SDK_ROOT` select paths. Framework views/SQLite, direct aapt2/javac/d8/zipalign/apksigner, no WebView runtime. A source checkout carries empty-credential templates; restore the authorized private assets and original signer as described in [configuration preparation](config/README.md) before building an update. The Git synchronization does not change the already-published code13 APK.

```sh
./tools/test-host.sh
./tools/build.sh
```

Run `tools/local-api-fixture.py` with the backend Python environment, then `./tools/device-test.sh emulator-NNNN` for disposable actual-backend integration. Device/UI fixture scripts refuse physical-phone serials. `native-ui-smoke.py` resets only an isolated emulator and changes its font/rotation settings. Synthetic update APKs are tests, not releases. Live-provider instrumentation uses a generated phrase without personal records.

[Current phone framework](design/phone-v7-framework.md), [historical design](docs/phone-v3-design.md), [implementation plan](docs/phone-v3-plan.md), [UI specification](design/android-phone-design.md), [prototype](design/prototype.html), [architecture](docs/architecture.md), [resource budget](docs/resource-budget.md), [historical delivery/evidence](docs/delivery.md). Prototype responses are simulations. Android sources, empty-credential templates, paired documentation and selected sanitized receipts are synchronized in Git under the owner's authorization; unrelated root/backend work remains outside this commit. No firmware flash. The isolated private update service and signed APKs were published within the authorized deployment scope. [Timeline design and validation](docs/caregiver-timeline.md), [implementation plan](docs/caregiver-timeline-plan.md).
