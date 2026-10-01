**English** · [简体中文](xigua-ui-design.zh_CN.md)

# Xigua Assistant UI and three-button interaction design v1.0

This document defines the first Xigua Assistant interaction model for the
FoloToy AI Passport. It is the interaction baseline for a future `main/xigua/`
implementation. A capability described here is only available after its local
storage, audio, networking, or voice module is implemented and validated.

## 1. Design goals

The device has a 240×320 portrait display, three resistor-ladder buttons, 8 MB
Flash, an ESP32-C3, and no PSRAM. The interface must let a parent record one
event with one hand while keeping the meaning of the next press obvious.

- Local records work without Wi-Fi, cloud services, or voice.
- Each page exposes one primary action, so the three buttons do not change
  meaning unexpectedly.
- A write shows `Saving` until persistence succeeds; only then is it reported as
  recorded.
- Every new record keeps a five-second undo action bound to its event ID.
- The top bar always shows time confidence, network state, and battery. An
  unavailable battery is `--`; charging is never guessed.

## 2. Page map

Boot goes directly to Overview instead of the hardware-test menu. Navigation is
single-level and uses no hidden swipe or touch gesture. The password keyboard explicitly shows double UP/DOWN as row navigation.

| Page | Purpose | Entry | Exit |
| --- | --- | --- | --- |
| Overview | AI assistant and Story first, then manual record, Today, Sleep, Sound, Wi-Fi, and Settings | Boot; long DOWN from a top-level page | OK opens focus; UP/DOWN changes focus |
| Feeding | Record time, amount, and ingredient/notes | OK on Feeding focus | OK edits; long DOWN back |
| Diaper | Record pee/poop and occurrence time | OK on Diaper focus | UP/DOWN choose; OK saves |
| Sleep | Background sleep session and duration | Overview Start sleep / End sleep card | OK toggles the session without entering a timer page |
| AI assistant | Voice Q&A and JSON records | OK on the first Overview focus | Hold OK to talk; long DOWN back |
| Story | Dedicated voice story mode; a completed reply is spoken automatically | OK on the second Overview focus | Hold OK to talk; long DOWN back |
| Songs/noise | Browse cloud tracks, pause, resume, stop, or refresh | OK on Songs, the second home entry | UP/DOWN choose; OK activates the selected track/control |
| Today | Paginated counts and recent records | OK on Today focus | Long DOWN back; UP/DOWN pages |
| Settings | Display, sound, Wi-Fi, time, AI, device info, restart | OK on Settings focus | Long DOWN back; destructive actions confirm |
| Wi-Fi/time/AI | Show state and test results; auto-connect known networks, scan/select an SSID, and enter its password | Settings item | UP/DOWN choose; OK advances; long OK completes text; long DOWN back |
| Result/error bar | Show saving, success, failure, retry, and undo | After any write or test | Timeout returns; OK undoes or retries |

Only the current page, its snapshot, and short strings stay allocated. Producers,
timers, and callbacks stop before their page objects are deleted.

## 3. Overview layout

The 240×320 display has four fixed areas:

1. A persistent time/network/battery row, followed by the page title.
2. A 204×190 px content area with a two-line milk/sleep/diaper summary and up
   to three 38 px menu cards, separated by 6 px gaps.
3. A feedback line, showing the menu page number when no action feedback exists.
4. A fixed two-line key hint: UP/DOWN selects, OK opens, hold UP records feeding.

The focus order is AI assistant → Story → Manual record → Today → Sleep → Sound →
Wi-Fi → Settings. The three menu pages contain 3, 3, and 2 items; UP/DOWN wraps
between the first and last items. Selection moves on PRESS, so rapid repeated
presses continue moving even when the driver subsequently reports DOUBLE.
Menu cards share the AI preparation/action colors: dark blue background, cyan
selection with dark text and a white outline. Cards exist only on the Overview
page. The short undo window takes priority over opening ordinary menu items;
the Sleep card always performs its displayed Start/End action.
The AI assistant and Story are separate top-level entries. The model can call local
records through JSON; ordinary AI replies stay on screen, while Story replies are
spoken automatically for the parent or child.
Audio and network changes
cannot silently move focus or change what OK means.

