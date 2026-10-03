<p align="right"><a href="native-ui-review.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Native implementation UI handoff review

Revision-2 navigation update: the [simplified three-root design](simplified-design.md) is now represented by the supplied native Care/Records/Household screenshots. The revision-2 section below reviews that implementation; the older four-root findings and captures remain historical evidence.

Initial source review: 2026-10-02. Final emulator screenshot review: 2026-10-03, Asia/Shanghai. The initial findings below describe the earlier `MainActivity.java`/`Ui.java` implementation and are historical; the final screenshot section supersedes their visible-state findings. This review does not change application code or establish physical-phone, microphone or sound-quality acceptance.

The initial implementation followed the four destinations, used native controls, showed local-only/production-not-verified status and started with an empty timeline. The earlier recommendations were:

| Priority | Finding | Concrete change |
| --- | --- | --- |
| 1 | Today has feeding/sleep/diaper, but no quick voice-record path. Speech is available only inside Q&A/Story. | Add “Say it, record it” after quick actions. Recognition produces editable text, then a typed event/time/quantity preview; only Confirm save writes. Unsupported/ambiguous input remains editable text. |
| 2 | Current sleep hero uses the same light-sage surface as many supporting cards; the active state has weak visual priority. | Use dark forest `#214E43` with light text for active sleep, a separate 44–48 sp elapsed number and a light end action. Retain a quieter light empty state. |
| 3 | Three weighted quick actions, metric columns, date buttons and four audio category buttons assume compact text. | At `fontScale >= 1.3`, stack quick actions/metrics or allow adaptive wrapping; date actions can use icons plus full accessibility labels. At 200%, preserve full quantity/duration meaning and every action without horizontal clipping. |
| 4 | The mini-player is title + three 52 dp buttons and can consume over 100 dp before navigation. | Prefer title + pause/resume + stop in one compact 64–80 dp row; put timer/seek on a full player/details control. Limit title visually to two lines with an accessible full label. Show buffering and actual retry semantics instead of offering Continue for an unrecoverable failure. |
| 5 | Day secondary `#718078` is too faint for small text on cream; page navigation preserves the previous screen's scroll position. | Set secondary to `#667267` (4.75:1 on `#FAF8F3`). Reset scroll for a new destination; preserve within-page scroll only during data refresh. |

Additional implementation boundaries: CSV is the required export; JSON shown in the design is optional until implemented. Breastfeeding side/duration, recent-deletion recovery, reminder snooze/edit, richer history ranges and voice-created structured events must not be displayed as complete unless they work. Handoff notes currently local-only should remain clearly labelled until the actual backend contract supports syncing them. Static design copy should not replace the app's accurate feature status.

Read [design specification](design-spec.md) for full state, navigation, accessibility and copy requirements. night/large-type screenshot (local artifact: `screenshots/night-large-type.jpg`) illustrates adaptive rows; feeding sheet (local artifact: `screenshots/feeding-sheet.jpg`) and voice capture (local artifact: `screenshots/voice-recording.jpg`) illustrate action hierarchy. The prototype demonstrates interface states, not phone recording or sound quality.

## Revision 1: historical emulator screenshot review — 2026-10-03

Inspected the five supplied native captures (1080 × 2400), rather than treating the HTML prototype as Android evidence. No blocking visual defect is visible in these states.

| Step | Actual screenshot | Observed visual result |
| --- | --- | --- |
| 1 | Final home (local artifact: `../reports/screenshots/native-home-final.png`) | Clear local-only status, empty totals, three quick actions and the added voice-record entry. All four navigation labels remain visible. |
| 2 | Night feeding form (local artifact: `../reports/screenshots/native-night-form.png`) | Labels, fields, feeding choice, Cancel and Save are readable and contained in the dialog. The background dimming is intentional. |
| 3 | Night home at 200% (local artifact: `../reports/screenshots/native-night-200-percent.png`) | Active sleep has a dark-green card and large timer. Status/start text wraps, the end-sleep action stays intact, quick actions reflow vertically and all four navigation labels remain readable. Lower content continues beyond the captured viewport; this image does not verify every screen or lower-row scroll reach at 200%. |
| 4 | History (local artifact: `../reports/screenshots/native-history.png`) | Date actions, pending status, totals and record details remain readable. Ongoing sleep is explicitly separate from completed sleep; navigation does not cover the visible records. |
| 5 | Night audio playing (local artifact: `../reports/screenshots/native-audio-playing.png`) | The compact player shows title/state, Pause and Stop above the separate navigation row. Timer control is on the audio page; no visible control collision. This is playback-state evidence, not audible quality. |

