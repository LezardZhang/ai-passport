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
| Overview | AI assistant first, plus manual record, Today, Sleep, Sound, and Settings | Boot; long DOWN from a top-level page | OK opens focus; UP/DOWN changes focus |
| Feeding | Record time, amount, and ingredient/notes | OK on Feeding focus | OK edits; long DOWN back |
| Diaper | Record pee/poop and occurrence time | OK on Diaper focus | UP/DOWN choose; OK saves |
| Sleep | Start, end, and calculate duration | OK on Sleep focus | OK starts/ends; long DOWN abandons after confirmation |
| AI assistant | One entry for JSON records, text answers, and TTS stories | OK on the first Overview focus | UP/DOWN choose ask/story; hold OK to talk; long DOWN back |
| Songs/noise | Choose a playlist, volume, and playback state; streaming later | OK on Sound focus | UP/DOWN choose; OK play/pause |
| Today | Paginated counts and recent records | OK on Today focus | Long DOWN back; UP/DOWN pages |
| Settings | Display, sound, Wi-Fi, time, AI, device info, restart | OK on Settings focus | Long DOWN back; destructive actions confirm |
| Wi-Fi/time/AI | Show state and test results; auto-connect known networks, scan/select an SSID, and enter its password | Settings item | UP/DOWN choose; OK advances; long OK completes text; long DOWN back |
| Result/error bar | Show saving, success, failure, retry, and undo | After any write or test | Timeout returns; OK undoes or retries |

Only the current page, its snapshot, and short strings stay allocated. Producers,
timers, and callbacks stop before their page objects are deleted.

## 3. Overview layout

The 240×320 display has four fixed areas:

1. A title and battery header.
2. A 204×190 px content area with a two-line milk/sleep/diaper summary and up
   to three 38 px menu cards, separated by 6 px gaps.
3. A feedback line, showing the menu page number when no action feedback exists.
4. A fixed two-line key hint: UP/DOWN selects, OK opens, hold UP records feeding.

The focus order is AI assistant → Manual record → Today → Sleep → Sound →
Wi-Fi → Settings. The three menu pages contain 3, 3, and 1 items; UP/DOWN wraps
between the first and last items. Selection moves on PRESS, so rapid repeated
presses continue moving even when the driver subsequently reports DOUBLE.
Menu cards share the AI preparation/action colors: dark blue background, cyan
selection with dark text and a white outline. Cards exist only on the Overview
page. The short undo window still takes priority over opening a menu item.
The AI assistant is the single top-level entry: the model can call local records through
JSON, while text/TTS is reserved for content that needs a parent-facing response.
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
`▲ -10 ml  ▼ +10 ml  ● Save` at the bottom. Time is taken from the trusted clock;
ingredients start as fixed choices and free notes arrive through voice. Values
outside 10–400 ml require manual review instead of silent clamping.

The diaper page has only Pee and Poop. It writes a local event immediately; voice
can add colour, consistency, or notes later. Sleep uses one active-session page and
stores start, end, and duration. Bath and tummy time remain optional one-shot
events rather than timers.

Saving enters a short `Saving` state. Success shows `Recorded 150 ml` and a
five-second undo. Failure shows retry and back; the authoritative counters are
not updated early.

The AI assistant handles Q&A, records, and stories through one PTT, ASR, model, and
TTS pipeline. The model must return either a structured command or a parent-facing
reply.

The implemented AI UI separates preparation, recording, processing, reply reading,
reply actions, and failure. Preparation has three highlighted cards: hold OK to
speak, view the last reply, and return. Short OK never sends a canned prompt or
starts recording. Only holding OK on the preparation recording card starts PTT;
release ends capture. Processing ignores OK and retains the last successful reply.
The reply fills a dedicated clipped viewport using the complete 20 px font.
UP/DOWN press changes one page of five 38 px lines; focus, clicks, and double-clicks
cannot send another request. OK opens reply actions with Continue selected by
default; Ask again returns to preparation and still requires a deliberate hold.
Failure shows the error separately and keeps the previous reply accessible.
The reply buffer is 1024 bytes; truncation respects UTF-8 boundaries and is marked
as a partial reply. The background service health check does not replace a reply.

Example structured command:

```json
{"actions":[{"action":"record_feeding","time":"15:20","amount_ml":150,"ingredient":"FORMULA"}]}
```

The firmware validates and stores whitelisted actions (`record_feeding`,
`record_diaper`, `record_sleep`, `record_bath`, and `record_tummy`) in the local
event queue. A response that needs to be shown or spoken uses
`{"reply_text":"Recorded formula 150 ml at 15:20"}` or
`{"tts_text":"Once upon a time..."}`. Plain reply text never becomes a device
command. Songs and white noise use a bounded local/streaming audio queue; HTTP
chunks are never passed directly to I2S.

Today uses pages rather than a hidden scroll list. Empty state still offers a
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
Voice story, TTS, and finally server-streamed songs/noise in separate increments.
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