## 4. Unified button rules

The hardware is a three-range ADC ladder on GPIO0: UP is about 0 mV, DOWN about
300 mV, and OK about 595 mV. Application callbacks enqueue events; they never
touch LVGL.

| Gesture | Browse page | Edit/select page | Confirm/error page |
| --- | --- | --- | --- |
| UP click | Previous card/item | −10 or previous option | Previous option |
| DOWN click | Next card/item | +10 or next option | Next option |
| OK click | Open focus or run primary action | Save/start/play | Confirm highlighted option; undo/retry in result bar |
| UP long | Open feeding with last amount | Repeat −10 | Ignored |
| DOWN long | Go back; top level returns to Overview | Cancel without saving | Cancel and return |
| OK long | Hold-to-talk on Ask/Story; voice entry elsewhere | Release submits recording; no implicit save | Disabled to prevent accidental recording |

Long UP opens the feeding editor; it never commits a record. Long DOWN cancels
the current editor. PTT requires a BSP release event; LONG alone cannot be
treated as recording completion.

## 5. Page behavior

The feeding editor shows the amount in a large font and repeats
`▲ -10 ml  ▼ +10 ml  ● Save` at the bottom. Saving captures the exact event
timestamp in the same local event ring used by voice records. The confirmation
shows the recorded date and time, and Today pages show the newest feeding
records with time, amount, and ingredient. Time is taken from the trusted clock;
ingredients start as fixed choices and free notes arrive through voice. Values
outside 10–400 ml require manual review instead of silent clamping.

The diaper page has only Pee and Poop. It writes a local event immediately; voice
can add colour, consistency, or notes later. Sleep is a background session:
the Overview card changes between Start sleep and End sleep. Leaving Overview,
feeding, voice requests, and the separate timer do not interrupt it. Ending
stores one event with the exact start timestamp, exact end timestamp, and
calculated duration. The active card shows the start date and time; Today uses
UP/DOWN pages for the active session and the newest completed sleep records.
Within one boot the
duration uses monotonic time; after reboot it uses trusted timestamps, otherwise
reports duration uncalibrated. Bath and tummy time remain one-shot events.

The existing persisted end timestamp uses `-1` to mark a running sleep; the
original state blob remains unchanged. Exact start/end pairs are kept in a
separate versioned NVS blob keyed to the existing event ring, so rollback can
still read the original records. Older active-sleep state migrates on load;
older firmware does not recognize this extra timing blob or the background
session marker when rolling back. Records created before this change show an
unknown start time unless the latest legacy session can be safely backfilled.

Saving enters a short `Saving` state. Success shows `Recorded 150 ml` and a
five-second undo. Failure shows retry and back; the authoritative counters are
not updated early.

The AI assistant handles Q&A and records, while the separate Story page handles
story prompts through the same PTT, ASR, model, and TTS building blocks. The model
must return either a structured command or a parent-facing reply.

