<p align="right"><a href="android-phone-design.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Android phone module and voice design

2026-10-03, revision 3: natural phone recording/answers/playback, authorized defaults, user-managed AI profiles and no hardware administration. The latest instruction retains AI Services in Family. This replaces earlier duration/setup recommendations and specifies the next implementation, not acceptance of the current APK.

Read-only evidence: native Care (local artifact: `../reports/final-v2/care.png`), service setup (local artifact: `../reports/final-v2/provider.png`), updates (local artifact: `../reports/final-v2/updates.png`) and `MainActivity`/`FamilyUi`/`UpdatesUi`. No credential value was read. The current single-service editor lacks the requested profile/model freedom; screenshots cannot establish recording-to-send behavior.

## Keep three destinations

| Destination | Necessary purpose | Restructure |
| --- | --- | --- |
| Care | Record, ask, hear and act | One owned voice/text composer; intent/result cards; active sleep; compact playback. Manual record, sound selection, reminders and handoff are secondary task surfaces. |
| Records | Inspect and correct facts | Local date/timeline/totals, detail editing, Undo and export. Remove the manual same-revision cloud-statistics check; reconciliation runs behind the accurate status. |
| Family | Maintain people and usable preferences | Baby/caregiver profiles, current actor, night/system text preferences, data export, AI Services and About/version. AI Services deliberately manages profiles/models; ordinary care does not require configuring them. |

Keep other tasks secondary: feeding/sleep/diaper first, bath/tummy-time under Other, timers within events, handoff through system sharing, and real available sounds in a chooser. No model/story/recorder/device/provisioning roots.

## Every current module has a phone purpose

| Current modules | Decision and responsibility |
| --- | --- |
| `MainActivity`, `Ui` | Keep the three-root native shell; detach composer state from rebuilt views. One input owner binds text, mic, status, cancel and Send. |
| `VoiceInput`, `MicrophoneCapture` | Keep native capture and recognition adapters; the composer owns their lifecycle, pending send and recovery. Remove the fixed 60-second product limit. |
| `IntentRouter`, `CareRules` | Keep tested routing/validation; ambiguous input asks a focused question, writes always preview the actual target/time/quantity. |
| `AiEngine`, `MimoClient`, `RequestOwner` | Keep capability-aware ASR/model tasks and separate question/story/media ownership. Resolve the authorized default internally and honor explicit profile/model choices; retain immutable request configuration ownership. |
| `Store`, `CareApp` | Keep durable records/profiles/outbox/drafts and bounded workers. Persist sleep and task ownership; store transactions decide success. |
| `FamilyUi`, `FamilyRules` | Keep local registration/edit/switch/archive/restore and history identity. Use a date picker, optional birthday and simple relationship choices; retain the editable form on failure. Open AI Services as a separate deliberate secondary page rather than mixing its editors into the person list. |
| `PlaybackService`, `PlaybackRules`, `DiskMedia` | Keep one real player, audio focus, bounded files and offline sounds; use Android media-session/notification controls. Pause/continue/stop follow actual playback state. |
| `Reminders`, `ReminderReceiver` | Keep durable reminder lists and Android scheduling. Ask notification permission when needed; denial retains the reminder and explains notification limits. |
| `Api`, `SyncService` | Keep only for actual optional childcare backend support. Reconcile/schedule automatically when usable; expose saved/pending/conflict status and actionable conflict recovery. No Wi-Fi credentials, provisioning wizard or foreground polling requirement. |
| `UpdatesUi`, `Updates`, `UpdateRules`, `UpdateProvider` | Keep verified maintenance as About/version → Update. Use the bundled release channel if one exists, or Android document selection for a signed APK. Remove manual channel URLs, hashes and signature terminology from the ordinary flow; retain validation internally. |

Remove any hardware-provisioning/device administration and unused permissions; these are not required by the inspected phone modules.

## Intentional AI Services in Family

Family → AI Services manages named profiles (stable ID, base URL, masked key) and independent Chat/ASR profile/model bindings. Ship the authorized Token Plan key with its matching dedicated base URL, not an unrelated ordinary-API address. Preserve profiles on upgrade; replace/delete repairs affected bindings.

Discovery calls the selected profile's `/models`: loading, IDs, empty or failed. Always allow manual model IDs. Chat and ASR may use different services; each has untested/available/failed status from actual operations. Listing models does not establish ASR support. Explicit tests retain editor drafts on failure.

Unchanged key fields retain saved keys; intentional replacement stays masked. Never echo keys. Pending tasks freeze profile revision/model; changes affect new tasks. Installed system speech can be an explicit fallback; typing always remains available. Link system TTS settings when fixing/selecting a reading voice.

