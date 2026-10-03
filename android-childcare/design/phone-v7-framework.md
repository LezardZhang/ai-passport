<p align="right"><a href="phone-v7-framework.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Phone v7: daily care framework

Design specification, 2026-10-03. This revision supersedes the navigation, visual hierarchy, settings placement and export recommendations in [revision 3](android-phone-design.md), [simplified design](simplified-design.md) and historical [native reviews](native-ui-review.md). It specifies implementation targets; it does not certify a running APK. Preserve the existing speech-session ownership and record-confirmation behavior.

## Architecture

This is one private family with exactly one baby. Do not add a baby selector, household selector, multiple-child onboarding or profile-switching flow. Caregiver switching changes attribution only.

Keep three bottom roots: Care, Records, Family. A 48 dp gear target in each root toolbar opens Settings as its own route. Family contains only the baby and caregivers. The Settings route has a back toolbar and no selected Family tab. Secondary routes use a real parent/back stack; Back from an audio detail returns to the library, and Back from the library returns to the same Home position. Switching a root restores its own position. Opening a settings child returns to Settings. Avoid treating every Back as Home.

| Route | Contents and direct actions |
| --- | --- |
| Care | Today's facts, active sleep, quick recording, upcoming reminders, sounds/stories, latest response preview and recent events |
| Records | Date selector, same-day totals, chronological events, add/backfill, event detail/edit/delete/undo; handoff summary action |
| Family | Compact baby identity/birthday/age, profile edit, current caregiver and caregiver list; person detail owns switch/edit/deactivate/restore |
| Settings | AI services, Sync, Backup and export, Notifications, Version and updates; concise grouped rows |
| Audio library | Category chips, actual available titles, local/cloud availability, play, import; full player owns seek/timer when supported |
| Reminders | Upcoming/overdue list, add, detail and complete; notification status only when relevant |
| Conversation | Owned request history and complete answers; opened from Home latest-response preview; composer in this route too |
| Handoff | Recent care summary, editable note, save and system share; reached from Records toolbar |

AI configuration never becomes a bottom root. There are no hardware/network provisioning pages, night-mode switch, user-editable timezone field, backup URL editor or backup credential editor.

## Home composition

Use a quiet warm canvas, one coherent vertical reading order and grouped rows. At ordinary font scale, a 360 dp-wide content viewport should expose reminders and the audio-section heading before a long scroll. Large text may scroll; do not shrink controls or truncate facts to force this target.

1. **Toolbar, 56 dp:** baby name/age on the left (20 sp name, 12 sp date beneath), settings gear on the right. Missing age simply omits age. No slogan, large avatar or separate registration card.
2. **Active sleep, only when present, about 72 dp:** moon icon, elapsed time and start time, trailing End action. Dark forest surface makes the running state distinct; no 44 sp hero timer. Elapsed time derives from persisted start time.
3. **Today, about 64 dp:** one shared surface containing feeding total/count, completed sleep and diaper count. 22 sp figures, 12 sp labels. Unknown volume remains unknown. Running sleep is not silently counted as completed sleep. A 12 sp local/pending status line appears below only when useful.
4. **Quick record, 48–56 dp:** Feed, Sleep, Diaper, More; icon plus short label, with equal widths. More opens only remaining event kinds. It must not hide reminders/audio/settings. At large text, use a two-column wrapping arrangement.
5. **Reminders:** 18 sp section name and trailing All action, both on one 40 dp-minimum header. Show up to two upcoming/overdue reminders as 56–64 dp rows with time, task and Complete action. Empty state is one row with Add reminder. No empty decorative panel or permission warning on every launch.
6. **Sounds and stories:** same section header with Library action; 48 dp category targets for white noise, lullaby and stories; one or two real playable titles in 56 dp rows. A locally available rain track can be offered only if actually available. No fake album covers or fabricated catalog. Cloud failure retains local titles and one retry row.
7. **Latest reply:** only when a request exists; prompt/status and at most three answer lines, Open conversation action. Pending/failed work stays recoverable here. Long answers and five repeated response cards must not push daily care offscreen.
8. **Recent care:** latest three events with time, kind, quantity/detail and caregiver; All records action. Missing data gets an honest compact empty row.

Setup gaps use one 44–48 dp dismissible inline row near the affected section, never a dominant home panel. Updates use a dot on Settings and a settings-row badge. An urgent actionable sync conflict gets a compact status row linking to its detail; routine healthy sync does not occupy a card.

### 360 x 800 dp viewport budget

With a representative 24 dp status inset and 24 dp gesture inset, usable app height is 752 dp. A 56 dp toolbar, 64 dp idle composer and 56 dp bottom navigation leave **576 dp of visible scroll content**. Actual insets must be measured rather than hardcoded. A 56 dp active player leaves 520 dp. Use the following compact Home arrangement, in order, within that budget:

