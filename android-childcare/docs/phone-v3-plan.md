<p align="right"><a href="phone-v3-plan.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Android phone v3 implementation plan

> **For agentic workers:** Use superpowers:executing-plans inline; one final fresh review. UI designer owns design/ only.

**Goal:** Make voice submission lifecycle correct and rebuild daily modules around Android.
**Architecture:** SpeechSession pure state machine, CareComposer retained native views, injectable SpeechBackend, foreground AudioRecord and streamed long-WAV segmentation with private retry draft. Tracked configuration supplies service defaults. Retain SQLite schema3 and existing media/sync owners.
**Tech:** Java/Android API26-35, SQLite, WAV, HTTPS, system media/permissions/document/installer APIs.
**Spec:** [v3 design](phone-v3-design.md).

## Constraints and review focus
Only android-childcare changes; existing package/signer/data. No raw secrets in evidence, no production publication. Explicit applicable-key embedding authorized. Send cannot cancel its own in-flight transcription; refresh cannot replace voice controls; old listeners/results cannot dispatch; failures retain a retry draft; long speech stays file-streamed and bounded.

## Tasks
- [x] Host RED: SpeechSession recording/transcribing Send, duplicate/late/error/cancel handling; SpeechAudio long WAV split (>60s), valid header/alignment and bounded memory. Implement isolated rules and run host suite.
- [x] Replace VoiceInput/MicrophoneCapture lifecycle; add SpeechBackend/Providers/SpeechDrafts, validate provider before capture, remove fixed cutoff and support sequential file chunks/retry. Prove permission/cancel/background behavior with Android tests.
- [x] Add CareComposer and retain it through voice refresh; MainActivity routes complete strings only. Remove legacy split voice form and routine transport diagnostics; retain intentional multi-provider settings; rebuild family/service/update daily modules from design.
- [x] Built-in applicable provider/optional cloud/release defaults, no Wi-Fi credentials; verify presence/authentication without printing values. Matching Token Plan defaults authenticated; live Android models/chat/synthetic ASR passed.
- [x] Build code3/0.3.0-local, regenerate code4 synthetic update fixtures, run host/Android/native UI/full gate and final review. Same-signer phone upgrade without clearing data, bounded phone UI and live speech when credential available. Update paired docs/resource budget and archive exact artifacts.

## Ledger
Ruling: Reuse the existing android-childcare checkout instead of a new worktree because the app is untracked here and another chat owns backend work; moving would omit current source. Preserve all unrelated changes.
Ruling: Do not introduce repeated design/plan approval or commit gates; the owner already authorized autonomous implementation and now explicitly requests refactoring.
Ruling: Phone recording gets no fixed duration cutoff; finite private-file/storage limits remain necessary to prevent disk exhaustion. Long ASR is sequentially segmented.
Ruling: Owner-authorized private credentials are tracked and packaged; the latest matching Token Plan instruction resolves the earlier ordinary-key dependency.

Ruling: Latest user steering explicitly requires matching Token Plan defaults plus multiple URL/key profiles and model discovery; retain these intentional user controls in Family, with independent chat/ASR choices and manual fallback.

Acceptance: host111, Android354, real models/chat/synthetic ASR; phone installation preserves data. See delivery for physical UI/network and remaining limits.
