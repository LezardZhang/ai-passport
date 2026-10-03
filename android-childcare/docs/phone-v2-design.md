<p align="right"><a href="phone-v2-design.zh_CN.md">简体中文</a> · <strong>English</strong></p>

Historical v0.2.0 document; current behavior is described in [v3 design](phone-v3-design.md).

# Phone care workflow and lifecycle

## User intent
The owner rejects fragmented phone menus and a prototype that merely runs. All voice/LLM functions share one care entrance; spoken or typed intent selects the next action. People need registration, management and profile updates. Earlier authorization to implement autonomously continues; no additional design approval gate is introduced.

## Product design
Three destinations: Care, Records, Family. Care has a single editable composer for microphone and keyboard, with record, question, story, media, reminder and query intents. Record changes are previewed in the existing structured editor. Ambiguous, negated, multi-action and retrospective inputs never silently write. Questions about feeding or sleep remain questions. Responses retain durable request ID/type and cancellation ownership behind this unified view.

Family contains one existing baby profile (stable cloud identity baby), dated profile information, and a local caregiver roster. A caregiver can be registered, edited, selected, deactivated and restored. The selected caregiver cannot be deactivated until another is selected; historical records retain their original caregiver snapshot. Existing installation data survives the schema upgrade. Local roster registration is not remote account authentication. Cross-device accounts require a backend identity interface: the current fixed bearer/profile API has none; clarification is pending.

MiMo-backed native recording avoids relying on disabled Google components. AudioRecord captures 16 kHz mono PCM to bounded temporary WAV; explicit Stop transcribes to editable text, Cancel/background invalidates callbacks and releases recording. A dedicated bounded transport streams base64. Integration probes found that the existing Token Plan key works at its coding endpoint but is rejected by the ordinary application API. The shipped APK contains no key; Family has an ordinary API configuration flow. System SpeechRecognizer remains an explicit fallback. Text AI uses the configured existing backend when present or MiMo directly when configured; provider/network failures are visible and retry is explicit. Actual microphone speech with ordinary application credentials remains unverified.

Version maintenance displays the installed package version and an optional HTTPS release channel. A release manifest declares package, monotonically increasing version code, minimum SDK, APK HTTPS URL, SHA256 and release notes. Downloads and imported APKs are bounded, package/version/signing identity are checked, and installation opens Android's own confirmation UI. No feed means no claim of online update availability. Interrupted staging is cleaned up. Update installation preserves the same package, signing certificate and SQLite data.

## Scope and evidence
Only android-childcare changes. Backend and firmware are read-only. Do not enable Google services, clear physical-phone data, publish or deploy. APK version code 2 / 0.2.0-local uses the existing signer. Host tests cover routing, profile validation and update metadata rules; isolated instrumentation covers v2-to-v3 migration, caregiver transitions/provenance and request recovery; emulator UI checks the three-entry flow, font scale, rotation and lifecycle. Install the validated upgrade on the connected phone without clearing data. Report unavailable cloud account/production update capabilities explicitly.
