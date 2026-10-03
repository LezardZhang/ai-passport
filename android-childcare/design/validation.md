<p align="right"><a href="validation.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Design validation record

## Revision 2: unified care and household lifecycle

2026-10-03, Asia/Shanghai. The in-app browser was unavailable during this revision, so the available Chrome browser was used for actual prototype inspection. [Revision 2 specification](simplified-design.md) supersedes the earlier four-root prototype. The revision-1 interaction table below remains historical evidence.

| Check | Observed result |
| --- | --- |
| Navigation | Three roots: Care, Records and Household; export, profile and maintenance open as secondary surfaces |
| Mobile baseline | 390 × 844; document width and scroll width both 390 px; no horizontal document overflow |
| Reachable input | Voice and Send are 48 px high, ending at y=712.5; bottom navigation occupies y=761–829; conversation scrolls separately |
| Local person registration | Entered a demonstration name, reviewed local-only identity, simulated failure, retried and saved; reloading retained the demonstration profile |
| Failure after latest changes | Review displayed an inline failure reason with draft and Save/Back-to-edit actions; no person was added |
| Person update/archive | Update showed revision 2; archive cleared the current actor while the existing event retained its original actor name |
| Unified record route | A 150 ml phrase produced a confirmation card; confirming added one local demonstration event and updated its Records total to 150 ml/one feeding |
| Story unavailable | Original theme stayed on its owned card with Service not connected; no online request or fake AI result |
| Unified voice | Start/stop simulation produced editable text; inserting it left Send outstanding and did not create another record |
| Denied microphone | Permission-denial simulation offered system settings and typing |
| Night/130% type | Actual browser capture shows wrapped content with Voice/Send and three-root navigation still visible |
| Export and maintenance | Records export opens; installed-version sample, unconfigured update channel and backup/restore limitations are explicitly shown |
| Browser console | No error entries at final inspection |

Revision-2 captures:

- Unified care (local artifact: `screenshots/unified-care-v2.jpg`) and desktop overview (local artifact: `screenshots/unified-desktop-v2.jpg`)
- Intent preview (local artifact: `screenshots/intent-preview-v2.jpg`) and records (local artifact: `screenshots/records-v2.jpg`)
- Household (local artifact: `screenshots/household-v2.jpg`) and person confirmation (local artifact: `screenshots/person-review-v2.jpg`)
- Version and maintenance (local artifact: `screenshots/maintenance-v2.jpg`)
- Unavailable story (local artifact: `screenshots/service-unavailable-v2.jpg`)
- Night/large type (local artifact: `screenshots/unified-night-large-v2.jpg`) and voice transcript (local artifact: `screenshots/unified-voice-review-v2.jpg`)

Profile persistence is real only for demonstration browser data. Care records, speech, media, reminders, synchronization, updates and restore remain simulations or explicit unavailable handoffs. Revision 2 has not been claimed implemented in the native APK. Real storage-exhaustion handling, Android migrations/process death and physical-phone behavior remain implementation acceptance work.

## Revision 1: historical prototype checks

Date: 2026-10-02, Asia/Shanghai. Tested the actual local prototype in the Codex in-app browser. This validates the design artifact, not the native APK or a live backend.

| Check | Observed result |
| --- | --- |
| JavaScript syntax | PASS: `node --check prototype.js` |
| Design tokens | PASS: JSON parsed successfully |
| Markdown pairs and links | PASS: English/Chinese pairs and all local targets exist |
| Browser console | No error entries at the final review |
| Mobile baseline | 390 × 844; no horizontal document overflow; bottom navigation remains visible |
| Feeding save/Undo | Saving an edited 180 ml sample shows Saved on phone/Pending sync; Undo removes that event and shows a deletion awaiting sync |
| Voice draft | Start/stop simulation produces an editable transcript; Use text fills its Q&A editor before Send |
| Q&A/Story ownership | A Q&A-specific prompt and a Story-specific prompt remain on their own result screens after switching |
| AI failure/Retry/Cancel | Failure retains original prompt; Retry enters processing; Cancel returns to input |
| Denied microphone | Denial shows system-settings and type-instead actions |
| Cross-page player | Story title remains in mini-player across navigation; pause/resume states and controls were checked |
| Empty first launch | Zero records, no active sleep/player, birthday unknown and empty handoff/reminders |
| Night/130% type | Adaptive quick actions form one 350 px column inside a 390 px phone; no horizontal overflow; bottom nav ends at y=829 inside 844 px viewport |

Captures:

- Home (local artifact: `screenshots/home-mobile.jpg`) and desktop overview (local artifact: `screenshots/home-desktop.jpg`)
- Feeding sheet (local artifact: `screenshots/feeding-sheet.jpg`)
- Voice recording (local artifact: `screenshots/voice-recording.jpg`)
- Story result (local artifact: `screenshots/story-result.jpg`)
- History (local artifact: `screenshots/history-mobile.jpg`)
- First-launch empty (local artifact: `screenshots/first-launch-empty.jpg`)
- Night/large type (local artifact: `screenshots/night-large-type.jpg`)

Build: NOT RUN (the design prototype has no compilation step; APK is owned by the main implementation workflow).

Host tests: NOT RUN (static syntax/JSON/link checks and browser interaction checks ran; no Android state-machine tests were run by this design agent).

Device tests: NOT RUN.

Native screenshot evidence update (2026-10-03): the depicted native home at 200% system font scale, baseline home/history, night form and compact audio player were reviewed from supplied emulator captures; see the [final native screenshot review](native-ui-review.md).

Unverified by this design review: physical-phone rendering/behavior, 200% layouts on other screens, TalkBack, keyboard-open layouts, real ASR/TTS, live backend, notification/reboot behavior, audio quality and background media lifecycle. Prototype sleep/voice/AI/player states are simulations. Repository-wide gates and APK validation remain with the main workflow.
