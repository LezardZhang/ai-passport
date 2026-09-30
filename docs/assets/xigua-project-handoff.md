<p align="right">
  <a href="xigua-project-handoff.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Xigua Childcare Assistant Handoff

This is the current handoff for `feature/xigua-childcare`. It records the product direction, implemented modules, evidence from the latest device run, and the work still required. It contains no API key or Wi-Fi password.

## Product direction

The long-term product is a phone-independent childcare assistant on the ESP32-C3 FoloToy AI Passport. It should keep local childcare records offline, connect to a known Wi-Fi network automatically, capture a spoken request with the built-in microphone, send it to MiMo ASR and a language model, show a readable reply, and later play a TTS reply through the speaker.

Near-term work is to finish device voice acceptance, make the three-button information architecture clear, improve typography and long-response display, add an intentional deep self-test for ASR and TTS, and then implement TTS playback.

## Implemented modules

- `main/xigua_app.c` provides childcare records for feeding, diaper, sleep, bath, tummy time, and timers. Records are stored in NVS and the latest action can be undone. Wi-Fi has a status page and a “Search nearby Wi-Fi” action; the user selects an SSID from scan results and uses the three-page keyboard only for the password.
- `main/xigua_wifi.c` stores the last successful station configuration, scans at boot, and tries the three built-in networks before the previous saved network. The owner-authorized built-in profiles are tracked in `main/xigua_wifi_credentials.h`. When no candidate is visible, the device exposes a local scan-and-select flow instead of Bluetooth provisioning. Authentication expiry, authentication failure, association failure, and handshake timeouts retry up to three attempts. Stale BSSID locks are cleared, PMF is optional, power save is disabled during connection, and successful credentials are persisted.
- `main/xigua_ai.c` uses the configured OpenAI-compatible MiMo endpoint for text, ASR, and the model configuration list. `main/xigua_ai_credentials.h` stores the owner-authorized shared endpoint, key, and model settings so a fresh clone does not require repeating local setup. Voice capture is 16 kHz, 16-bit, mono WAV written to the `voice_tmp` partition, with a maximum of 60 seconds and chunked Base64 upload. Capture runs in a worker task, so button callbacks stay non-blocking.
- The AI worker runs an automatic health check after IP acquisition. It first performs a public HTTPS probe, waits for time synchronization, and then sends a minimal MiMo text request. Bluetooth provisioning is no longer started, leaving more heap for Wi-Fi and TLS. A successful result is cached for six hours; failures retry every two minutes. MiMo requests send the standard Bearer header plus the legacy `api-key` header, and non-2xx responses retain a bounded body preview in the log.
- `main/xigua_font_zh16.c` and `main/xigua_font_zh20.c` cover the current UI text, punctuation, and ASCII inventory. The larger font is used for primary Chinese text and recording/self-check messages. The LXGW WenKai license is kept beside the generated font.
- `partitions.csv` reserves `voice_tmp` for temporary recordings while keeping the application within the 8 MB flash layout.

## Wi-Fi and TLS root cause

The repeated connection problem was not caused by an incorrect password alone. Serial logs showed the access point `Lezard2.4G` reaching WPA authentication and returning `WIFI_REASON_AUTH_EXPIRE (2)`. A retry then associated using WPA2-PSK and received `192.168.50.115`. This is consistent with a transient mixed WPA2/WPA3 authentication handshake. The application now retries and normalizes the station profile, but the access point should still be tested with a fixed WPA2-PSK profile if the first-attempt failure matters.

The earlier `ESP_ERR_HTTP_CONNECT` error had a memory cause: Bluetooth provisioning competed with mbedTLS during RSA certificate verification. The current build does not start that service. Certificate verification remains enabled, and MiMo HTTP failures now log the status and a bounded response preview so network, authentication, and request-format failures can be distinguished.

Normal flashing overwrites the bootloader, partition table, and factory application from `0x0`, but NVS at `0x9000` is retained. Use the Wi-Fi clear action when stored credentials are suspect. Do not erase the whole chip as a routine repair because it removes user records.

## Latest verification

