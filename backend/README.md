<p align="right"><a href="README.zh_CN.md">简体中文</a> · <strong>English</strong></p>

# Xigua childcare backend

This service stores childcare events from the ESP32-C3 assistant and exposes a
small audio catalogue for songs, stories, and white noise. It uses SQLite for a
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
5. List `/v1/audio/tracks?category=white_noise` and stream the returned URL.