## One composer owns recording through sending

The mic callback fills text asynchronously; `routeCare()` rejects empty view text before coordinating capture. Send must use owned state/draft, not transient view text.

| State | Visible actions | Send behavior |
| --- | --- | --- |
| Idle | Type or Speak | Validate the actual text draft only here; no text means invite typing or speaking. |
| Recording | Elapsed time, Stop recording, Cancel, End and send | Set one pending-send intent; finish capture and begin transcription. No empty-text validation. |
| Transcribing | Recognizing, Cancel, Send when ready | Queue the same pending intent. Repeated taps cannot enqueue duplicates or start recognition again. |
| Ready | Editable recognized text, Speak again, Send | Route the assembled nonempty draft once. Stop-without-send stays here until the user sends. |
| Sending | Actual task progress, Cancel when supported | Further Send taps do nothing; results update only this task. Care-event writes still require their structured Save confirmation. |
| Error/interrupted | Specific reason, Retry, Continue recording or Type | Retain typed text and valid captured material. Retry recognition does not record again or silently submit another request. |

Own session ID, draft revision, target/actor and cancellation generation. Append recognition without overwriting edits. Pending Send authorizes one route, not an unreviewed write. Cancel invalidates pending send/late callbacks. Backgrounding releases the mic and retains valid material; recovery never auto-records/sends. Empty recognition gets a specific Retry/Type error.

No fixed 60-second stop. Stream bounded disk segments, retain order and use supported chunk recognition within verified payload limits. Do not buffer entire sessions. Actual device/storage/provider failure preserves usable material and gives an honest recovery reason. Remove recovery files only after consumption/discard; remove old duration copy.

## Use Android facilities

| Need | Phone behavior |
| --- | --- |
| Network | Use the phone's existing connectivity. Show offline/retry when relevant; open system Internet settings only on request. Never collect Wi-Fi passwords or provision hardware. |
| Microphone and notifications | Request the system permission at use time, explain denial inline, keep typing/local lists usable and link system App settings when necessary. |
| Text, dates and timezone | System IME and date/time picker; unknown birthday stays empty. Keep existing statistical timezone internally and explain day-boundary changes; no raw timezone identifier in a profile form. |
| Files/export/update | Android document picker/save sheet and share sheet; scoped files, progress/cancel and retained drafts. Export does not require a cloud account. |
| Audio | System audio focus/media session and notification/lock-screen controls. Coordinate capture with playback; never restart sound automatically after recovery. |
| Credentials/service health | Bundle the authorized Token Plan key with its matching dedicated base URL. AI Services allows named address/key profiles and independent Chat/ASR bindings. Show actual Available/Offline/Temporarily unavailable results; a stored key or model listing does not establish usable capabilities. |
| Updates | Show installed version, release summary and progress. Without a deployed channel, offer local signed-package update rather than a fake online check or a channel editor. Android performs final installation confirmation. |

Defaults remove mandatory setup; AI Services retains user control. Configured defaults do not prove capabilities. Never expose keys in evidence/logs/export. No cloud registration/invitation or hardware-administration contract is added.

## Required acceptance

1. Send during capture with an empty text box stops capture, waits for recognition and routes exactly once; a record preview appears before Save.
2. Repeated Send during transcription creates one owned task; Stop alone produces editable Ready text. Empty recognition has its own actionable error.
3. Speech longer than two minutes has no 60-second cutoff; memory/storage are bounded and actual payload limits are handled honestly.
4. Permission denial, network/storage error, Cancel, Home, recreation and late callbacks preserve the correct draft and cannot send cancelled/wrong-session input.
5. Typed edits during recognition survive. Switching caregiver/page cannot reassign an existing task or saved event.
6. Keyboard-open, 200% font and active-player layouts keep mic/Send/Cancel/navigation reachable and the conversation scrollable; check TalkBack labels and state announcements.
7. A fresh APK has the authorized matching Token Plan default without forced setup. User-managed AI profiles remain available; Wi-Fi/channel administration is absent. Unsupported ASR/live services and undeployed updates stay accurately labelled.
8. Local registration/update/archive/restore, record Undo/export, reminder denial, media focus and signed-package update retain their existing functional checks.
9. Multiple AI profiles preserve masked keys; discovery success/empty/failure and manual model IDs work. Chat/ASR bindings are independent; profile changes/cancellation cannot redirect pending requests.

Build: NOT RUN. Host tests: NOT RUN. Device tests: NOT RUN. Recording transitions, live services and redesigned native flows still require implementation/phone acceptance.
