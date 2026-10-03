<p align="right"><a href="phone-v2-plan.zh_CN.md">简体中文</a> · <strong>English</strong></p>

Historical v0.2.0 document; current behavior is described in [v3 design](phone-v3-design.md).

# Phone v2 implementation plan

> **For agentic workers:** Use superpowers:executing-plans for inline execution. The UI design subagent remains responsible only for design/.

**Goal:** Ship a simpler care entrance and durable people/version lifecycle on the existing Android app.
**Architecture:** Native framework views, pure routing/validation rules, SQLite schema 3, bounded MiMo transport and Android package installer. Retain the current sync and media owners.
**Tech Stack:** Java 8 source, Android API 26–35, SQLite, HttpURLConnection, AudioRecord.
**Spec:** [Phone v2 design](phone-v2-design.md).

## Constraints and review focus
Preserve existing records, outbox identities and signer. Backend and firmware remain read-only. Local caregivers confer no cloud permissions. Do not infer write intent from domain keywords in a question. Cancellation rejects late transcripts/results. Never accept an update for another package/signer or a lower version. Persist history attribution through caregiver edits/deactivation.

## Tasks
- [x] Add failing host assertions for IntentRouter.classify(String), FamilyRules.validateChild/validateMember and UpdateRules.validate metadata. Verify failure, implement pure rules, run tools/test-host.sh.
- [x] Migrate Store from schema2 to3: members table; event caregiver ID/name fields; registration/select/deactivate/restore; transactionally edit child metadata. Add isolated Android tests including a populated v2 migration.
- [x] Replace the four-tab/split-Q&A/story navigation with Care/Records/Family. Use one composer and route previews; render per-request response/cancel/retry in the same care stream. Add child/member dialogs and service/version status.
- [x] Implement foreground recording ownership, temporary WAV cleanup, streamed base64 upload and MiMo direct text fallback. Add transport/cancellation tests without personal speech or tokens in logs; bound request concurrency and resource phases.
- [x] Implement optional HTTPS release manifest and bounded signed APK import/download; open system installer with a private readonly content provider. Test metadata rejection, wrong package/signer and interrupted staging.
- [x] Run host/instrumentation/native UI and repository gates; review new code; install the same-signer v2 APK on the identified Samsung without clearing data; archive source/evidence and update bilingual delivery documents with remaining cloud account/real-speech/production-release checks.

No commits/pushes/publication are requested. Update this checklist from actual evidence.

## Product completion still open
- [ ] Agree and implement remote account registration, family invitations/permissions and cross-phone membership if required; current backend has no identity interface.
- [ ] Configure an ordinary MiMo API key and accept actual phone microphone speech/TTS; coding endpoint probes are separate.
- [ ] Unlock the phone for v2 page acceptance and test production HTTPS API/release channel and Android installer.
