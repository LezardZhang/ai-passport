"""Generate ready-to-use, role-scoped childcare Agent Skills."""
import io
import json
import zipfile
from urllib.parse import urlsplit

CLIENT = '''import json, sys, urllib.request
from pathlib import Path
config = json.loads((Path(__file__).resolve().parents[1] / "connection.json").read_text())
path = sys.argv[1] if len(sys.argv) > 1 else "/healthz"
if not path.startswith("/") or path.startswith("//") or ".." in path or "?" in path:
    raise SystemExit("Use an API path within this service")
class NoRedirect(urllib.request.HTTPRedirectHandler):
    def redirect_request(self, req, fp, code, msg, headers, newurl):
        raise ValueError("Service redirect refused")
opener = urllib.request.build_opener(NoRedirect)
request = urllib.request.Request(config["base_url"] + path,
    headers={"Authorization": "Bearer " + config["token"]})
# Never print exception response bodies or request headers.
try:
    with opener.open(request, timeout=30) as response:
        print(response.read().decode())
except Exception:
    raise SystemExit("Service request failed; check availability and credential validity")
'''


def build_bundle(role: str, base_url: str, token: str, child_id: str = "baby", device_id: str = "xigua-device") -> bytes:
    url = urlsplit(base_url)
    if url.scheme not in ("https", "http") or not url.hostname or url.username or url.password or url.query or url.fragment:
        raise ValueError("A public HTTP(S) base URL without credentials is required")
    if not token:
        raise ValueError("The selected role token is not configured")
    folder = f"childcare-{role}"
    permissions = {
        "hermes": "Read /v1/hermes/children/{child_id}/analysis-input; upload audio via PUT /v1/hermes/audio/files/{file_name} and register via POST /v1/hermes/audio/tracks. Do not write childcare events.",
        "public": "Read /v1/children/{child_id}/events, /summary, /v1/export/children/{child_id}, and /v1/audio/tracks. Do not write any data.",
        "device": "Read /v1/audio/tracks. Upload batches to POST /v1/children/{child_id}/events using X-Device-Id and stable event id values. Keep retries idempotent. Use POST /v1/device/snapshot with schema xigua-device-snapshot-v1, monotonically persisted revision, sequence floor, and up to 32 records with seq/type/epoch. Retry the same revision idempotently. Keep older cloud history outside the current sequence window. Preserve sequence NVS on reboot; never reset it or fake unknown timestamps. Read /v1/device/command and handle play/pause/resume/stop and acknowledge received/playing/paused/complete/stopped/error via POST /v1/device/command/{id}. Legacy batch and playback routes remain available.",
    }[role]
    skill = f'''---
name: {folder}
description: Use the configured childcare service for {role} operations with its bundled service address and role credential.
---

# Configured childcare service

Read connection.json for the address and token; do not ask for them again.
Never print, commit, or send the token except as a Bearer header to that exact service.
Run `python scripts/client.py /healthz` to check connectivity, or pass a permitted GET path.
{permissions}
The single child and device IDs are already in connection.json. No registration is needed. Read /v1/personal for current profile, device status and record counts. Read /v1/personal/statistics?start=YYYY-MM-DD&end=YYYY-MM-DD for local-day statistics. Export the full selected range via /v1/personal/export; use format=csv for spreadsheets. Device credentials cannot call these private read APIs; use /v1/device/config instead.
Event batches contain 1-100 records, each with id, type, and occurred_at (ISO 8601 with timezone).
Feeding requires amount_ml, diaper requires diaper_kind (pee/poop/both), sleep requires duration_min.
Summaries use the child's local calendar day. Export schema is xigua-childcare-export-v1.
On 401/403 stop and report invalid permissions without echoing credentials. Do not retry writes blindly.
The shared token is revoked by rotating that role token on the server; all downloaded Skills for that role then expire.
'''
    output = io.BytesIO()
    with zipfile.ZipFile(output, "w", zipfile.ZIP_DEFLATED) as archive:
        archive.writestr(f"{folder}/SKILL.md", skill)
        archive.writestr(f"{folder}/connection.json", json.dumps({"base_url": base_url.rstrip("/"), "role": role, "token": token, "child_id": child_id, "device_id": device_id}))
        archive.writestr(f"{folder}/scripts/client.py", CLIENT)
    return output.getvalue()
