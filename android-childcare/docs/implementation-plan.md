<p align="right"><a href="implementation-plan.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Android Childcare implementation plan

**Goal:** Deliver an installable native childcare app and reviewable design.
**Architecture:** Local SQLite source of truth, durable outbox, contract-based network adapter; native voice and foreground media playback.
**Spec:** [Architecture](architecture.md); [UI specification](../design/design-spec.md).
**Execution:** Implement in this session; the already delegated designer owns design/. User authorized implementation without repeating direction/approval prompts. No shared branch changes or commits.

## Global constraints
- Write only android-childcare/; keep maintained Markdown paired.
- Package cn.xigua.childcare; minimum API 26, target API 35; dedicated local development signing identity.
- No unbounded media/JSON arrays or UI-thread networking; no fabricated live integrations.

## Review focus
- Midnight/DST sleep overlaps: duration belongs to the intersection with the selected local day.
- Offline delete/retry: operation identity survives restart; deletion remains a tombstone.
- Remote reset/conflict: old edits cannot silently revive or overwrite records.
- Background/cancel: no microphone retention; stale AI results never enter the other mode.
- Process loss/audio focus: reconstruct durable state, never autoplay, release audio on stop.

## Tasks
1. [x] Write failing host tests for CareRules day ranges, validation, CSV safety and RequestOwner; implement pure Java rules and run tests.
2. [x] Implement Store SQLite transactions, pending operations, deletion/undo, active sleep, reminders, requests and bounded queries. Android tests cover persistence and outbox durability.
3. [x] Implement the four native destinations and edit sheets following the designer specification. All pages reload the same Store; preserve drafts and date across recreation.
4. [x] Implement native speech lifecycle and independent durable Q&A/Story jobs. Tests exercise cancellation and request ownership.
5. [x] Implement mediaPlayback service, focus, notifications, streaming/local library, bounded import and sleep timer. Check actual UI and device behavior.
6. [x] Read the actual backend contract; implement capability-gated push/pull, conflicts, reset handling, server-statistics comparison, AI jobs and media catalog. Run contract integration against a local test server if available.
7. [x] Build/verify signed APK using SDK tools; run host/Android/repository gates, capture UI/device evidence, write bilingual delivery with hashes and explicit unverified checks.

## Validation commands
`tools/test-host.sh`; `tools/build.sh`; `tools/device-test.sh`; repository `./tools/validate.sh`.

## Execution status

Local APK, delegated design, host/emulator tests and repository gates are delivered. Physical-phone and production integration remain unverified; see [delivery](delivery.md). The automated device script is emulator-only; emulator evidence is not physical-phone acceptance.
