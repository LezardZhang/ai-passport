import os
import tempfile

os.environ["CHILDCARE_DB"] = os.path.join(tempfile.gettempdir(), "xigua-childcare-api-test.db")
try:
    os.unlink(os.environ["CHILDCARE_DB"])
except FileNotFoundError:
    pass
os.environ["CHILDCARE_MEDIA_DIR"] = "/tmp/xigua-test-media"

from fastapi.testclient import TestClient

from backend.app.main import app


def test_records_are_idempotent_and_time_stamped():
    with TestClient(app) as client:
        assert client.post("/v1/children", json={"id": "c1", "name": "宝宝"}).status_code == 200
        assert client.post("/v1/devices/register", json={"id": "d1", "child_id": "c1"}).status_code == 200
        payload = {"events": [{
            "id": "e1", "type": "feeding", "occurred_at": "2026-09-30T10:00:00+08:00",
            "amount_ml": 120, "ingredient": "formula",
        }]}
        headers = {"X-Device-Id": "d1"}
        assert client.post("/v1/children/c1/events", json={"events": [{
            "id": "invalid", "type": "feeding", "occurred_at": "2026-09-30T10:00:00+08:00",
        }]}, headers=headers).status_code == 422
        assert client.post("/v1/children/c1/events", json=payload, headers=headers).json()["accepted"] == 1
        assert client.post("/v1/children/c1/events", json=payload, headers=headers).json()["duplicates"] == 1
        response = client.get("/v1/children/c1/events")
        assert response.json()["items"][0]["occurred_at"] == "2026-09-30T02:00:00+00:00"
        assert client.get("/v1/children/c1/summary?day=2026-09-30").json()["milk_ml"] == 120


def test_audio_and_hermes_export():
    with TestClient(app) as client:
        response = client.post("/v1/audio/tracks", json={
            "title": "Rain", "category": "white_noise", "file_name": "rain.mp3",
        })
        assert response.status_code == 200
        assert "/media/rain.mp3" in response.json()["play_url"]
        uploaded = client.put("/v1/hermes/audio/files/rain.raw", content=b"RIFF-test")
        assert uploaded.status_code == 200
        assert uploaded.json()["size_bytes"] == 9
        assert client.get("/v1/export/children/c1").status_code == 200
        assert client.get("/v1/hermes/children/c1/analysis-input").status_code == 200


def test_admin_login_and_page():
    with TestClient(app) as client:
        assert client.get("/admin/login").status_code == 200
        response = client.post("/admin/login", json={"password": "dev"})
        assert response.status_code == 200
        assert client.get("/admin").status_code == 200


def test_role_tokens_are_separate(monkeypatch):
    import backend.app.main as main

    for name in ("API_TOKEN", "DEVICE_TOKEN", "ADMIN_TOKEN", "HERMES_TOKEN", "PUBLIC_READ_TOKEN"):
        monkeypatch.setattr(main, name, {"DEVICE_TOKEN": "device", "ADMIN_TOKEN": "admin",
                                         "HERMES_TOKEN": "hermes", "PUBLIC_READ_TOKEN": "public"}.get(name, ""))
    with TestClient(app) as client:
        assert client.post("/v1/children", json={"id": "blocked", "name": "x"},
                           headers={"Authorization": "Bearer device"}).status_code == 403
        assert client.post("/v1/children", json={"id": "c2", "name": "x"},
                           headers={"Authorization": "Bearer admin"}).status_code == 200
        assert client.get("/v1/children/c2/summary", headers={"Authorization": "Bearer public"}).status_code == 200
        assert client.post("/v1/children/c2/events", json={"events": [{
            "id": "e2", "type": "feeding", "occurred_at": "2026-09-30T11:00:00+08:00", "amount_ml": 90,
        }]}, headers={"Authorization": "Bearer device", "X-Device-Id": "d1"}).status_code == 404


def test_summary_uses_child_local_day():
    with TestClient(app) as client:
        client.post("/v1/children", json={"id": "tz", "name": "Timezone"})
        client.post("/v1/devices/register", json={"id": "tz-device", "child_id": "tz"})
        events = [{"id": str(i), "type": "feeding", "amount_ml": 10,
                   "occurred_at": timestamp} for i, timestamp in enumerate([
            "2026-09-29T23:59:59+08:00", "2026-09-30T00:00:00+08:00",
            "2026-09-30T23:59:59+08:00", "2026-10-01T00:00:00+08:00"])]
        assert client.post("/v1/children/tz/events", json={"events": events},
                           headers={"X-Device-Id": "tz-device"}).status_code == 200
        result = client.get("/v1/children/tz/summary?day=2026-09-30").json()
        assert result["feeding_count"] == 2
        assert result["milk_ml"] == 20
        assert client.get("/v1/children/tz/summary?day=2026-02-30").status_code == 400
        assert client.post("/v1/children", json={"id": "bad-zone", "name": "x",
                           "timezone": "Invalid/Zone"}).status_code == 422


def test_device_can_read_audio_but_not_child_data(monkeypatch):
    import backend.app.main as main
    monkeypatch.setattr(main, "DEVICE_TOKEN", "device-reader")
    with TestClient(app) as client:
        headers = {"Authorization": "Bearer device-reader"}
        result = client.get("/v1/audio/tracks", headers=headers)
        assert result.status_code == 200
        track = result.json()["items"][0]
        assert client.get("/v1/audio/tracks/" + track["id"], headers=headers).status_code == 200
        assert client.get("/v1/children/c1/events", headers=headers).status_code == 403
