<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Xigua childcare backend

This service stores childcare events from the ESP32-C3 assistant and exposes a
small audio catalogue for songs, educational stories, classical early-learning
music, and white noise. It uses SQLite for a
single Tencent Cloud server and keeps the database and media files in `/data`.

Run locally:

```bash
cd backend
python -m uvicorn app.main:app --reload
```

For deployment, copy `.env.example` to `.env`, set separate random tokens for
the device, admin API, Hermes, and public read roles, set the admin password and
`CHILDCARE_PUBLIC_BASE_URL`, and run `docker compose up -d --build`. The
container exposes port 8000 and persists SQLite plus media in the named
`childcare-data` volume. Put audio files under `/data/media`, register them with
`POST /v1/audio/tracks`, and let the device consume the returned `play_url`.
Put a reverse proxy with HTTPS in front of port 8000 before using the API
outside the server. The management page is `/admin`.

All `/v1` routes accept either `Authorization: Bearer <token>` or
`X-API-Key: <token>`. Device uploads use `CHILDCARE_DEVICE_TOKEN` and
`X-Device-Id`; admin writes use the admin token or the console session; Hermes
uses `CHILDCARE_HERMES_TOKEN`; external read/export uses the public-read or
Hermes token. Event `id` is the device idempotency key, so retrying a batch is
safe. The event types mirror the firmware: `feeding`, `diaper`, `sleep`,
`bath`, `tummy`, and `timer`. Every event has `occurred_at`; a feeding record
therefore includes the exact time point as well as `amount_ml` and `ingredient`.

Hermes should read `GET /v1/hermes/children/{child_id}/analysis-input` for a
stable export (`schema=xigua-childcare-export-v1`) and may add catalogue
metadata with `POST /v1/hermes/audio/tracks`. To add a file, upload bounded
audio bytes to `PUT /v1/hermes/audio/files/{file_name}` and then register that
file as a track. It cannot write childcare events.
The returned `audio_tracks` contain metadata and a playable URL, never server
credentials or raw database paths.

The first client flow is:

1. Create a child with `POST /v1/children`.
2. Register a device with `POST /v1/devices/register`.
3. Push local NVS records in batches.
4. Read `/v1/children/{id}/summary` and `/events` from a phone or admin UI.
5. List `/v1/audio/tracks?category=song`, `story`, `classical`, or `white_noise`
   and stream the returned URL. Classical entries are limited to Bach, Mozart,
   Beethoven, and other public-domain or explicitly licensed works; the product
   does not seed easy-listening arrangements such as Richard Clayderman albums.

## Deployment preparation (2026-10-01)

The inspected server runs Ubuntu 22.04.4 LTS on x86_64, Python 3.10.12,
Docker 27.0.3 and Compose 2.28.1. The terminal runs as root in `/root`.
Approximately 44 GB disk and 4.7 GiB memory were available. Existing services
include 1Panel, OpenResty, PostgreSQL and other application containers; ports
80, 443 and 8090 are in use. Port 8000 had no listener. No Git checkout was
found within four directory levels under `/opt`, `/srv`, `/root` and `/home`.
This bounded search does not rule out checkouts elsewhere.

Use a dedicated checkout such as `/opt/ai-passport` (not yet created). Compose
binds the API to `127.0.0.1:8000`; configure the existing OpenResty proxy to
forward a chosen HTTPS hostname to that address. Python runs inside the 3.12
container. Before deployment, choose the hostname, configure DNS and TLS,
provide role tokens/password/session secret in `.env`, and confirm the proxy
can reach the host loopback endpoint. The inspected OpenResty container uses
`host` networking, so this loopback upstream is suitable. Its site configuration
is mounted from `/opt/1panel/www/conf.d`; manage it through 1Panel. Do not reuse occupied application ports.

No server files, services or firewall rules were changed during investigation.
Deployment, remote image build/pull, proxy configuration, backups and online
acceptance remain pending. Back up SQLite using its backup API (including WAL
consistency) and retain media before upgrades; do not remove the data volume.

Daily summaries use the child's IANA timezone with inclusive local midnight
and exclusive next midnight boundaries. Invalid timezones and dates are
rejected. Device tokens may read audio lists and individual tracks, but cannot
read child events or analytics exports.

Use Python 3.12. Run backend regression tests independently of the firmware toolchain:

```bash
python -m pip install -r backend/requirements.txt pytest httpx
python -m pytest backend/tests -q
```