- Build: PASS. ESP-IDF 5.5.3, ESP32-C3, 8 MB flash. Verified archive: `build/firmware/e7e4f48e1c03c0f62063028e7e1c9dd7a3979e6838658a5a3aa020b1ebc6e455/`.
- Host/static tests: PASS, including repository checks and BSP host tests.
- Device flash and boot: PASS. The merged image was written and hash-verified over USB Serial/JTAG.
- Device Wi-Fi: PASS in the observed retry scenario. The device logged reason 2 once, retried automatically, connected to `Lezard2.4G`, obtained an IP, and started SNTP.
- Device HTTPS and MiMo text self-check: PASS. The secure log shows certificate validation, HTTP status 200 from the public probe, MiMo status 200, and `ESP_OK` from the text self-check.
- Most recent flash confirmation: PASS. The device connected directly with WPA3-SAE on `Lezard2.4G`, obtained `192.168.50.115`, validated the certificate, and completed the MiMo self-check without an HTTP or TLS error.
- Device voice ASR and TTS: NOT RUN end to end in this round. The capture and upload path is implemented, but a confirmed microphone phrase, ASR transcript, model reply, and speaker playback still need device acceptance.

## Worktree checkpoint (2026-09-30)

The latest Wi-Fi UI, built-in network, and shared MiMo configuration changes have passed compilation and merged-image verification. A local Git checkpoint is created before flashing. The earlier scan/select implementation was flashed with NVS preserved; the latest build is identified below.

- Wi-Fi code was changed toward a device-only flow: boot scan and saved-network retry remain; the Wi-Fi page now searches nearby networks, lets the user select an SSID, and opens the soft keyboard for the password. Manual SSID entry and Bluetooth provisioning were removed from the Xigua path.
- `main/xigua_wifi_security.c` and its header were removed from the Xigua build, and the NimBLE/BLUFI defaults and component dependency were removed from the application configuration. The reference baseline BLE demo files were not changed.
- MiMo requests now add the standard `Authorization: Bearer` header while retaining `api-key` compatibility, use `max_tokens` and `enable_thinking=false`, and log a bounded error-response preview for non-2xx replies. This is a code change, not a device-confirmed fix for the reported `ESP_FAIL`.
- Documentation was updated to describe the scan/select/password flow and the removal of Bluetooth provisioning.
- The Wi-Fi body uses 16 px text and a taller hint area; keyboard focus uses `< >` and mode/operation controls occupy separate rows. UI wording avoids the observed missing Chinese glyphs.
- The owner grants standing flash authorization: save a local Git source checkpoint and matching firmware/ELF/MAP before writing, then flash directly with NVS preserved. No per-flash approval is required.

Validation for this checkpoint:

- Verified merged-image SHA-256: `DB213323011A34E496F35DA5BD7AA8672421C5C345F0412FAAF9DA3D6E1CBCB6`. Matching firmware/ELF/MAP are archived under `build/firmware/db213323011a34e496f35da5bd7aa8672421c5c345f0412faaf9da3d6e1cbcb6/`; the ASCII build directory is `C:/aihw_build_src/build/validation/`.
- Repository check: PASS (`python tools/check_repo.py`).
- Deep-sleep contract tests: PASS (`python tests/test_deep_sleep_contract.py`).
- `tests/test_check_repo.py`: FAIL, five existing vendored-documentation assertions in `VendoredDocumentationTest`; this checkpoint did not modify those assertions.
- Firmware build: PASS. ESP-IDF 5.5.3 built the ESP32-C3 application, bootloader, partition table, and binary. Because the repository path contains Chinese characters and `ldgen` cannot resolve that path on this host, the same worktree was copied to an ASCII-only temporary path for the build.
- Merged firmware: PASS. `idf.py merge-bin` completed and `tools/verify_firmware.py` verified all three images, the partition table, the 8 MB flash bound, and the factory application placement.
- Full static gate: NOT RUN. `tools/validate.sh --static` could not run from this PowerShell host because Bash and a host C compiler are unavailable.
- Latest device flash and boot: PASS. Source checkpoint: `35034b86b169215ecad68e4fdf5a8cd05838f180`. The COM6 ESP32-C3 was flashed with segmented images from the archive above; write hashes passed and NVS was retained. A 40-second startup observation confirmed application boot, a scan with 10 networks, and an automatic attempt at built-in `GUANTANG_2.4G`.
- Latest device Wi-Fi: FAIL. Association returned `reason=4`, then the device entered local network search without obtaining an IP; MiMo requests therefore did not run. Source, image, ELF, and MAP are retained for restoration and diagnosis.
- Device UI/password entry, MiMo request, and voice request for this latest build: NOT RUN. They still require button observation and a successful connection.

