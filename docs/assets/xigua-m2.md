**English** · [简体中文](xigua-m2.zh_CN.md)

# Xigua M2 connectivity and AI text increment

This stage extends the [M0-M1 prototype](xigua-m0-m1.md) with USB runtime configuration, eight Wi-Fi profiles, SNTP synchronization a MiMo text request entry point, local-day statistics and consistent USB record export. Full M2 and the voice release remain incomplete. The user is remote and has deferred physical acceptance; this iteration does not flash, open the device port or call a real paid API.

## Implemented behavior

- A network worker owns Wi-Fi STA. Lower priority values connect first; equal priorities rotate, failures back off exponentially, and stable connections do not roam for stronger RSSI. Authentication, missing-AP and other failure reasons remain available as status codes.
- A successful configuration reply means persisted, not proven credentials or service access. The reply includes saved and reboot_required; restart to apply when the latter is true.
- Absolute time is trusted only after first SNTP synchronization in each boot. Temporary disconnection retains that boot's clock. Unknown endpoints from the same boot can be reconstructed and persisted; uncertain cross-boot intervals retain their existing semantics.
- USB supplies the timezone manually; an empty value is unconfigured. Supported forms include fixed POSIX TZ (CST-8, UTC0) and explicit M-rule DST (EST5EDT,M3.2.0/2,M11.1.0/2). IANA names and automatic location are unsupported. The Today page uses local calendar-day boundaries after clock and timezone are available; UP/DOWN switches to cumulative totals.
- Settings show network, time, configured AI and the latest request result. AI answers appear in the computer's USB tool; the screen does not claim arbitrary Chinese answer coverage.
- AI requests run in a separate worker without holding the LVGL lock. Buttons and records remain on the original controller. Insufficient memory returns low_memory before TLS; actual device peaks still need measurement.

## USB tool

The computer needs Python and pyserial. After authorized installation and port identification, use [xigua_device.py](../../tools/xigua_device.py). COM6 below is only an example.

```text
python tools/xigua_device.py --port COM6 status
python tools/xigua_device.py --port COM6 export --output xigua-records.json
python tools/xigua_device.py --port COM6 wifi-set --slot 0 --ssid Home --priority 0
python tools/xigua_device.py --port COM6 time-set --timezone CST-8
python tools/xigua_device.py --port COM6 ai-set --endpoint https://api.xiaomimimo.com/v1/chat/completions --model mimo-v2.6-flash
python tools/xigua_device.py --port COM6 test-ai
python tools/xigua_device.py --port COM6 ask "Reply hello briefly."
python tools/xigua_device.py --port COM6 wifi-clear --slot 0
python tools/xigua_device.py --port COM6 ai-clear
```

Passwords and API keys are entered through hidden prompts, not command-line arguments. SSIDs allow 32 UTF-8 bytes. Passwords are empty for open networks, 8-63 printable ASCII characters, or a 64-digit hexadecimal PSK. Slots range from 0 to 7 and priorities from 0 to 255; --disabled saves a disabled profile. API keys allow 191 bytes and model names 63. The endpoint must be a complete HTTPS request URL without user information, query, fragment or control characters. Authentication uses MiMo's api-key header; providers requiring Bearer are not automatically compatible.

Only test-ai/ask makes an explicit, potentially billable request. Configuration does not test AI automatically. The tool never automatically retries; a timeout has an ambiguous outcome, so inspect status first. It disables DTR/RTS before opening and does not intentionally reset the board. USB management assumes trusted local access; it is not remote administration. Invalid id/op, unknown or duplicate fields, malformed JSON and invalid UTF-8 are rejected. Do not forward the management port to an untrusted network.

## AI protocol and limits