| Content | Height including adjacent gap |
| --- | --- |
| Active sleep, when present | 72 + 8 = 80 dp |
| Today totals | 64 + 8 = 72 dp |
| Quick record | 48 + 12 = 60 dp |
| Reminder header + first reminder | 40 + 56 + 12 = 108 dp |
| Audio header + categories + first playable row | 40 + 48 + 56 = 144 dp |
| Total with active sleep | **464 dp** |

Thus the first actual reminder and first actual audio row fit even with active sleep and playback (520 dp available). This is the default compact preview count on a 360 x 800 dp phone: **one reminder and one audio row**, with All/Library opening complete lists. Show a second reminder/audio row only when remaining measured height permits; never prioritize a second reminder over visibility of the first audio row. If a necessary status line adds 20 dp, the total is 484 dp. Extra top/bottom content padding must fit the remaining 36 dp in the player case. Latest reply and recent care follow below the fold. When there is no active sleep, the spare 80 dp can expose another row. At 200% font or with IME, these are minimum heights rather than fixed clipping boxes; scrolling takes precedence over the above-the-fold target.

## Bottom area and speech

Use a vertical native shell: fixed toolbar, weighted scroll content, optional playback strip, contextual composer, bottom navigation, with system insets applied once. Care and Conversation have the composer; Records, Family and Settings do not. Idle composer is one 56–64 dp row: 48 dp mic target, flexible text field, 48 dp Send target. Show a short input hint; a three-line hint, permanent explanation, two full-width buttons and hidden-control margins are prohibited. Empty Send is disabled with an accessible explanation. Text expands to at most three lines before inner scrolling.

Active recording or recognition can add one compact status row and a Cancel target; Error adds Retry only when valid audio exists. Entire unused status/action containers become `GONE`. A pending Send must remain possible during capture even with no text: it stops capture, waits and routes once. Stop alone leaves an editable transcript. Keep `SpeechSession` ownership, cancellation generations and duplicate-send protection. Draft recovery never auto-sends. Permission denial retains typed text and exposes a clear recovery action. Background capture cleanup follows the existing verified policy; do not claim raw audio survives unless retained material exists.

During keyboard use, `ADJUST_RESIZE` keeps composer above the IME. Hide the bottom navigation while IME is visible to return space to content. Back first dismisses the IME. At 200% text, allow a taller input and separate compact status row, keeping 48 dp mic/send/cancel targets; never stack multiple large action panels. If extremely short height prevents useful content, the focused composer can become a dedicated input sheet.

Playing audio adds one 52–56 dp row with title, Pause/Resume and Stop, each control at least 48 dp. No strip when idle. Library/full-player screens show their own controls without duplicating the global strip. Recording coordinates audio focus; returning to a screen must not restart sound. Audio unavailable, buffering, paused, stopped and failed are different states.

## Visual system

| Token | Value and use |
| --- | --- |
| Canvas | `#F7F5EF`, warm neutral background |
| Surface | `#FFFFFF`, one shared group surface rather than a card for every sentence |
| Main text | `#233B32` |
| Secondary text | `#667267`; verify contrast in rendered implementation |
| Primary | `#285747`, important actions and selected navigation |
| Soft green | `#E9F0E9`, quiet selected chips and supporting state |
| Peach | `#F4DDCB`, occasional category accent, never every other panel |
| Divider | `#E6E9DF`, 1 dp group separators |
| Error | `#9D3027`, with explicit text/icon, never color alone |
| Type | System sans-serif; title 22 sp, section 18 sp, body/control 16 sp, metadata 12–13 sp; medium weight only for hierarchy |
| Rhythm | 16 dp outer gutter, 12 dp group padding, 8 dp row gap, 20 dp section gap; 12–16 dp corner radius |
| Icons | Consistent 22–24 dp stroked vectors/Canvas paths; 48 dp target; no emoji as navigation glyphs |

No photo assets are needed. A small leaf/seed vector can accompany baby identity but must not add height. Avoid shadows, gradients, oversized pills and nested rounded cards. Native `LinearLayout`, `ScrollView`, `TextView`, `EditText`, `GradientDrawable`, `RippleDrawable` and small custom icon Views are sufficient. Set semantic button roles/clickability/focus, meaningful descriptions and selected state. Group rows are preferable to repeatedly calling the existing 20 dp-padded, 24 dp-radius `Ui.card()` helper.

## Family and cloud identity

Baby row shows authoritative cloud birthday and derived age after a successful profile fetch, using the existing baby ID. On launch, show the locally cached profile immediately and automatically synchronize the existing server baby profile and household care data through the built-in owner service at `162.14.108.234`. This is the default behavior, not an optional connection wizard; use the single existing baby identity and household context. The deployed transport, route and authentication come from application configuration; the IP alone is not an invented API contract. Fetch failure preserves cached birthday and shows stale/offline status only in profile detail. Missing birthday remains unset. Never force duplicate registration because an old local registration flag is false. A genuinely missing profile can be edited without duplicating existing event ownership.