These captures confirm native emulator rendering for the depicted baseline, night form and 200% home states. The emulator smoke record (local artifact: `../reports/native-ui-smoke.json`) identifies audio output as disabled. Physical phone behavior, real ASR/TTS, sound quality, TalkBack/focus semantics, keyboard-open layouts and 200% layouts on other screens remain unverified by this review. HTML prototype voice/AI/media states remain simulations. JSON export, breastfeeding duration/side, recently deleted recovery and other optional design capabilities are not claimed implemented from these images.

## Revision 2: native screenshot review — 2026-10-03

Inspected nine supplied native emulator captures (1080 × 2400) directly with the image viewer. Scope: enter a unified care request, review a feeding write, register/manage caregivers, inspect update status, and use the depicted night/large-font/media states. No blocking visual defect is visible in these nine states.

| Step | Actual screenshot | Health and observed result |
| --- | --- | --- |
| 1 | Care home (local artifact: `../reports/screenshots/care-v2-home.png`) | Clear: one text/voice composer is fixed above the three-root navigation. Speak and Send are fully visible; empty/local status and optional household completion have clear next actions. |
| 2 | Feeding confirmation (local artifact: `../reports/screenshots/care-v2-feeding-confirm.png`) | Clear: child, unset actor, occurrence time/timezone, 150 ml, feeding choice and editable note are visible before Save; Cancel and Save are contained in the dialog. |
| 3 | Caregiver registration (local artifact: `../reports/screenshots/care-v2-person-register.png`) | Clear: name, relationship and optional telephone are editable together, with the local-storage/current-caregiver explanation and both actions visible. This form itself provides the editable pre-save review; a separate second preview screen is not required. The keyboard is closed in this capture. |
| 4 | Household management (local artifact: `../reports/screenshots/care-v2-family.png`) | Clear: baby editing, current caregiver and each person's Edit/Switch/Deactivate actions are readable and grouped. Local registration is explicitly distinguished from cloud accounts/permissions. The lower list continues beyond the viewport. |
| 5 | Version and updates (local artifact: `../reports/screenshots/care-v2-updates.png`) | Clear: installed version 0.2.0-local/version code 2, local signing identity and unconfigured online channel are stated. Channel setup, checking and APK selection have separate actions; package/signature/higher-version requirements and Android installer confirmation are explained. It does not claim a deployed update channel or Latest version. |
| 6 | Night Care home (local artifact: `../reports/screenshots/care-v2-night.png`) | Clear: text, local totals, composer, Speak/Send and navigation remain readable in the dark palette with no visible collision. |
| 7 | Night Care at 200% (local artifact: `../reports/screenshots/care-v2-night-200-percent.png`) | Clear for depicted controls: the input hint wraps and Speak/Send stack vertically; all three navigation labels remain readable and above the system gesture area. The upper summary continues beyond its scroll viewport; this image does not establish every lower-row reach or 200% form layout. |
| 8 | Night audio playing (local artifact: `../reports/screenshots/care-v2-playing.png`) | Clear: the compact player presents title/playing state, Pause and Stop above the separate navigation. Local import and offline rain are distinguished from the cloud catalog; the catalog empty state explains a next step. This is visible media-state evidence, not sound-quality evidence. |
| 9 | Final Care home (local artifact: `../reports/screenshots/care-v2-final.png`) | Clear: the settled night home retains the local summary, complete fixed input actions and three navigation labels; no blocking overlay or loading state is visible. |

Implementation differences accepted for this revision: native person forms provide the editable preview in the form itself; the current caregiver must be switched before deactivation rather than being implicitly unset. The main workflow reports registration/edit/switch/deactivate/restore and draft retention on failure. Those behaviors are not inferred solely from the static captures.

The supplied Android instrumentation record (local artifact: `../reports/android-tests-v2.log`) reports 98 passing assertions, including actual foreground AudioRecord capture, Home cancellation/temporary WAV cleanup, caregiver lifecycle and update identity checks. This is separate execution evidence; it does not certify acoustic quality, recognition accuracy, a deployed release channel or normal live model availability. Normal MiMo API use still requires separately configured credentials; Google is not enabled. The native update implementation supports an optional HTTPS manifest and bounded APK import (64 MiB), same-package/signer and higher-version checks, followed by Android system confirmation; an online channel has not been deployed. Cloud account registration/invitations remain unsupported.

Evidence limits: this review did not operate the native UI or run instrumentation itself. Full TalkBack/focus semantics, keyboard-open reach, 200% dialogs/household/update/player layouts, permission recovery and physical-phone behavior for revision 2 are not verified by these screenshots. Profile failure/draft recovery, channel errors/download cancellation and installer outcomes require the matching functional evidence. HTML prototype capture/AI/media/update/restore states remain design demonstrations, and its browser profile persistence is independent of the Android data layer. Optional design capabilities remain unclaimed unless their own implementation and acceptance evidence exist.