Following the [official MiMo documentation](https://mimo.mi.com/docs/zh-CN/quick-start/summary/model), the candidate default is mimo-v2.6-flash at https://api.xiaomimimo.com/v1/chat/completions. Requests use api-key, stream=false, thinking.type=disabled and max_completion_tokens=256.

Input is limited to 512 UTF-8 bytes, response JSON to 8 KiB and returned text to 1,024 bytes. Truncation preserves UTF-8 boundaries and is marked with truncated. DNS, TLS and HTTP share a 30-second deadline; headers and total wire data are bounded. Certificates and hostnames are verified, redirects are not followed and requests are not retried. HTTP statuses such as 401/429 remain available; raw server error bodies and credentials are not returned on failure.

Requests are single-turn without conversation history. No model tools are enabled; tool responses are rejected, and an answer claiming to have saved something cannot change records. ASR, TTS, PTT, Jev and an on-device chat page are not included. Real account access, quota, DNS/TLS reachability and model responses remain unverified.

## Configuration and data

Partition offsets and the 128-event capacity remain unchanged from M0-M1. Runtime configuration uses the settings namespace in xigua_data; records remain in the xigua namespace. Configuration uses versioned JSON, alternating NVS slots and CRC. It is published only after successful commit; corruption or ambiguous commit enters read-only mode without automatic erase. Network changes preserve records; ai-clear preserves Wi-Fi.

Keys are not compiled into source or firmware constants. NVS encryption is not enabled. Clearing credentials is not physical secure erasure: previous slots or NVS pages may retain old values. Do not distribute raw device Flash dumps.

The schema-1 maximum snapshot is 8,975 bytes (128 events and 16 replay entries), checked by a host assertion. Scratch buffers are limited to 9,216 bytes each instead of 20,000. Wi-Fi IRAM speed optimizations are disabled; static RX buffers are 4 and dynamic RX/TX limits are 8, with RX BA window 4. This trades peak throughput for RAM; runtime headroom still requires measurement.

## Validation and remaining work

Run tools/validate.sh. Host checks cover configuration validation/roundtrips, strict JSON, Wi-Fi policy, AI response parsing, and fake-serial correlation/timeouts/short writes. Protocol tests use unmodified cJSON.c/.h and the MIT LICENSE in tests/vendor/cjson, from the [cJSON commit pinned by ESP-IDF 5.5.3](https://github.com/DaveGamble/cJSON/tree/c859b25da02955fef659d658b8f324b5cde87be3). This copy is test-only; firmware links IDF's json component. Static tests do not require IDF installation.

Actual build results and artifact hashes are in the iteration handoff. Compilation does not establish physical acceptance. Remaining device checks include two-AP failover, wrong passwords, offline/synchronization behavior, credential persistence under power loss, repeated USB connections, actual API access, input responsiveness and memory peaks. BLUFI, automatic timezone, complete daily statistics, local sounds, voice recording/playback and validated tool execution remain later work.

## Local-day statistics

The Today page includes bottle count/volume, diapers, baths, sleep and tummy time. Its default is the current local date; UP/DOWN switches between today and cumulative totals, and OK opens all saved records. An unavailable clock or timezone produces an explicit unavailable page, not an exact zero. Unknown-day records are counted separately and excluded from daily totals. Cumulative totals retain known counts/durations.

Day boundaries use two calendar midnights under the configured POSIX TZ, rather than adding 86,400 seconds; DST days may contain 23 or 25 hours. Ongoing and completed sleep/tummy intervals are clipped to the day. Within one boot, duration stays monotonic and is allocated from the recorded start anchor, so a later wall-clock correction cannot manufacture duration. Unknown endpoints or unresolvable intervals remain pending. Changing timezone recomputes boundaries without rewriting historical UTC. These remain estimates when the original timestamps were reconstructed.

## Record export

The export command reads every retained event without changing the device. It includes event IDs/revisions, type/value, active state, start/end clocks and their quality, and saved duration/quality. The file format is xigua-records-v1; 64-bit timestamps and durations are decimal strings to preserve integer precision. It excludes Wi-Fi/AI credentials, settings, timers and undo/replay internals. This is a readable record archive; a restore/import command is not implemented.

The protocol uses records.begin, records.item and records.finish. Each item and finish presents the initial boot ID, state revision and record count; the controller reads one bounded record at a time. Any record mutation or time reconstruction invalidates the token and returns snapshot_changed. The client does not automatically retry; rerun the command after recording activity settles. A final check detects a change after the last item. The computer publishes the JSON file only after validating the complete sequence, refuses an existing destination, and leaves no partial destination on timeout or failure. Raw corrupt/unreadable storage is not exported as an empty archive. A recovered, validated read-only snapshot can still be exported.

An active event contains the stored start and duration fields, not an invented finalized end or a continuously refreshed elapsed duration. Treat record files as personal data. Actual USB export and power-loss recovery on the board still require device acceptance.

Local sound is implemented in the subsequent [M3 increment](xigua-m3.md); its board acceptance remains pending.
