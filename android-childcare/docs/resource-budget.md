<p align="right"><a href="resource-budget.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Android allocation and phase budget v5

This budget is separate from ESP32-C3 firmware. APK size/build success do not prove runtime reserves. Android microphone, decoder and TTS allocate outside Java heap.

| Owner | Preparation through cleanup |
| --- | --- |
| Main/UI | Up to100 each records/tracks/reminders/conflicts. Summaries stream SQLite. Input/reply12,000chars; notes2,000chars; composer retained through speech refresh. |
| Database executor | One serial write/import/generation; transactions preserve records/outbox. History and queues are not evicted. |
| Network executor | One sync/chat/model/update operation. JSON responses1MiB, connect10s/read30s Personal API or60s provider. JSON/parse/string copies require an approximate8MiB envelope plus provider metadata. |
| Provider configuration | Up to20 profiles,512 discovered model IDs/profile,200chars/ID,2,097,152-character serialized metadata (~4MiB UTF-16, up to~6MiB UTF-8). Editing/all/snapshot copies can coexist; reserve beyond those serialized copies, not just payload. Bundled defaults read cap128KiB. |
| Speech executor | One recorder or ASR transport;16kHz mono16-bit PCM,4KiB buffer. No elapsed recording cutoff. File payload cap64MiB and4MiB free reserve; initial check8MiB. Recorder released before ASR. Retry draft stays private until success/discard. |
| ASR staging | Original draft plus one60-second transport WAV (1,920,044bytes). Sequential8KiB streaming/base64, ~2.56MiB wire chunk without whole-audio arrays. Reply1MiB/12,000chars and combined text12,000chars. Cancellation disconnects/invalidates callbacks and removes partial capture/chunk files. |
| Media/TTS | One player/session, platform streaming decoder. One synthesis file up to128MiB checked after OS generation, text split3,500chars. Stop/failure releases player/focus/TTS; pause expires after5min. Platform peak remains unmeasured. |
| Update staging | Manifest64KiB, APK64MiB,32KiB copy buffer; old64MiB plus new partial64MiB can coexist.4MiB initial free check/2MiB copying reserve; verify before rename/installer. |

Speech and network owners may overlap: at most two app HTTP calls plus platform playback/UI/database. Sync processes at most100 pushes and100 pull pages of100changes; views paginate persisted history. Provider snapshots remain only for unfinished direct requests and are cleared on terminal state/cold recovery. Bound queued requests/history can still grow; users export/manage data explicitly.

Audio imports use32KiB buffers,128MiB/item,256MiB directory including partials; cold cleanup removes interrupted files. Generated30-second rain is2,646,044bytes. A previous64MiB voice draft and new64MiB capture plus1.83MiB ASR segment plan about130MiB. Combining imported256MiB, TTS128MiB, updates128MiB and voice130MiB plans about642MiB plus database/journal/exports/platform synthesis/system installer. These are caps, not free-space guarantees. Voice promotion replaces the draft; temporary/chunk cleanup is mandatory. Complete failed audio is intentionally retained.

## Evidence and limits

Android v3 tests (local artifact: `../reports/android-tests-v3.log`) use real AudioRecord for Send/duplicate/error/retry/background cancellation and65-second capture through periodic refresh. Live-provider test (local artifact: `../reports/android-live-provider-v3.log`) uses generated Chinese audio with actual network models/chat/ASR; it does not measure human speech accuracy or microphone sound quality. UI/memory evidence (local artifact: `../reports/native-ui-smoke-v3.json`) and PSS sample (local artifact: `../reports/memory-sample-v3.txt`) are single emulator observations, not peaks/leak proof. Historicalv2 steady PSS was32,526KiB; v0.1 physical sample115,196KiB does not measurev3.

Phone acceptance targets remain≤128MiB steady PSS and≤160MiB transient, with stable repeat-run cleanup. Minimum-device/API26, long background audio/calls/headset/reboot/TalkBack, low-storage paths, maximum responses/config/imports, concurrent sync and native contiguous-allocation reserves remain unverified. Do not turn a build or single sample into a capacity claim.

## Timeline extension

Configured:100 native rows/page with one time label, one decorative rail and one activity card per row; discarded on page redraw. Caregiver form holds at most100 active roster choices plus two snapshot/unknown choices, names40chars and opaque IDs128chars. Native wire preservation copies one bounded stored remote JSON record into one request; it stays under existing1MiB response/request bounds. Web adds100 cards per page and at most100 name suggestions. Load More deliberately retains appended pages; browser DOM can grow with history and no peak claim is made. Server queries still materialize a snapshot before filter/sort, using O(n) storage and O(n log n) sorting. Historical inventory limits and production RSS remain unverified. No media/font/dependency allocation or ESP32 firmware change.

## Cloud release extension

Automatic discovery queues at most one check; six-hour throttling limits repeated launches. Updates share the serial network executor with provider calls and synchronization. Manifest64KiB plus bounded JSON copies, APK64MiB and32KiB buffers retain the staging budget above. Server hashing uses64KiB buffers; ZIP preflight reads at most65,557bytes and admits central directories≤2MiB/4,096objects before parsing. One upload may coexist with authenticated downloads; OS/FastAPI/socket buffers and FileResponse add platform overhead. Configured container192MiB, disk128MiB reserve and2GiB release quota do not establish peak RSS, concurrency capacity or available storage. Production capacity and minimum-phone update reserves remain unmeasured.

One production container sample after the first publication was32.77MiB of the192MiB limit, with2 processes. This is a point sample, not peak RSS or a concurrency/load acceptance.