Next, run the remaining device acceptance sequence: scan/select/password entry, reconnect after credential changes, MiMo self-check, and a real voice request. Preserve the pre-existing untracked quota files listed by `git status`.

## Keyboard and automatic Wi-Fi fix (2026-09-30)

- Uppercase and lowercase each show all 26 letters on one page. Digits/symbols use two pages with up to 30 keys each. Type switches, Backspace, Next page, and Done stay visible; focus uses cyan fill and a white outline.
- UP/DOWN moves on PRESS; DOUBLE moves one row from the gesture's starting key. Three or more rapid presses still move the cursor. Long UP deletes, long OK finishes, and long DOWN cancels.
- Association timeout reason 4 now gets three retries, with a rescan after 15 seconds when candidates are exhausted. All scan results are checked, so built-in APs are not limited to the first 16 UI entries; the network list itself uses five-entry pages. Disconnects reconnect; manual search cancels background automatic flow.
- Keyboard widgets are allocated only while editing and released on exit. Fixed UI Chinese glyph coverage passed for both 16 px and 20 px inventories.
- Build: PASS. Merged image and debug archive verified. SHA-256: `cb7792f755c82be4209df441ca78990ba0fbebc7b03edc7ebbe76429c16dab53`; archive: `build/firmware/cb7792f755c82be4209df441ca78990ba0fbebc7b03edc7ebbe76429c16dab53/`.
- Host tests: keyboard and Wi-Fi regression tests PASS using actual application logic; repository checks PASS. The complete gate was attempted but its actionlint installer does not support the current Windows Git Bash platform, so the gate is incomplete. The five earlier documentation-test assertion failures remain outside this change.
- Device tests: COM6 flash and startup PASS with NVS preserved; source checkpoint: `33fff9eb84774a465949095d89baac7e1bcc90b9`. About 43 seconds after startup the device automatically connected to `GUANTANG_2.4G` and obtained `192.168.10.214`. Earlier reason 4/205 failures triggered retries and a rescan instead of permanently stopping in network search. Initial connection is still slow: the log shows association failures at several channels for the same SSID before success. The bounded post-connect observation did not capture a MiMo self-check result and included a beacon-timeout probe message; sustained connection stability, screen layout, and physical buttons remain unverified.

## MiMo credential correction (2026-09-30)

The model self-check failure was caused by a different standard API key stored in the tracked header while using the Token Plan endpoint. The owner supplied the intended Token Plan key; it is now stored in `main/xigua_ai_credentials.h`, keeping the Token Plan endpoint unchanged. Both a minimal request and the firmware self-check payload returned HTTP 200 with `OK` from the host using the corrected key. Credential values are omitted here. Build, merged-image and archive verification PASS. Source checkpoint: `e6abf939cd0b79b0bc0014922ac1abb60dd97580`; merged SHA-256: `99bdff4dc34096ce29489283199292393ac6da2ce46c1e66d300c83ce869e207`. COM6 segmented flash preserved NVS; the matching ELF prefix `641f85239` booted, automatically connected to Wi-Fi, and completed the public HTTPS probe and MiMo text self-check with HTTP 200 / `ESP_OK`.

## Voice WAV header fix (2026-09-30)

A three-second device recording completed, but ASR returned HTTP 400 with a 134-byte body. The recording code first programmed a zero WAV header into NOR Flash, then tried to overwrite it with the final RIFF header without another erase. Zero bits cannot be restored by programming, so the audio header was invalid. A host request with a zero header reproduced HTTP 400 and the same 134-byte invalid-audio-format response; a valid WAV request using the same Token Plan key returned HTTP 200.

