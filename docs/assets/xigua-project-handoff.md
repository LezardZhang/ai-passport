<p align="right">
  <a href="xigua-project-handoff.zh_CN.md">简体中文</a> · <strong>English</strong>
</p>

# Xigua Childcare Assistant Handoff

This is the current handoff for `feature/xigua-childcare`. It records the product direction, implemented modules, evidence from the latest device run, and the work still required. It contains no API key or Wi-Fi password.

## Product direction

The long-term product is a phone-independent childcare assistant on the ESP32-C3 FoloToy AI Passport. It should keep local childcare records offline, connect to a known Wi-Fi network automatically, capture a spoken request with the built-in microphone, send it to MiMo ASR and a language model, show a readable reply, and later play a TTS reply through the speaker.

Near-term work is to finish device voice acceptance, make the three-button information architecture clear, improve typography and long-response display, add an intentional deep self-test for ASR and TTS, and then implement TTS playback.

## Implemented modules

- `main/xigua_app.c` provides childcare records for feeding, diaper, sleep, bath, tummy time, and timers. Records are stored in NVS and the latest action can be undone. Wi-Fi has a status page, built-in-network status, manual SSID/password entry, and a three-page keyboard: uppercase, lowercase, and combined digits/symbols.
- `main/xigua_wifi.c` stores the last successful station configuration, scans at boot, and tries the previous network before the three built-in networks. It falls back to manual or BLE provisioning when no candidate is visible. Authentication expiry, authentication failure, association failure, and handshake timeouts retry up to three attempts. Stale BSSID locks are cleared, PMF is optional, power save is disabled during connection, and successful credentials are persisted.
- `main/xigua_ai.c` uses the configured OpenAI-compatible MiMo endpoint for text, ASR, and the model configuration list. Voice capture is 16 kHz, 16-bit, mono WAV written to the `voice_tmp` partition, with a maximum of 60 seconds and chunked Base64 upload. Capture runs in a worker task, so button callbacks stay non-blocking.
- The AI worker runs an automatic health check after IP acquisition. It first performs a public HTTPS probe, waits for time synchronization, and then sends a minimal MiMo text request. BLUFI/NimBLE provisioning is stopped for the check and can be resumed from the Wi-Fi page, reclaiming heap for TLS. A successful result is cached for six hours; failures retry every two minutes.
- `main/xigua_font_zh16.c` and `main/xigua_font_zh20.c` cover the current UI text, punctuation, and ASCII inventory. The larger font is used for primary Chinese text and recording/self-check messages. The LXGW WenKai license is kept beside the generated font.
- `partitions.csv` reserves `voice_tmp` for temporary recordings while keeping the application within the 8 MB flash layout.

## Wi-Fi and TLS root cause

The repeated connection problem was not caused by an incorrect password alone. Serial logs showed the access point `Lezard2.4G` reaching WPA authentication and returning `WIFI_REASON_AUTH_EXPIRE (2)`. A retry then associated using WPA2-PSK and received `192.168.50.115`. This is consistent with a transient mixed WPA2/WPA3 authentication handshake. The application now retries and normalizes the station profile, but the access point should still be tested with a fixed WPA2-PSK profile if the first-attempt failure matters.

The later `ESP_ERR_HTTP_CONNECT` error had a separate memory cause. BLUFI/NimBLE remained active while mbedTLS verified the RSA certificate, leaving an 18 KB largest heap block and producing an mbedTLS RSA allocation failure. Releasing provisioning before the health check increased the largest block to 32 KB. The latest secure run validated the certificate and completed both the HTTPS probe and MiMo request with `ESP_OK`. The final firmware does not skip certificate verification.

Normal flashing overwrites the bootloader, partition table, and factory application from `0x0`, but NVS at `0x9000` is retained. Use the Wi-Fi clear action when stored credentials are suspect. Do not erase the whole chip as a routine repair because it removes user records.

## Latest verification

- Build: PASS. ESP-IDF 5.5.3, ESP32-C3, 8 MB flash. Verified archive: `build/firmware/e7e4f48e1c03c0f62063028e7e1c9dd7a3979e6838658a5a3aa020b1ebc6e455/`.
- Host/static tests: PASS, including repository checks and BSP host tests.
- Device flash and boot: PASS. The merged image was written and hash-verified over USB Serial/JTAG.
- Device Wi-Fi: PASS in the observed retry scenario. The device logged reason 2 once, retried automatically, connected to `Lezard2.4G`, obtained an IP, and started SNTP.
- Device HTTPS and MiMo text self-check: PASS. The secure log shows certificate validation, HTTP status 200 from the public probe, MiMo status 200, and `ESP_OK` from the text self-check.
- Device voice ASR and TTS: NOT RUN end to end in this round. The capture and upload path is implemented, but a confirmed microphone phrase, ASR transcript, model reply, and speaker playback still need device acceptance.

## Remaining work

The font coverage is improved, but the text is still too small for comfortable use on the 240x320 display. The menu hierarchy, focus indication, back navigation, and bottom hint line need a deliberate redesign rather than more labels. Long model replies need scrolling or paging and UTF-8-safe truncation.

The voice path needs a real short-phrase test, then 20–30 second and 60 second recordings, with capture duration, free heap, ASR status, transcript length, and model reply recorded. TTS playback and audio format conversion are not implemented. The automatic self-check intentionally avoids ASR and TTS usage; a user-controlled deep check should be added later.

Add host tests for the Wi-Fi and voice state machines and an automated glyph-inventory check. Repeated boots should be tested against the same access point to quantify the first-attempt authentication failure rate.

## Handoff steps

Read `AGENTS.md`, the five required passport skills, the Wi-Fi provisioning guide, and this document before changing the firmware. Keep credentials in ignored local headers. Run `./tools/validate.sh --static`, `./tools/validate.sh --firmware`, and the complete gate before delivery. For a device run, capture serial logs from boot through IP acquisition, health check, and voice request; flash the verified `full.bin` at `0x0` only after reviewing the resulting build archive.