Unified deployment and compatible migration are described in [the deployment guide](deploy/README.md).

## Single-child workspace

Unified login uses the existing Cloud Backup admin Key. One child and one device are selected automatically, without registration. The console contains feeding and sleep records, daily charts, complete CSV/JSON export, audio management, backup files and historical versions, application keys and full archives. Hermes has separate configured Skill downloads.

The login Key field uses visible text for phone keyboard and paste support.
Automatic capitalization and correction are disabled. Submission removes only
leading and trailing whitespace; the complete original Key is still required.

Firmware uploads revisioned snapshots of its 32-record NVS ring. Stable sequences handle retry, reboot and undo; the server retains uploaded history after ring rollover. Previously overwritten records cannot be recovered, and prolonged offline use beyond ring capacity can lose unsynchronized entries. Uncalibrated timestamps are retained but excluded from dated statistics. Sleep intervals split at local midnight.

Download device configuration to `main/xigua_backend_config_local.h` before building. Audio requires 12 kHz mono 16-bit PCM WAV, up to 25 MB. The device catalog includes songs, educational stories, classical early-learning music, and white noise. Playback status comes from device acknowledgments. `backend/seed-audio/manifest.json` records the content category and attribution for reproducible imports; run `backend/deploy/import_nursery.py` only after generating or supplying the listed WAV files.

Deleted records move into recoverable storage and stop contributing to statistics and exports. Device/manual retries cannot resurrect them. Restore individual entries from the records page. Nursery catalog entries retain creator, source, license and format-conversion attribution.

The nursery device controls support play, pause, resume and stop. Pause retains the playback position and releases audio ownership; starting AI, recording or another audio workflow ends the nursery session. The device command ACK includes `paused`, so the console distinguishes an issued request from device acceptance.

## Record-aware assistant and caregiver handoff

The authenticated device endpoint `GET /v1/device/context` returns a bounded
`xigua-care-context-v1` summary: current snapshot revision, generation time,
seven local-calendar days of calculated totals, latest known feeding/sleep/
diaper records, and the parent note. Device tokens can read only this paired
family summary; they do not gain access to arbitrary children's history.
Unknown timestamps are counted and excluded from calendar-day totals.

The workspace's Caregiver handoff page uses `GET /admin/api/care`. An admin
saves or clears its persistent note with `PUT /admin/api/handoff` and
`{"note":"..."}` (maximum 160 characters). Notes are displayed as text and
passed to the assistant as data, never as device commands. A newer device
snapshot can correct feeding amount, time and ingredient in place without a
new event or stale retry reverting it. Install the updated backend alongside
the firmware to enable the seven-day context and cloud notes. One-shot
reminders are created, managed and persisted on the device; this increment
adds no phone notification service or reminder scheduling endpoint.

## Android Bluetooth Wi-Fi provisioning

The console's **Phone Bluetooth provisioning** page (`/console#phone`) connects
directly to the application BLE service. On the device, open **Wi-Fi provisioning
→ Phone Bluetooth provisioning** first. Android Chrome then chooses the nearby
`Xigua-` device and asks for the six-digit code shown on its screen. Each device
window lasts at most five minutes; exiting the device page or ending the window
stops BLE and resumes AI/cloud work. NFC opens the page; the passive tag cannot
start the device's provisioning window.

The phone can scan 2.4 GHz networks, enter a hidden SSID/password, or select an
existing built-in network. Built-in passwords stay on the device. Submitted
passwords go only through authenticated BLE and are cleared from the form;
they are not stored in the browser or sent to the backend. A new network is
persisted only after obtaining an IP; failed joins retain the last saved
configuration. API-key/model editing and iPhone provisioning are not included.

Production entry: `https://162.14.108.234/cloud-backup/console#phone`. The prior
HTTP NFC login address redirects to HTTPS, so the tag does not need rewriting.
`XIGUA_PHONE_HTTPS_URL` optionally sets the canonical HTTPS console URL. The
reverse proxy must preserve `X-Forwarded-Proto: https`, and Uvicorn must trust
only that proxy's private source. Original device/backup HTTP protocols remain
compatible. The Bluetooth script is an authenticated `/console/api/bluetooth.js`
asset and contains no management Key.

Run `node tests/test_xigua_ble_client.js` for the actual browser client's framing,
ACK, disconnect/reconnect and console-script checks. `tests/test_xigua_ble.py`
is included in the repository gate; real Android pairing and Wi-Fi persistence
still require device acceptance.