The recorder now leaves the erased header area untouched while writing PCM and programs the final header once. ASR failures log a bounded response preview. The new host regression compiles the actual capture functions against a NOR Flash model and checks RIFF lengths, PCM, minimum/maximum recordings, and an interrupted capture. It passes. Build, merged-image and archive verification PASS; source checkpoint: `0319500367447a53632e49ca6723206d77cacd71`, merged SHA-256: `0e204d4c23ebcda686816038506206cc9fc7ae895ea6d064ec20f2b2c5d0a643`, archive: `build/firmware/0e204d4c23ebcda686816038506206cc9fc7ae895ea6d064ec20f2b2c5d0a643/`. COM6 segmented flash and boot PASS with NVS preserved; the matching ELF prefix `524657437` automatically obtained `192.168.10.214` and completed MiMo text self-check with HTTP 200 / `ESP_OK`. The complete gate was attempted and is still blocked by unsupported Windows Git Bash actionlint installation. A new physical voice recording is pending user acceptance; text self-check does not validate ASR or microphone quality.

## Complete reply font (2026-09-30)

The owner confirmed the corrected voice path works. AI/story replies now use
the complete Source Han Sans SC 2.005 font: 44,853 source codepoints, 20 px,
2 bpp compressed, with every basic/Extension A Han character verified against
the original OTF. Source assets and generation tooling are tracked for fresh
clones. Fixed menu fonts retain their compact layout and use the full font as
fallback; the duplicate built-in Chinese subset is disabled. See
[font coverage, generation, and resource budget](xigua-full-font.md).

Build and merged/archive verification PASS. Application: 5,761,040 bytes;
factory free: 464,880 bytes. Merged SHA-256:
`da415083206835b7e9463f5cc2f00daffe10e96c6f40e30ca7007e67d8ed1000`.
Font coverage, voice NOR/WAV, keyboard, Wi-Fi regressions and repository checks
PASS. The complete gate was attempted but the actionlint installer still rejects
the Windows Git Bash platform. On-screen rendering and long replies remain
pending physical acceptance; the existing 192-byte reply buffer is unchanged.

## AI page interaction redesign (2026-09-30)

The owner confirmed the full-font firmware's core behavior works, then reported
that a short OK after a reply sent the canned service-check prompt and overwrote
the useful result. The UI no longer exposes that text request. Preparation,
recording, processing, paged reply reading, actions, and errors are separate
states. Short OK opens actions with Continue selected; Ask again returns to
preparation and requires a deliberate hold on the recording card. UP/DOWN press
pages the reply immediately. Failed requests retain the previous successful
reply. The buffer is now 1024 bytes with UTF-8-safe truncation and a partial-reply
indicator. The reader and action widgets exist only on the AI page.

Build and merged/archive verification PASS; app 5,762,928 bytes, factory free
462,992 bytes. Full image SHA-256:
`5cd1eaf5bdc2685149c90f2fc4783dbeadf9e139e9cea15d97845863fd40b30c`.
Matching ELF SHA-256:
`8df4d818bb02d9b171327fd500e5e9b7c14a4dc1cd5f7d2835c7515799aff40c`.
Reply protection, recording transitions, paging bounds, UTF-8, NOR/WAV capture,
full-font and new fixed-label coverage checks PASS. Repository checks PASS;
the complete gate remains blocked by the Windows actionlint installer.
COM6 segmented flashing and write hashes PASS, preserving NVS and voice data.
Source checkpoint: `4499fdac399ae4c79400b7cab36cee7b078be6a3`. The startup
ELF prefix `8df4d818b` matches the archive. Automatic built-in Wi-Fi connected
and obtained an IP at about ten seconds; public HTTPS and MiMo text self-check
returned HTTP 200 / `ESP_OK`. Initial TLS heap: 124,160 bytes free, largest
106,496 bytes. No reboot or allocation failure appeared in the bounded log.
Physical acceptance of the redesigned page is pending.

## Home menu and recording stack (2026-09-30)

The owner reported a reboot when recording seconds appear and requested the
AI page's background/card selection for the main menu. Overview now shows a
two-line summary and three menu cards per page, with seven entries on three
pages. UP/DOWN moves on PRESS without waiting for CLICK/DOUBLE; the selected
card shares the AI menu's cyan fill, dark text, and white outline.