The implemented AI UI separates preparation, recording, processing, reply reading,
reply actions, and failure. Preparation has three highlighted cards: hold OK to
speak, view the last reply, and return. Short OK never sends a canned prompt or
starts recording. Only holding OK on the preparation recording card starts PTT;
release ends capture. Processing ignores OK and retains the last successful reply.
The reply fills a dedicated clipped viewport using the complete 20 px font.
UP/DOWN press changes one page of five 38 px lines; focus, clicks, and double-clicks
cannot send another request. OK opens reply actions with Continue selected by
default; Ask again returns to preparation and still requires a deliberate hold.
Story text appears before automatic speech starts. OK opens four reply actions:
Pause reading / Resume reading (or Start reading when idle), Read from beginning,
Continue viewing, and Another story. Pause retains the exact next PCM block; resume
continues that audio without generating another story or making another TTS request.
Read from beginning resets the audio cursor. UP/DOWN still pages through text during
playback and pause; long DOWN from the text view cancels the current playback and
returns home (from actions, it returns to the text view). Another story cancels the
old playback and returns to preparation. Speech failure preserves text for retry.
The four story action cards fit within the 190 px body viewport.
Stories request 500–700 Chinese characters with a complete ending. The SSE decoder
handles large base64 strings incrementally using a 1 KiB PCM buffer. A 31-tap low-pass
filter converts the service's 24 kHz PCM to 12 kHz without changing duration or pitch,
and IMA ADPCM compresses each independent block to approximately one quarter of PCM.
The existing 2 MiB `voice_tmp` scratch partition stores these blocks instead of a
21 KiB RAM queue. It holds roughly 5.7 minutes of speech; overflow fails explicitly
rather than overwriting unread audio. Erase it before playback, write complete blocks,
and publish them only after successful writes. Flash I/O stays outside critical
sections; I2S interrupts stay in IRAM during cache writes.
A separate PCM worker starts after three seconds are available (72,000 decoded PCM
bytes at 12 kHz); shorter completed clips drain immediately. Pausing drains the
accepted DMA samples, suspends the codec, and preserves the next block. Reception
continues into Flash while paused, so a full RAM queue cannot stall the server stream.
Resume wakes the codec and reads from that cursor. After receiving the entire clip,
the HTTP connection closes even while the speaker remains paused. Buffer underruns
re-enter buffering. Logs record pause/resume offsets, underruns, PCM feed gaps and
stack headroom; intentional pauses are excluded from the feed-gap measurement.
A completed cache is bound to the exact reply by SHA-256 and can replay locally
without Wi-Fi. A new microphone recording reuses the same scratch partition and
invalidates cached speech; app shutdown/reboot loses its in-memory validity and
position. Recording or downloading an uncached reply still needs Wi-Fi/TTS. NVS
records and settings are independent of this temporary audio cache.
The TLS receive limit is 16 KiB because MiMo sends full-size records; reducing
it to 8 KiB interrupts speech with mbedTLS error `-0x7100`. TLS reserves the 16 KiB receive and 4 KiB transmit buffers for each connection
and releases them at HTTP client cleanup. Dynamic per-record allocation is disabled:
actual-machine replay intermittently failed with `-0x7F00` when a full-size record
could no longer obtain a contiguous block during playback. Start the PCM worker only after HTTPS response headers arrive.
Failure shows the error separately and keeps the previous reply accessible.
The reply buffer is 4096 bytes, stored outside task stacks. The model budget is
1024 output tokens for assistant replies and 2048 for stories; the chat I/O timeout is 45 seconds. Both local UTF-8-safe
clipping and the server's `finish_reason: length` mark a partial reply. The
background service health check does not replace a reply. The preset prompt
requests complete plain paragraphs, preferably within 500 Chinese characters,
without Markdown, blank lines, emoji or decorative symbols, even when a user
requests those formats. Since the model can still violate it, the worker also
collapses blank lines and removes common Markdown markers and emoji before
display. This cleanup runs after command validation, never on unparsed JSON.

Example structured command:

```json
{"actions":[{"action":"record_feeding","time":"15:20","amount_ml":150,"ingredient":"FORMULA"}]}
```

The prompt distinguishes completed facts from questions, negation, plans and
hypotheticals. A feeding fact with an amount becomes a record; feeding advice is
plain text. Missing required information produces a plain-text follow-up.

| Voice action | Required fields / effect |
| --- | --- |
| `record_feeding` | Integer `amount_ml` 10–400; optional explicit `ingredient` (`FORMULA` / `BREAST_MILK`) |
| `record_diaper` | `kind`: `pee` / `poop` |
| `record_sleep` | Integer `duration_min` 0–65535 for an already completed sleep |
| `record_bath` | One bath event |
| `record_tummy` | One tummy-time event |
| `start_sleep` | Start background sleep, reject an already running session |
| `end_sleep` | End the running sleep and calculate duration; reject when none is running |

