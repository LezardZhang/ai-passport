**English** · [简体中文](xigua-m3.zh_CN.md)

# Xigua M3 local sound increment

This stage adds three synthetic ambient sounds, session volume and timer-alert arbitration to the [connectivity and records stage](xigua-m2.md). It is an implementation checkpoint, not device acceptance. The original M3 was flashed and answered USB status queries. The user reported quiet, similar sounds; the revision below addresses the software causes. Revised physical acceptance remains pending.

## Controls

From the home Sound card, OK opens the sound page. UP/DOWN selects white noise, rain or waves without starting playback. OK plays the selected sound; OK on the currently requested sound pauses it. Long OK opens volume, UP/DOWN adjusts by five percentage points, and OK returns. Long DOWN returns to the previous page; leaving the sound page or dimming the display does not stop playback. Long UP retains the quick-feed shortcut.

Volume ranges from 0 to 100, initially 40. The earlier ceiling of 40 mapped to about -30 dB on the codec, leaving little output even at the UI maximum. The full codec range is now reachable. This is a software setting, not a measured acoustic level. Volume and selection are session settings: reboot starts paused with the default volume. No automatic background playback occurs at boot. The three labels describe synthesized textures, not recordings or clinically evaluated sound products.

## Generation and ownership

[xigua_sound.c](../../main/xigua/xigua_sound.c) generates signed 16-bit mono PCM at 16 kHz. White noise is deterministic pseudorandom noise; rain combines filtered noise with irregular resonant droplets; waves combine a low roar and midrange surf with a deep eight-second swell. A one-second alert contains three enveloped rising tones. The generator uses 36 bytes of state, no heap, and limits sample magnitude to 24,000 before output-volume control. It does not load a complete audio file or require an audio partition or external asset license.

One application worker exclusively owns BSP codec/I2S calls. It renders 320 samples per chunk (640 bytes, 20 ms); the worker stack is 4 KiB. Codec setup is lazy, on the first play or timer alert. Buttons only change bounded desired state and notify the worker. LVGL receives copied status; audio never owns page objects. Background playback continues independently of navigation.

Pause/stop suspends the codec through the BSP after the current operation. BSP sleep releases codec objects but retains I2S channels and their DMA allocation; the worker remains alive. This is bounded retained infrastructure, not complete audio subsystem deallocation. Codec failures remain visible and explicit play retries are possible. There is no automatic background retry loop.

## Timer and failure behavior

A newly due local timer requests one finite alert. The alert temporarily preempts ambient sound, then returns to the latest requested background state. Pausing during the alert cancels both the alert and background continuation. Clearing/restarting the timer dismisses its alert. Undo of an unrelated record does not retrigger an already-due timer. Volume zero mutes audio, while the visible timer indication remains.

Requests coalesce to the latest desired state; stop cannot be dropped by a full command queue. Generation/alert identifiers prevent old completions from consuming a replacement alert or overwriting newer intent. Codec writes use the dependency's 1,000 ms wait timeout, so stop is not an instantaneous cancellation guarantee; codec sleep and scheduling add latency. Startup/resume uses fades, but click-free transitions require listening on the board.

CONFIG_I2S_ISR_IRAM_SAFE=y keeps the DMA interrupt serviceable during Flash writes. It does not keep a Flash-resident producer running or prove glitch-free playback. Existing NVS layout, event schema and record capacity are unchanged. Concurrent TLS/audio heap headroom remains a required measurement.

## Diagnostics and acceptance

The existing USB status command reports audio availability, playing/alerting, selected track, requested volume, last error, maximum observed feed gap and worker stack minimum. A zero stack sample before playback is not a measured margin. Feed gaps are software timing observations, not acoustic dropout detection. Heap counters remain available in the same status response.

Host checks cover deterministic/chunk-invariant synthesis, peak bounds, invalid inputs, alert completion, continuous profile statistics, wave boundaries and playback arbitration. Fixed UI glyphs are checked separately. These cannot verify ES8311 output, audible timbre, speaker level or hardware timing.

Device acceptance still requires at least 20 minutes of playback while navigating, saving records and running timers; repeated play/pause/track changes; stop during alerts; mute; codec failure/retry; correct rate/pitch; no growing allocation loss; and heap/task-stack observations during network activity. ASR, TTS, PTT, night mode and cloud audio remain later stages. Full A-release acceptance also still requires the pending M0-M2 device checks and sustained network/audio concurrency.


## On-device Wi-Fi configuration

Open More, Settings, then press OK for Wi-Fi. Scan lists up to eight visible networks; choose one or enter an SSID manually. Edit SSID/password, then select Save and connect. Saving explicitly replaces the matching profile or uses an empty slot; a full eight-profile list requires USB management. The record store and AI key are unchanged. Connection status distinguishes a saved configuration from an acquired IP address.

The 5-by-5 software keyboard uses four rows for characters and a fixed final row for lowercase, uppercase, numbers/symbols, backspace and confirm. UP/DOWN selects the previous/next cell and OK activates it. Long OK changes the character page within the active set; long DOWN cancels without occupying a visible key. SPC at the end of each set inserts a space. All printable ASCII is available, including case-sensitive letters and password punctuation. Manual SSIDs allow 32 bytes; passwords accept empty for open networks, 8–63 characters or 64 hexadecimal digits. Passwords stay masked. Leaving the editor clears its password copy; canceling a keyboard edit leaves the previously accepted field unchanged. Record shortcuts are disabled within provisioning.

Scanned UTF-8 SSIDs preserve their original bytes for connection; non-ASCII bytes are displayed explicitly as hex escapes because the fixed UI font does not cover arbitrary network names. Scanning, persistence and connection run on background workers. Network failures allow editing and explicit retry. No Wi-Fi password or API key is compiled into firmware.
