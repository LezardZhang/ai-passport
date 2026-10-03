<p align="right"><a href="delivery.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Local Android 0.4.0 delivery

Android and the management baby page now show a daily vertical timeline: actual caregiver, occurrence time, action and milk volume or diaper details. Both have direct feeding/diaper entry. Android retains baby/caregiver registration, editing and deactivation; each event preserves its historical name snapshot. The editor allows another caregiver for retrospective entries. Unknown times/details remain unknown and appear separately. See the [product and compatibility contract](caregiver-timeline.md).

The optional caregiver snapshot now synchronizes through the shared personal API. Legacy omissions preserve an existing snapshot; explicit null clears it. A frozen v3 operation is replayed unchanged, then followed by an attribution update. Unsupported services retain named records pending rather than dropping the name. Keeping a local conflict retains its chosen caregiver. Actor-only edits preserve custom feeding details, nullable diaper kinds, original occurrence times, intervals and independently supplied sleep durations. The existing identity fences, adult editor and durable web outbox remain.

## Artifact and validation

APK (local artifact: `../build/apk/xigua-childcare-0.4.0-local.apk`): **0.4.0-local / code 4**, 270,877 bytes, SHA256 `e81687b9b1533a8f1ca01d1773b21217d2ecb7fe1ae7d289dff919168114ea46`. Package `cn.xigua.childcare`, minimum API 26 / target 35. Certificate SHA256 `b9ba60dca0104e1968bdd6fa46e9459799bb2e4b69f2dd8d41c14a70321917a9`; original local signer, APK v2/v3 signatures verified. SQLite schema 3 remains. Baseline `8978fa86bba2b40554fda9e051b0f8f7d7ea5801`; changes remain uncommitted.

| Field | Result |
| --- | --- |
| Build | PASS signed/aligned APK and ESP-IDF 5.5.3 complete repository gate; no firmware change or flash |
| Host tests | PASS 125 Android assertions; 77 backend tests including Node presentation/form regressions; repository static/host checks |
| Device tests | PASS 354 full Android assertions plus 36 caregiver protocol assertions and 20 native UI assertions on an isolated API 35 emulator |
| Native UI | PASS timeline order, fractional feeding, explicit caregiver selection, rotation and custom ingredient preservation; daytime and night with 200% font screenshots inspected |
| Physical phone | PASS Samsung SM-S9280 / Android 16 API 36 same-signer upgrade, canonical installed APK hash, launch and 13 read-only page/form checks |
| Unverified | Production deployment of this extension; physical microphone/human ASR accuracy; TTS/audio quality, calls/headsets/long background/reboot reminders/TalkBack/API 26; load/capacity and shared account/roster/invitation/release services |

Evidence: build manifest (local artifact: `../reports/build-manifest-v4.json`), complete repository gate (local artifact: `../reports/repository-gate-v4.log`), host (local artifact: `../reports/timeline-host-tests.log`), backend (local artifact: `../reports/timeline-backend-suite.log`), Android (local artifact: `../reports/android-tests-v4.log`), caregiver sync (local artifact: `../reports/timeline-android-tests.log`), native day (local artifact: `../reports/timeline-native-ui-tests.log`), native night (local artifact: `../reports/timeline-native-ui-night-tests.log`), actual web forms (local artifact: `../reports/timeline-web-ui.json`), [physical phone](phone-v4-validation.md), delivery manifest (local artifact: `../reports/delivery-manifest.json`). Functional fixtures use disposable data, not production accounts. Screenshot-only harness changes followed the full regression; every non-signature entry of the final APK matches the phone-tested runtime.

One independent review found four material preservation bugs: lost caregiver choice in conflict recovery, replaced null/custom native details, invented historical timestamps and web recalculation of untouched sleep facts. Failing regressions reproduced them; repaired protocol and form checks passed. Unknown-duration labeling and the web undated group were also repaired. No Critical finding remains. UI harnesses now wait for visible controls, the newly injected composer and completed recreation, avoiding stale or hidden-button observations.

## Operation and retained artifacts

Voice still uses native AudioRecord and the configured ASR service. Send ends recording and waits for transcription; it submits once and opens a record confirmation. No fixed 60-second recording cutoff; 65-second real capture passed. Failed audio retains a private retry/discard draft. Multiple named URL/key profiles, API model discovery, manual models, independent chat/ASR bindings and the owner's bundled Token Plan configuration remain. No provider keys appear in routine evidence or summaries.

The local Android roster includes optional contact details; only per-record name/ID snapshots synchronize. Remote accounts, shared roster administration and invitations were not implemented. The management extension is locally tested and **not deployed to production**. Until the server advertises caregiver support, named records stay on the phone awaiting synchronization. Review [resource budgets](resource-budget.md) before treating large histories or platform allocations as measured capacity.

Version Maintenance accepts newer same-package/exact-signer APKs and verifies optional download manifests. No online release feed is configured. Code 5 update fixtures are synthetic, excluded from delivery. Preserve the signing key for future upgrades; an older APK is not a database downgrade guarantee.

Source/evidence archive (local artifact: `../build/source/xigua-childcare-0.4.0-local-source.zip`) includes app/tools/tests/docs/design, selected v4 evidence, the APK and the current backend source/contract snapshot with per-file hashes. The owner's authorized provider asset and APK are intentionally included; the private signer, local runtimes/AVD, user-data databases, physical screenshots/XML and synthetic update fixtures are excluded. Archive verification (local artifact: `../reports/source-archive-v4.json`). Backend changes are limited to the timeline contract/UI integration while preserving concurrent unrelated work. No commits, pushes or production publication.

Historical v3 archive (local artifact: `../build/source/xigua-childcare-0.3.0-local-source.zip`) and [v3 phone validation](phone-v3-validation.md) are retained unchanged.