The recording reboot has not yet been captured in serial output. Matching
compiler stack-usage reports showed 2128 bytes for `record_voice`, plus 560
for `voice_once` and 1440 for `ai_task`, before nested BSP/Flash calls on the
6144-byte worker stack. Moving the 2048-byte PCM scratch to a capture-lifetime
allocation reduces the capture frame to 80 bytes. All success/read/write error
paths release it; allocation failure returns `ESP_ERR_NO_MEM`. Capture logs now
include task stack high-water marks. This removes measured stack pressure;
the exact reboot cause remains unconfirmed without a panic or physical retest.

Build, merged/archive verification, menu navigation, AI interaction, NOR/WAV
capture (including allocation/write failure), keyboard, Wi-Fi, and full-font
checks PASS. The new labels are covered by the selected fixed fonts. The
complete gate was attempted; the Windows actionlint installer still blocks it.
App: 5,764,000 bytes; factory free: 461,920 bytes. Partition table unchanged.
Merged SHA-256:
`aa875be0e0cfbe9ddbd34ad484d67156376c1d1552ea3836810e81ba61d34046`.
Matching ELF SHA-256:
`47cbb2ae5b0e76b154a0fd0c11151682691f515b060fbf064a1c18aab3c075be`.
Archive: `build/firmware/aa875be0e0cfbe9ddbd34ad484d67156376c1d1552ea3836810e81ba61d34046/`.
Source checkpoint: `07289750efaf2633e8cdf53b9f95c2e3e143fcfb`. COM6
segmented flash and write hashes PASS, preserving NVS and the voice partition.
The startup ELF prefix `47cbb2ae5` matches the archive. Automatic built-in Wi-Fi
obtained an IP at about ten seconds; public HTTPS and MiMo text self-check
returned HTTP 200 / `ESP_OK`. Initial TLS heap: 124,144 bytes free, largest
106,496 bytes. No panic appeared during the bounded startup observation.
Physical recording and card rendering acceptance are pending.

## Post-recording model failure investigation (2026-09-30)

The owner reported model connection failure after releasing the recording key.
The running ELF prefix `47cbb2ae5` matches the verified `aa875be0...` archive
above. A bounded reboot observation again obtained Wi-Fi IP and HTTP 200 from
the public probe and MiMo text self-check. No new voice request was triggered
during the serial windows, so the board's failing stage/status remains unknown.

The retained voice partition contains a finalized 172,076-byte WAV: 5.376 seconds,
16 kHz mono, 16-bit PCM. Stub bulk reads encountered corrupt serial packets;
a ROM read successfully obtained the exact WAV without writing Flash. A host
replay using the firmware's ASR JSON framing and current configured credentials
returned ASR HTTP 200 in 0.89 seconds, followed by chat HTTP 200 in 6.12 seconds.
This validates the retained audio and current service access from the host; it
does not validate the board's upload, response reads, TLS memory, or the earlier
failure. No firmware change or flash was made for this investigation. Capture
the actual failed voice request before selecting a repair; keep private audio,
transcripts and credentials out of tracked notes and routine output.

## Voice records, plain replies and background sleep (2026-09-30)

The owner confirmed a recent real recording returned normally, then requested
longer plain replies, actual voice-driven local records, and sleep independent
of its page. The preset now describes seven local commands: feeding, diaper,
completed sleep, bath, tummy time, start sleep and end sleep. Completed facts
produce one-action JSON; questions and missing information produce prose.
The worker strictly validates JSON and saves NVS before showing a local success
message. Failed persistence restores the previous state. Invalid, truncated or
multiple commands never become records. Manual and voice sleep share this path.

Overview now has Start sleep / End sleep as one top-level toggle. Sleep continues
while using other pages, recording feeding, or running the independent timer.
Each completed sleep now keeps exact start and end timestamps as well as minutes.
The active session and completed sessions have separate Today pages. The original
state/event blob remains compatible; exact pairs use a separate versioned NVS blob
keyed by event slot. Old records remain readable and may have unknown starts.
Within one boot duration uses monotonic time; across reboot it uses trusted
timestamps or reports uncalibrated duration. The existing NVS end timestamp's
`-1` sentinel preserves structure size; old active sleep migrates. Older firmware
does not recognize an ongoing background sleep when rolled back.