Optional `time` is today's `HH:MM` when explicitly supplied; omission uses device
time. The worker accepts only a complete JSON object with exactly one whitelisted
action. Malformed, clipped, multiple, unknown or invalid actions never commit.
It saves NVS before replacing JSON with a device-generated success message and
offering undo. Persistence failure restores the prior state and reports an error.
Plain text never changes records. Settings, Wi-Fi, timers and audio playback are
not exposed as voice commands. The older `reply_text` / `tts_text` response API
remains for compatibility. Story mode uses the dedicated `mimo-v2.5-tts` stream,
decodes each `delta.audio.data` Base64 chunk as 24 kHz 16-bit mono PCM, and sends
it through the filtered 12 kHz BSP audio worker path; ordinary AI replies remain
text-only for now.
Audio library is the second home entry, visible on the first three-card page. The cloud
catalog caches at most 16 song, educational-story, classical-early-learning, or
white-noise tracks. Each item keeps its category label so the device can distinguish
Songs, Educational Stories, Classical Early Learning, and White Noise while browsing. UP/DOWN selects a title or the
Pause/Resume, Stop, and Refresh controls; OK activates the selection. Canonical
12 kHz, 16-bit mono WAV is validated before PCM playback on the shared AI worker.
Pause saves the emitted PCM byte position, closes HTTP, sleeps the codec, and
returns the worker to its queue. Resume uses a validated HTTP Range response.
Opening AI or Voice story cancels the song; recording and other audio requests
can also take ownership. Selecting another song cancels and replaces the current
playing or paused session. A canceled song never restarts automatically. Pause
position is held in RAM: reboot or a failed stream requires a fresh play request.

Today uses pages rather than a hidden scroll list. The summary page is followed
by the active/recent sleep pages and recent feeding pages. Empty state still offers a
return path and Quick record. Sound playback belongs to an audio worker and is
stopped or handed over before page deletion. Wi-Fi and AI pages show configured
status and masked identifiers. Wi-Fi scans on startup, checks all scan results,
tries the three visible built-in networks first, then the saved network. Connection
failures include three retries for association expiry; automatic mode scans again
after 15 seconds if candidates are exhausted. Manual search takes control of the
radio flow. Bluetooth provisioning and manual SSID entry are removed.

The password keyboard has six columns and up to five character rows. Uppercase
and lowercase each show all 26 letters on one page; digits/symbols use two pages.
Type switches and Backspace/Next page/Done remain visible in two fixed bottom rows.
The selected key has a cyan fill, dark text, and white outline. Every UP/DOWN press
moves immediately; double UP/DOWN moves one row from the gesture's starting key.
Three or more rapid presses still move the cursor. OK selects, long UP deletes,
long OK finishes, and long DOWN cancels. Passwords are masked. Keyboard widgets
are created only during editing and released on exit to preserve TLS heap.
Wi-Fi and MiMo presets use the owner's authorized tracked configuration.

Every page shows its current UP/DOWN/OK action in the bottom bar. There is no
first-use tutorial; the operation hint is always available where the action
happens.

## 6. Feedback and hardware limits

All actions use `idle → accepted → running → completed`, with `failed/retryable`
and `cancelled` branches. Messages always state both the result and the next key:
`Saving 150 ml…`, `Recorded · ● Undo`, `Save failed · ● Retry · ▼ Back`, and
`Network unconfirmed · offline records still work`.

The UI avoids full-screen images and double buffers, keeps the existing LVGL pool
and partial RGB565 buffer budget, dims the backlight after about 20 seconds, and
uses the first key after wake only to wake the screen. Storage, TLS, audio, and
Flash writes run in workers. Voice capture streams 16 kHz mono PCM into the
recording partition for up to 60 seconds. Its 2 KiB scratch buffer is allocated
only for capture and freed on success or read/write failure, keeping it out of
the shared 6 KiB AI task stack. Capture logs include stack high-water marks;
these diagnostics do not by themselves establish the cause of a device reboot.

Fixed Chinese strings use the built-in Source Han Sans SC 16 CJK subset only after
glyph coverage is checked. Numbers and units may use Montserrat, but mixed lines
must be checked for baseline and line-height issues.

## 7. Delivery order

Implement offline Feeding, Diaper, Sleep, Today, and Settings first with a
bounded local event log. Add trusted time, Wi-Fi/AI configuration, Voice chat,
the dedicated Voice story with TTS, and finally server-streamed songs/noise in
separate increments.
Report build, host tests, device tests, and unverified visual/font/power checks
separately for every increment.

