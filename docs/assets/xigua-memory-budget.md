<p align="right">
  <a href="xigua-memory-budget.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Xigua Assistant Resource Budget

Apply the [framework rules](../development/engineering/memory-budget.md) to all
resource changes. This is the current application contract, updated with code;
the [2026-10-02 audit](xigua-project-handoff.md#memory-capacity-assessment-2026-10-02)
retains historical evidence. **Overall acceptance: UNVERIFIED, with known
headroom and concurrency gaps.** Do not add unrelated features until the gaps
are closed. These scheduling requirements are not all implemented yet.

## Baseline and allocation ledger

Audited image: `04eba2bb76e4844f928a91567fa7a126b17fb150bd64c4a60302161e904e3f10`;
ELF: `c6a870f29f9ba7f5c48b30dddb6712a7a7bf09f3300cfd1751f5bd1361908da3`;
checkpoint: `24ac50cca60eba2aff72aa879c939c3edcd1dc9d`.
Target: ESP-IDF 5.5.3, ESP32-C3, no PSRAM.
Configuration/allocation sources: [defaults](../../sdkconfig.defaults),
[display BSP](../../components/bsp/src/bsp_display_lvgl.c),
[AI worker](../../main/xigua_ai.c), [application](../../main/xigua_app.c) and
[backend worker](../../main/xigua_backend.c). Recheck the effective build
configuration whenever these sizes change.

| Resource / owner | Size and evidence type | Lifetime / release / overlap |
| --- | --- | --- |
| LVGL / UI | Configured: 28,672 static pool | Application lifetime; measure pool separately |
| LCD / BSP | Configured: 19,200 single DMA buffer, 40 lines | Display lifetime; present in every active UI phase |
| Catalog / application | ELF/configured: 10,000 static + 10,000 refresh shadow | Static retained; release shadow after publish/failure; refresh overlap needs arbitration |
| Reply / application + AI | ELF/configured: 12,304 static copies + 4,104 queue payload | Retained structures; one valid mode/result owner; additional reading copies need counting |
| Records/care/undo / application | ELF: 6,912 static bytes, including two care transaction backups | Application lifetime; additional context/JSON copies need separate peak measurement; preserve NVS and undo correctness |
| Care/handoff text / application + backend | ELF: 7,680 static bytes | Application lifetime; JSON/context construction counted separately |
| AI / cloud / LVGL task stacks | Configured: 6,144 / 6,144 / 7,168 | Task lifetime; stack minima unverified |
| TTS PCM task stack | Configured: 4,096 | Playback only; release after worker exits |
| TLS content buffers | Configured: 16,384 RX + 4,096 TX | HTTPS lifetime; excludes certificate/RSA/TCP/client/parser allocations |
| JSON / AI + backend | Configured caps: Chat 16,384; ASR 4,096; backend 8,192 or catalog 16,384 | Request/response lifetime; include JSON trees, encoded payloads and growth copies |
| Temporary voice / AI | Configured: 2,097,152 Flash | Shared `voice_tmp`; microphone/TTS ownership must be exclusive; never play an incomplete cache |
| Full reply font / UI | ELF: 4,050,047 Flash data, zero static DRAM | Mapped constant; drawing/cache RAM is counted separately |

These are allocation components, not additive totals. The audited link-time
`.data + .bss + .iram.text` total is 206,174 bytes, including some entries above.
The runtime ledger still needs measured Wi-Fi/BLE, codec/I2S/DMA, JSON and LVGL
peaks; unknown entries must not be treated as zero.

Application Flash is 6,185,888 / 6,225,920 bytes: only 40,032 bytes remain.
Layout: NVS `0x9000/0x6000`, PHY `0xf000/0x1000`, factory `0x10000/0x5f0000`,
`voice_tmp` `0x600000/0x200000`. Any image growth requires an explicit fit and
future-update plan. Cache reduction or partition changes must account for
recording duration, speech capacity and preservation of existing data.

## Required phase schedule

UI/display and existing system/worker stacks remain resident unless explicitly
retired. The table describes required resource lifetimes, including preparation.

| Phase | Allowed heavy resources | Work to defer / transition requirement |
| --- | --- | --- |
| Idle UI | One background network job | Sync/catalog serialized from construction through cleanup |
| Microphone capture | Capture codec/I2S/DMA, Flash recording chunks | Defer sync/catalog/BLE; release audio before ASR TLS |
| ASR | One HTTPS client and bounded encode/parser buffers | No PCM DMA; retire request/client/buffers before chat |
| Q&A / Story generation | One HTTPS client, bounded request/reply JSON | No audio initialization; defer cloud/catalog/BLE; retire payload after upload and client before speech |
| TTS download | One HTTPS client, SSE/ADPCM/Flash cache buffers | No PCM worker/DMA; fully validate cache, then close/release networking before playback |
| Cached speech | Codec/I2S/DMA, PCM worker and bounded decode buffers | No TLS; defer heavy sync/catalog/BLE until speaker resources retire |
| Network media | Media client + PCM resources | Separate overlap budget required; current HTTP scheme does not validate future HTTPS |
| BLE provisioning | Bounded Wi-Fi/BLE provisioning resources | Defer AI/audio/sync/catalog; release radio tasks/handlers on close and measure recovery |

Small status/clock UI updates are permitted throughout but remain part of the
peak budget. Deferred sync must retain user data. Entry conflicts need a bounded
wait or retryable busy state; no partial reply/cache may become valid.

## Reserves and observed gaps

Initial planning targets: retain **20 KiB internal byte-addressable heap** at
peak and **2 KiB above the largest next allocation** in a suitable block.
These are provisional engineering margins for concurrent driver/UI activity
and fragmentation, not measured requirements or proof of capacity. Additional
future allocations must be budgeted on top. Do not lower targets just to turn
a failing sample into a pass; changes require worst-case device evidence.
DMA reserve and LVGL/stack margins are **UNVERIFIED** and must be quantified
from actual driver/pool/stack peaks before overall acceptance.

| Evidence | Free bytes | Largest block | Meaning |
| --- | --- | --- | --- |
| Current image, self-check before request | 63,404 | 51,200 | Phase sample; no peak claim |
| Current image, self-check TLS connected | 17,952 | 7,680 | Below planning reserve; original log used `MALLOC_CAP_8BIT`, not a new capability-specific trace |
| Previous `c2c6e764c004...`, physical Q&A TLS connected | 13,020 | 7,680 | Prior-version comparison only; DMA free 5,264 |

The 16 KiB chat body cap needs its full allocation/growth budget while TLS is
alive; a 7,680-byte block does not establish that this maximum can be reached.
Known gaps: cloud snapshot and catalog shadow allocation before the network
lock; media outside that lock; active BLE peak/recovery unknown; no continuous
largest-block trace or complete per-phase minimum-free evidence. Current
cache-first TTS passes host lifecycle tests; new-image audible Story acceptance
and repeated-workload memory acceptance remain unverified.

## Recovery order and acceptance

First cover preparation through cleanup with phase arbitration and complete
measurements. Then evaluate display buffering, compact catalog references and
reply ownership. The audit estimates 20–24 KiB of possible RAM savings; these
are unimplemented estimates with UI/backend/ownership tradeoffs. Preserve
recording, statistics consistency, Q&A and Story. Shrinking TLS blindly or
removing a Flash-resident font is not an accepted RAM fix.

Acceptance must record matching image/configuration and the framework metrics
at maximum recording/reply/list sizes; Q&A → Story → Q&A; read/pause/resume/replay;
cancellation and retry; Wi-Fi recovery; provisioning open/close; and deferred
sync recovery. As an initial stress workload, run at least 20 consecutive
completed microphone → ASR → Q&A/Story cycles and 10 provisioning open/close
cycles after warm-up.
This is a minimum regression workload, not a reliability guarantee. Require
stable equivalent-idle memory, documented margins, no allocation failures,
correct reply modes, audible speech and unchanged record/statistics integrity.

Runtime admission checks, complete phase arbitration, telemetry and numeric
DMA/LVGL/stack reserves remain implementation work. Document checks and a
successful firmware build do not close these gaps.
