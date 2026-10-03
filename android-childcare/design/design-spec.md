<p align="right"><a href="design-spec.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Xigua Childcare: Warm Care mobile design

Current Android phone behavior and service profiles: [revision 3 phone design](android-phone-design.md). Its recording/send ownership and absence of a fixed recording-duration limit take precedence over earlier interaction examples.


Current navigation: [revision 2 simplified design](simplified-design.md), 2026-10-03. The capability specifications below remain applicable, but Q&A, Story, playback, reminders and handoff now route through one Care conversation. The earlier four-root page map and screenshots describe revision 1, not the current target.

Date: 2026-10-02, Asia/Shanghai. Scope: the native Android childcare app, one household and one child. This is the visual and interaction handoff for the implementation described in [application architecture](../docs/architecture.md). All design files stay in this directory; firmware, BSP and backend remain untouched.

## Deliverables and design intent

Open [prototype.html](prototype.html) directly or serve this directory and open `http://127.0.0.1:8096/prototype.html`. The prototype is plain HTML/CSS/JavaScript, designed at 390 × 844 and responsive on phones. It is a review artifact, not the Android runtime. No WebView is required for the APK.

[design-tokens.json](design-tokens.json) contains colors, spacing, radii, typography, component sizes and the agreed resource bounds. screenshots (local artifact: `screenshots/`) contain actual browser captures. [prototype.js](prototype.js) uses sample records and simulated voice, AI and media states. It does not call the microphone, backend, notification system or player. The sidebar says this explicitly. Controls expose empty-profile, night, large-type, offline, denied-microphone and failed-request states.

The interface helps a parent holding a baby answer three questions quickly: what is happening now, what did we last do, and what needs to happen next? Warm cream, forest green and restrained peach/lavender provide a calm, adult visual language. Clear numbers and generous grouping carry the design; decorative illustrations are unnecessary. The phone UI is newly designed, with no board test menu, firmware soft keyboard or enlarged hardware screen.

First APK launch creates only the default child identity/nickname. It shows zero records, no active sleep, an empty reminder list and no invented birthday, age, handoff note or AI conversation. A missing birthday displays “Birthday not set.” The prototype's sample birthday and history are review data only.

## Navigation and information structure

| Root destination | Content | Secondary screens |
| --- | --- | --- |
| Today | Current sleep state, quick feeding/sleep/diaper, voice record, today's summary, handoff shortcut | Sleep, Handoff, Reminders |
| Records | Day/range selector, provisional totals, bounded timeline, record filters and add | Record details/editor, extra care, Export, deleted-record recovery |
| Companion | Three clear choices for different tasks | Q&A, Story, Audio library, Player |
| My | Default child profile, service/sync, permissions, appearance, storage, export | Profile, Sync queue/conflicts, connection settings, cache, optional devices |

Root selection remains stable during normal use. Android Back closes the current sheet first, then returns from a secondary screen to its root, then applies normal root-back behavior. Switching roots does not end sleep or playback and does not delete a draft. A full player screen and a persistent mini-player share one playback state. The mini-player sits above bottom navigation and never covers its controls or the last content row.

Handoff and reminders belong near the household's daily care, not behind technical configuration. Device integration lives in optional device settings. Phone connectivity replaces firmware Wi-Fi setup; board tests are omitted.

## Screen and flow specification

### Today

Header: greeting, local day/date, nickname and known age, profile button. Sync label opens the queue and reads one of “Saved on phone / N pending,” “Offline, records still save,” “Syncing,” “Updated at …,” or “Not connected.” Network connectivity is not proof that the backend is working.

An ongoing sleep takes priority in a forest-green hero. It shows elapsed time, start date/time and one primary “Baby is awake” action. Ending asks for a short confirmation and persists one interval. With no active sleep, the hero invites starting sleep and confirms that leaving the screen continues timing. Empty state uses “Every little step is worth recording” and does not simulate past events.

Three equal quick actions open feeding, sleep and diaper. The next row is “Say it, record it,” with an example phrase. Voice records always pass through a structured preview before saving. Summary displays bottle-fed volume, completed sleep and diaper events. Active sleep remains separate. Missing volume is not zero. Do not sum diaper subcategories as event count when a combined wet/dirty event represents one change.