Each feeding, pee, poop, sleep start/end, bath, and tummy-time action first
creates a local event with a type, monotonic device time, trusted wall-clock time
when available, and parameters. Offline events remain local; a later sync worker
uploads them by event ID and marks them synced only after success. Server summaries
never replace raw events. Voice ingredients, notes, and story prompts retain the
original text and the model-result source.

The JSON response is the stable device/service protocol. The device accepts only
whitelisted actions, bounded values, and supported time fields. Unknown actions
produce an error without side effects. The current firmware stores a bounded NVS
event queue first; future synchronization should use event IDs instead of reparsing
model prose.

## 10. Record context, handoff, corrections and reminders (2026-10-01)

The application adds two top-level entries, Caregiver handoff and Reminders,
after Settings. These use the application's existing screen design. Handoff
shows the latest local feeding, sleep and diaper records, then the cached cloud
summary and parent note. UP/DOWN pages the reader, OK requests speech, and a
long DOWN returns home. Cloud data retains its synchronization timestamp;
missing records are never presented as proof that an activity did not happen.
The web workspace has a handoff page with a persistent parent note of at most
160 characters. Device refresh is periodic and requires the updated backend.

Ordinary assistant requests include bounded local records, computed local-day
milk/diaper/sleep totals, ongoing sleep, current local date/time and reminders.
A cached seven-day server summary includes its snapshot revision and timezone.
Local and cloud totals must not be added together. Uncalibrated time and a full
32-record ring are explicit limitations. Ordinary replies now offer Read reply
as a fourth action; speech remains optional. Story generation and automatic
service checks do not receive this record context.

Retrospective records accept exact local `YYYY-MM-DD HH:MM` values, or `HH:MM`
with `days_ago` from 0 to 365. Invalid dates and future record times are rejected.
Sleep backfill accepts `start_time` and `end_time`; the device calculates the
interval across calendar dates. `edit_last_feeding` may change amount, time or
milk kind in the most recently saved feeding. The device displays before/after
values, defaults to Cancel, and requires selecting Save and pressing OK. The
preview expires after five minutes and rejects a changed or overwritten target.
A successful edit keeps the event sequence, syncs without duplication and has
the existing five-second undo window. Other record types cannot yet be edited.

`create_reminder` accepts a short `title` and either `delay_min` (1 to 10080) or
an exact local `at`. `cancel_reminder` requires its displayed integer `id`.
Up to eight one-shot reminders persist in the separate `care_v1` NVS key.
Due reminders show a screen alert and queue a short offline tone if audio is
free. They wait for a trustworthy clock after reboot; overdue reminders remain
available until handled. UP/DOWN selects Done or Remind in ten minutes, OK
persists that choice, and long DOWN dismisses the popup while retaining the
reminder in the list. Selecting a reminder from the list opens this management
screen before it can be removed. Audio, recording and reply processing are not
interrupted. Reminders do not execute the action named in their title and do
not generate medical or feeding schedules. Phone notifications and recurrence
are outside this increment. The original partition and record layout remain.

## 11. Persistent status bar and proposed phone configuration (2026-10-01)

Every application page now has a separate first row for local `HH:MM`, network
state and battery. The one-second LVGL timer refreshes it even on an idle home
page or while reading a reply. Before the clock reaches the existing trusted
epoch threshold it shows `--:--`; changing the configured timezone applies to
the row. The page title occupies the next row without reducing the content
reader or keyboard area. Numeric fields use Montserrat 14; network labels use
the checked 16 px Chinese subset. An unavailable or invalid battery is `--%`.

The row prioritizes the current Wi-Fi state over earlier service-check results.
The localized Connected label means an IP connection without a confirmed public
HTTPS check; Checking means the existing check is running; Online means the
latest public HTTPS probe succeeded, including when the model subsequently
failed. A failed public probe shows Connection failed. These are periodic results,
not continuous Internet or childcare-backend availability measurements. No
network request runs from the LVGL timer. SSID/IP and model details remain on
their existing pages.