Caregiver registration is one sentence, “I am the baby’s ___”, with one free-text title such as Dad, Grandpa or Grandma. Do not request a separate name or contact number. The list uses that title, a Current badge or chevron; new care records carry the same title to the cloud. Existing records keep their historical snapshot. Tapping opens detail; avoid three full-width buttons on every list item. Changing the current caregiver affects future records only. Historical identity survives deactivation. Keep local-only caregiver status explicit where the server does not support it. No appearance, AI, backup, updates or timezone fields appear here.

## Settings and independent capabilities

Settings is a grouped list of 56–64 dp rows, each with a concise current state. AI Services opens five capability rows: Text reasoning, Speech recognition (ASR), Speech synthesis (TTS), Vision and JEV judgment (`judge`). Each row shows its independently selected service/model and health; no giant editor on the overview.

Each capability detail owns service name, endpoint, masked key, model ID and supported adapter/protocol. Allow named saved configurations, manual model IDs and optional model discovery from that selected service. Selecting or editing one role must not overwrite another role's endpoint/key/model. Reusing another configuration is explicit. Save persists configuration; Test reports actual capability behavior and cannot infer ASR/TTS/Vision from successful text reasoning. Model listing is not a capability test. Preserve editor drafts through errors and invalidate stale test/model-list results when the configuration revision changes. Existing in-flight requests retain immutable configuration snapshots.

The owner clarified the earlier GEV label as JEV judgment: the independent `judge` role handles judgment/routing, with its own endpoint, key and model. Keep tested deterministic intent rules when no judgment model is selected. A configured judge is used only through an implemented compatible adapter; saving its fields does not prove that adapter or its capability works. Model classification must still produce a validated typed intent, clarify ambiguity and preserve explicit care-write confirmation. Do not invent video or Gemini functionality from this label. TTS may offer Android system speech as an explicitly selected alternative; its settings and availability remain separate from remote TTS configuration. Vision may be configured before a real image-input flow exists; show that limitation clearly rather than a fake working camera button.

Sync is bound to the fixed owner backend with automatic normal operation, last successful sync, pending count, manual Retry and conflict detail. Errors say what can be retried and retain local records. Remove URL/key setup forms, including first launch. Server identity/generation checks remain internal correctness controls. Do not display Connected merely because bundled configuration exists.

Backup and export provides **Export backup (.zip)** or an actual supported `.bak` container. The save action uses Android's document picker. Produce a real archive with manifest/schema/app version/time, consistent record/profile/reminder snapshots and restore-relevant metadata; include an explicit media inventory/exclusion list. CSV may be an internal archive member, never the only artifact renamed `.zip`/`.bak`. Include file integrity checks; exclude secrets. Show Preparing, Writing, Complete (filename/size) or Failed; failure does not claim a usable backup. Do not offer Restore as working unless validation, preview and transactional restoration exist. An export-only package must state that restore is not yet available.

Version and updates uses the independent `/android-updates/` manifest and APK service. During migration it retries the legacy `/cloud-backup/android-childcare-updates/` manifest/download, records which source succeeded, and keeps the same signer/hash/install checks. It shows installed version, last check, available release notes, progress and verified Install action with system installer confirmation. Technical hashes/signers/channel addresses stay in diagnostics. Check failure keeps previous information with its date; absence of a channel never means Latest.

## Implementation and acceptance

`MainActivity` owns route/parent state and root scaffolding; `Ui` gains compact toolbar/group-row/chip/icon-action primitives; `CareComposer` changes presentation while retaining its state machine; `FamilyUi` only owns people; `AiSettings` organizes capability-first detail routes; `UpdatesUi` uses compact rows. Avoid rebuilding an active EditText or microphone session on every observer/timer tick. Preserve field focus, selection, draft and scroll through background refresh. Status belongs to its operation/request ID.

Required visual checks: 360 dp and 411 dp widths, empty/populated Home, active sleep, reminder overdue, local audio/cloud error, live player plus composer, keyboard open, 200% font, all settings roles and family lists. Verify no fixed bar covers scroll content and no action falls beneath system gestures. Required behavioral checks: root/back history; exactly-once capture-to-send; stop/cancel/late result; independent five-role configuration; cloud birthday without duplicate registration; failure-preserving sync; real archive contents/integrity; update error/retry. Screenshot evidence must be from the native app; prototype evidence cannot certify Android behavior.

Build: NOT RUN (design-only work).
Host tests: NOT RUN (design-only work).
Device tests: NOT RUN.
Unverified: implementation, native rendering, TalkBack, actual backend profile contract, archive restoration, live AI protocols and phone capture/playback.