Handoff is a compact supporting row, followed by upcoming reminders when any exist. The content is scrollable; essential recording controls remain near the top and bottom navigation remains reachable. Do not shrink type to force the entire day into one viewport.

### Feeding and extra care

Feeding opens a bottom sheet with volume, ±10 ml, recent safe presets, feeding kind, occurrence date/time, optional note and Save. Numeric entry uses the system numeric keyboard. Text notes use the installed system IME. Breastfeeding should use duration/side if supported, without inventing bottle volume; unsupported modes are omitted until implemented. Keep last-used volume as a convenience, not an automatic record.

Save uses `editing → saving → saved` with a separate sync state. Only a completed local transaction produces “Saved on phone.” A failure preserves all input and offers Retry. A success toast supplies Undo for seven seconds and history remains editable afterward. Undo uses the exact event identity and a compensating operation; it cannot undo a different record.

Date/time entry uses native controls and shows the local zone. Retrospective entry is first-class. Reject invalid/future occurrence times and invalid amounts with an inline message rather than silently clamping. Existing values outside the normal range can be retained for manual review. Bath and tummy-time use the same form structure with only their relevant fields; a timer is a separate duration tool, not a board test.

### Diaper

Choose wet, dirty or both (only when supported by the data model), occurrence time and optional notes. Color/consistency are optional descriptive fields. One Save makes one event; a combined event increments the corresponding category flags without doubling the event count. Notes are secondary and do not block a quick record.

### Sleep

The dedicated sleep page displays a large elapsed timer and start date/time. Start persists immediately. Leaving the page, locking the phone, backgrounding or process loss does not create a completed sleep event. After process loss, reconstruct elapsed time from persisted start time; do not restore a guessed monotonic origin. An impossible phone-clock change produces a check-time state.

End saves exact start/end and computed duration. A separate retrospective form supports cross-midnight intervals. Validate end after start, future times and overlaps according to the contract. Completed rows show an interval, not only a duration. Local-day statistics use interval overlap with the requested zone and range. Show the active interval separately rather than blending it into confirmed completed sleep.

Sleep sounds are supporting shortcuts to the same audio library and player. Sleep timing and audio timer are different persistent states.

### Records, history and statistics

Provide Today / last seven days / month, date navigation and explicit date range. Filters cover all, feeding, sleep and diaper, with extra types discoverable. A row shows occurrence time, type/quantity, relevant details and sync status. Tapping opens full details with edit/delete. Deletion creates a tombstone and offers immediate restore; recent deletion recovery must remain available after the toast expires when implemented.

The UI observes one durable local data source. List rows and summaries must update from the same committed state. No optimistic counter changes before persistence. Bound visible rows to 100 and page older records; do not discard history to satisfy a display limit.

The statistics key is child identity, data revision, timezone and half-open time range. A backend-confirmed total is labelled with its revision and update time. Local provisional totals may include outbox changes, and this difference is visible. Never add a local total to a server total. Unknown-time records appear in a separate group. Cross-midnight sleep allocates only its overlap to each day. APK/web comparisons must use identical revision, zone, range and interpretation of missing values.

### Voice input and recording preview

| State | Visible information | Available actions |
| --- | --- | --- |
| Ready | Why microphone is needed; “tap to start” | Start, type instead, close |
| Permission request | Native Android permission | Allow/deny in the system dialog |
| Denied | Recording unavailable; typing still works | Type, system settings when permanently denied |
| Recognizer unavailable | This phone/service cannot currently recognize speech | Type, retry when available |
| Recording | Clear mic indicator and elapsed time; user controls stopping | Stop and recognize, Cancel |
| Recognizing | Progress and original mode | Cancel; no extra capture |
| Transcript | Editable recognized text; explicitly not sent or saved | Correct using IME, rerecord, use text, cancel |
| Interrupted | Capture stopped on background, navigation or call | Use existing transcript if available, record again, type |
| Failure | Specific permission/network/recognition reason | Retry or type; retain available draft |