Replies now have 4096 bytes and a 1024-token budget. Local UTF-8 clipping and
server `finish_reason: length` both mark partial replies. Chat I/O timeout is
45 seconds. Presets request compact complete prose without Markdown/emoji/blank
lines. A real service test of five semantic cases passed HTTP 200: milk record,
missing amount, feeding question, sleep start and sleep end. A repeated story
test still returned a blank line; display cleanup now collapses blank lines and
removes common Markdown markers and emoji after command validation. No model
format guarantee is assumed. Synthetic prompt tests ran on the host, not the board.

Focused host checks PASS: transaction validation/NVS rollback, background sleep
and reboot timing, long replies/UTF-8/format cleanup, AI interaction, menu,
keyboard, voice NOR/WAV and full-font coverage. The transaction test compiles
real application functions with JSON-tree/NVS/clock doubles; it does not test
the upstream JSON parser. Repository checks PASS. The complete gate was attempted
and remains blocked by the actionlint installer's unsupported Windows Git Bash
platform. ESP-IDF 5.5.3 build and merged/archive verification PASS using the
verified ASCII source mirror. Stack frames: capture 80, voice request 560, AI
worker 432, result consumer 32 and local record transaction 688 bytes; 4 KiB
reply buffers stay off the task stacks.

App: 5,775,616 bytes; factory free: 450,304 bytes. Partition table unchanged.
Archive: `build/firmware/1eabfcedf481db8fc6859e21128ad6933884303517fc736e4fb5e4ff9cec4127/`.
Full-image SHA-256:
`1eabfcedf481db8fc6859e21128ad6933884303517fc736e4fb5e4ff9cec4127`.
Matching ELF SHA-256:
`3001c19686c253814a851cccffe2720e257c8f9bb4061d96cf79b7b66149c632`.
The source/configuration checkpoint is created before the authorized segmented
COM6 write, preserving NVS and voice data.

Source checkpoint: `13a36def9f4a41684b10cd62116d9ba41a69c781`.
COM6 segmented flashing and write hashes PASS. A bounded startup observation
matched ELF prefix `a079e0897`, connected the built-in Wi-Fi and obtained IP at
about ten seconds. Public HTTPS and MiMo text self-check returned HTTP 200 /
`ESP_OK`. Initial TLS heap: 110,444 bytes free, largest block 98,304 bytes. No
panic or allocation failure appeared in this window. Physical sleep navigation,
voice record persistence and formatted reply rendering remain pending acceptance.

## Remaining work

The menu and AI reader have been redesigned, but font size and reading comfort on the 240x320 display still need physical review. The reply buffer is 4096 bytes with paging; replies beyond the local or server limit remain partial. Physically verify voice records against Today, plain reply rendering, and sleep start → other page → sleep end, including a reboot during sleep.

The voice path needs a real short-phrase test, then 20–30 second and 60 second recordings, with capture duration, free heap, ASR status, transcript length, and model reply recorded. TTS playback and audio format conversion are not implemented. The automatic self-check intentionally avoids ASR and TTS usage; a user-controlled deep check should be added later.

Keep extending the existing Wi-Fi, voice, AI interaction, and glyph-coverage regressions when new behavior is added. Repeated boots should be tested against the same access point to quantify the first-attempt authentication failure rate.

## Handoff steps

Read `AGENTS.md`, the five required passport skills, the Wi-Fi provisioning guide, and this document before changing the firmware. This private repository may use the owner-authorized tracked Wi-Fi and MiMo configuration; do not print those values in logs or handoff text. Run `./tools/validate.sh --static`, `./tools/validate.sh --firmware`, and the complete gate before delivery. For a device run, capture serial logs from boot through IP acquisition, health check, and voice request; save a Git checkpoint and verify the matching archive before flashing. Use its component images at the recorded offsets to preserve NVS and recording data under the standing flash authorization.