The requested NFC/phone configuration flow is a proposed next increment, not
implemented by this status-bar change. The board's NTAG213 is a passive tag
with no MCU interface. A phone can write an NDEF URI record containing the
management entry URL; tag contents do not change when firmware is flashed and
the MCU cannot detect a tap. The [NTAG213 datasheet](https://www.nxp.com/docs/en/data-sheet/NTAG213_215_216.pdf)
specifies 144 bytes of user memory, enough for a short entry URL. Keep the tag
limited to the entry address; the management session and BLE configuration
authorization remain separate from reading it. iPhone background reading shows
a notification that opens the URL after a tap, as described by
[Apple](https://developer.apple.com/videos/play/tech-talks/702/). Android launch
behavior also depends on its version; see [Android NFC basics](https://developer.android.com/develop/connectivity/nfc/nfc).

The phone management page can provide multiple named Wi-Fi profiles and their
priority, API-key replacement, and separate conversation/ASR/TTS model choices.
The proposed transaction is: enter device configuration mode, connect the phone,
read non-secret settings, edit, validate, persist a complete configuration in
NVS, apply it in a worker, and report its revision and connection result. Mask
secrets in the page and do not echo stored passwords/keys in BLE reads or logs.
An interrupted transfer must leave the last valid configuration usable. Retain
the owner-authorized compiled defaults as fallback profiles with explicit NVS
overrides; runtime changes should not require a firmware rebuild.

[Web Bluetooth in Chrome](https://developer.chrome.com/docs/capabilities/bluetooth)
supports BLE GATT reads/writes on Android Chrome, requires a secure context and
a user gesture to choose a device. The current HTTP management address must
therefore gain a trusted HTTPS entry before this path is usable. NFC opening a
page does not remove the Connect-device click. Safari/iOS currently lacks this
API, according to [MDN's compatibility data](https://developer.mozilla.org/en-US/docs/Web/API/Web_Bluetooth_API).
An iPhone path needs a native BLE client, or a browser configuration flow through
a temporary device Wi-Fi access point. The phone platform and HTTPS address
are still open inputs. BLE requires a new application GATT service, bounded
configuration packets and an on-device authorization window; it should stop
after completion/timeout. Measure firmware size and concurrent Wi-Fi/BLE/TLS/
audio heap before choosing radio concurrency on this no-PSRAM board.

## 12. Android BLE provisioning (2026-10-02)

The Wi-Fi part of the phone proposal above is now implemented for Android.
The device Wi-Fi menu has a separate **Phone Bluetooth provisioning** entry.
It starts a five-minute worker-managed window, displays a fresh six-digit PIN
and the `Xigua-` radio name, and offers close/back. A page change, finish command
or timeout stops the host before releasing its buffers. Existing AI/cloud
requests drain before BLE starts; new requests pause during the window.

The authenticated HTTPS console has connect/end/status controls, a built-in
SSID selector, nearby-network scan, and manual SSID/password entry. Password
entry uses visible text with automatic correction/capitalization disabled.
Selecting an SSID fills the form; saving waits for the device's actual IP and
persistence result. A failure preserves the previous saved NVS network.
Built-in passwords, the pairing PIN and API keys are never returned by GATT.

Service UUID: `7e24a7f0-9b52-4f36-a7f8-84e9d9b00001`; command/status UUIDs end
in `00002`/`00003`. Encrypted, authenticated characteristics use bounded UTF-8
JSON plus newline, 512 bytes including delimiter. The browser writes acknowledged
20-byte chunks; request IDs match read-back responses. Scan and preset lists use
single-entry paging to remain within the ATT attribute limit. Disconnects clear
partial commands and never replay credentials automatically.

Entry: `https://162.14.108.234/cloud-backup/console#phone`, using the server's
existing trusted IP certificate. The old HTTP NFC address redirects here via
the login page. No domain or new phone app is needed for this Android workflow;
the Connect action and device PIN remain required. iPhone support, editing
multiple saved Wi-Fi profiles/priorities, and API-key/model editing remain
future work. NFC cannot signal the MCU or open its physical authorization window.