The visible input screen owns capture. Start pauses playback and verifies service/permission availability. Close, background and incoming interruptions release microphone/recognizer resources. A transcript may survive; do not claim raw audio is retained unless it actually is. A cancelled capture never sends, stores a care event or starts an AI request. Returning does not automatically resume recording or playback.

“Use text” inserts the transcript into the owning Q&A or Story editor. It does not send. Voice record mode shows structured event/time/quantity and asks Confirm save. Unsupported or ambiguous recognized commands remain text and ask for correction. AI prose never directly mutates records, deletes data or makes medical decisions.

### Q&A and Story

Both have their own draft, conversation/request identity, backend job identity, status, result and playback ownership. Do not share a single mutable “last reply” across modes. Persist these identities before network work. Cancellation invalidates the request owner. A late Q&A result can update its Q&A record, never the Story editor or player; the converse also holds. Persist interrupted status after process loss and offer a deliberate retry/resume according to actual backend capabilities.

Q&A provides a native text composer and large voice action. Suggested prompts only fill the composer. Send displays the final text, the selected care-record context/range when used, Processing and Cancel. Failures preserve the prompt and expose Retry and Edit. Retrying must preserve idempotent job identity where the contract requires it. Empty prompts are not sent. Prior successful answers remain accessible if a new attempt fails.

Story additionally offers duration and tone only when the service supports those choices. Text can become readable before playable speech is ready. Reading audio can fail independently; preserve the text and offer Retry audio. A Play/Read action begins speech deliberately. A new story cancels old generation/playback ownership and gets a new request identity. Saving a story means persistent text and media metadata, not a toast claiming persistence that did not occur.

Loading, failed, cancelled and interrupted states belong to each request. Background completion can show a small “Your answer/story is ready” affordance returning to the correct result. Backend DRAFT or unavailable capabilities must not be portrayed as a working online AI service. Demo/local fallback, if any, is labelled as such.

### Audio library and player

Library categories come from service capabilities. Render valid categories independently; a failure in one category does not blank the entire library. Search, paged rows, duration, available offline status and download progress are clear. Local imported audio and offline ambience use the same player. Import limit is 128 MiB/item, streaming is incremental and the playback cache cap is 256 MiB. Cache controls identify downloaded content separately from disposable streaming cache. Clearing cache preserves care records, text, favorites and active valid references.

One player owns one selected item. Pause preserves position, Continue resumes, Stop releases resources and selecting another item performs a clean transition. Cross-page mini-player shows title, current state, progress, pause/resume and stop. The full player adds seek, queue/library selection and optional sleep timer. Buttons have clear semantic descriptions, including “Pause,” “Resume,” “Stop” and “Forward/back 15 seconds.”

Actual implementation uses native audio focus, a media session and the appropriate foreground playback service/notification. Calls, headphone removal and competing audio produce visible paused state. Capture pauses the player. Restart after process death never autoplays. Foreground-notification permission and media-service failure must surface without claiming permanent background operation. Exported/design media states are not sound-quality validation; real phone playback must test volume, intelligibility, gaps, seek and interruptions.

### Handoff, reminders, export and settings

Handoff groups latest feeding, active/latest sleep and diaper, each with actual timestamps and status. A durable parent note is editable with system IME and voice where supported. Missing entries say “No record”; they do not prove an action did not happen. The local/cloud freshness distinction remains visible. Read aloud uses normal playback. Share opens a preview then the Android share sheet, leaving the recipient to the user.

Reminders persist title, due time, zone and completion. The management sheet offers Done, Remind in ten minutes, Edit and Delete. Notification permission and scheduling reliability are explicit. A denied permission does not delete the reminder or report that a notification will fire. Reboot must rebuild scheduled notifications. Avoid automatic recommended feeding/medical schedules.

CSV export is the required first implementation; stream it to a user-selected document or share target. The design also shows JSON as an optional format: omit/disable it if not implemented. Select date range and local-all versus backend-confirmed data. Preview format, row count, timezone and pending count. Include sync metadata; exclude tokens and unrelated adult data. Large history export never becomes one unbounded in-memory string.

Settings organizes child profile, service connection, sync queue, export, night/large type, notifications, microphone and downloads/cache. No register/login wizard is needed for a default local child. Service setup uses a single household URL/credential configuration when required, masked credential entry and actual connection/capability result. Do not prefill invented credentials or assume a link works. Optional device settings cannot imply implemented BLE/NFC support. A clear/reset flow, if included, must explain generation changes and stale offline operations before deletion.

## Component and visual specification

| Component | Size/spacing | Behavior |
| --- | --- | --- |
| Page | 20 dp horizontal; 24 dp section gap | System insets respected; content scrolls |
| Primary action | At least 50 dp height; 16 dp radius | One main action per context; disabled/loading labels |
| Touch target | At least 48 × 48 dp | Small visual icons retain large hit areas |
| Grouped list | 20 dp radius; 12 dp vertical row padding | Subtle dividers, no nested cards |
| Row | At least 70 dp, flexible height | Type/detail/status wrap rather than truncate essential meaning |
| Bottom navigation | At least 64 dp plus system bottom inset | Four stable labels and selected state |
| Mini-player | At least 64 dp above navigation | Content gets matching bottom space |
| Bottom sheet | 26 dp top radius; native keyboard inset | Scrollable, Cancel/Close accessible, Back closes first |
| Input | At least 48 dp; 13 dp radius | Native IME/date/time, errors next to field |
| Typography | Title 24 sp, section 16 sp, body 16 sp, row/button 14 sp, secondary 12 sp | System sans/CJK fallback; every text dimension uses sp |
| Timing | 180 ms transition, 120 ms fade; Undo seven seconds | Respect reduced motion, no essential animation |

Day colors: cream `#FAF8F3`, white `#FFFFFF`, primary `#214E43`, text `#233B33`, secondary `#667267`, divider `#E7E9E1`, peach `#F4D9C8`, sage `#E8EEE4`, lavender `#E8E6F1`. Night colors and full roles are in the token file. Default contrast: primary/cream 8.85:1, body/cream 11.34:1, secondary/cream 4.75:1. Night primary/background is 9.69:1 and secondary/surface 6.29:1. These are calculated token-pair ratios; inspect all actual state combinations separately.

Night mode uses a deep-green base and soft-green primary controls, with preserved readable text. It does not lower contrast just to reduce brightness. Large type respects system font scaling; the prototype adds 130% for review. At large sizes, quick actions and summary columns become stacked rows, labels wrap, sheets scroll and buttons grow. Do not fix text containers to single-line pixel heights. Test 200% system font size, TalkBack, keyboard focus, contrast and Android edge-to-edge insets on the actual app.

Prototype icons are Lucide 0.468.0, bundled locally under vendor (local artifact: `vendor/`) with its [ISC license](vendor/LUCIDE-LICENSE.txt). Native vectors may use a consistent equivalent set. Do not substitute emoji for core controls. Provide content descriptions and selected/checked state; color alone never communicates pending, failure or selection.

## Key copy and implementation boundary

The Chinese production strings are listed in the corresponding section of the [Simplified Chinese specification](design-spec.zh_CN.md). The table below gives their English equivalents.

| Meaning | English wording |
| --- | --- |
| Durable save | Saved on phone · Pending sync |
| Offline | Offline · Records still save |
| Empty | No records yet · Start today |
| Active sleep | Baby is sleeping · Timer continues |
| Capture | Listening |
| Transcript | Transcript · Editable · Not sent |
| Permission | Microphone unavailable · Typing still works |
| Interrupted | Recording stopped · Review draft or record again |
| AI failure | Request did not complete · Prompt retained |
| Conflict | Changed elsewhere · Choose a version |
| Cache cleanup | Clear playback cache · Keep records and favorites |

The prototype demonstrates intended controls and states. System settings/share sheets, real ASR, AI, imports, notifications, audio and exports remain implementation responsibilities. Some secondary demo controls show a clearly labelled handoff toast instead of performing an OS action. Real Android delivery must separately report Build, Host tests, Device tests and Unverified checks. Browser visual/interaction checks do not establish phone microphone, background playback or backend availability.
