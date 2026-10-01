import hashlib
import io
from pathlib import Path
import zipfile

import pytest
from fastapi.testclient import TestClient

from backend.app import main
from backend.app.unified import create_app
from backend.deploy.check_existing_data import inventory

cloud = pytest.importorskip("cloud_backup.main")
from cloud_backup.config import Settings


@pytest.fixture
def configured(tmp_path, monkeypatch):
    root = Path(cloud.__file__).resolve().parents[2]
    cfg = Settings(
        database_path=tmp_path / "backup.db", data_dir=tmp_path / "objects",
        admin_token="a" * 64, key_encryption_key=b"k" * 32,
        host="127.0.0.1", port=8080, max_upload_bytes=100000,
        maintenance_interval_seconds=3600, upload_ttl_seconds=3600,
        min_free_bytes=0, trust_proxy_headers=False,
        public_base_url="https://service.example/cloud-backup",
        admin_ui_path=root / "admin/index.html",
        skill_dir=root / "skills/cloud-backup-integrator",
        protocol_path=root / "docs/CLOUD-BACKUP-PROTOCOL.md", root_path="/cloud-backup",
    )
    monkeypatch.setattr(main, "DB_PATH", tmp_path / "childcare.db")
    for name in ("DEVICE_TOKEN", "ADMIN_TOKEN", "HERMES_TOKEN", "PUBLIC_READ_TOKEN"):
        monkeypatch.setattr(main, name, name + "-test")
    monkeypatch.setattr(main, "API_TOKEN", "")
    monkeypatch.setattr(main, "ADMIN_PASSWORD", "password-test")
    monkeypatch.setattr(main, "PUBLIC_BASE_URL", "https://service.example/cloud-backup/childcare")
    return cfg


def test_existing_keys_versions_and_skills_survive_unification(configured):
    admin = {"Authorization": "Bearer " + configured.admin_token}
    # Seed the ORIGINAL service first, not the unified application's schema.
    with TestClient(cloud.create_app(configured)) as client:
        user = client.post("/admin/v1/users", headers=admin,
                           json={"name": "existing", "quota_bytes": 10000}).json()
        key = client.post(f"/admin/v1/users/{user['id']}/keys", headers=admin,
                          json={"scopes": ["read", "write", "delete"]}).json()
        auth = {"Authorization": "Bearer " + key["key"]}
        versions = []
        for data in (b"old-fitness-data", b"new-fitness-data"):
            result = client.put("/api/v1/files/fitness/archive.sqlite", headers=auth, content=data)
            assert result.status_code == 201
            versions.append(result.json()["id"])

    before = inventory(configured.database_path, configured.data_dir)
    assert before["active_objects"] == 2
    with TestClient(create_app(configured), base_url="https://service.example") as client:
        prefix = "/cloud-backup"
        assert client.get(prefix + "/readyz").status_code == 200
        assert client.get("/childcare/healthz").status_code == 200
        assert client.get(prefix + "/console").status_code == 200
        assert client.get(prefix + "/api/v1/files/fitness/archive.sqlite", headers=auth).content == b"new-fitness-data"
        assert client.get(prefix + "/api/v1/files/fitness/archive.sqlite?version=" + versions[0], headers=auth).content == b"old-fitness-data"
        skill = client.get(prefix + f"/admin/v1/keys/{key['id']}/skill/download", headers=admin)
        assert skill.status_code == 200
        with zipfile.ZipFile(io.BytesIO(skill.content)) as bundle:
            assert any(name.endswith("connection.json") for name in bundle.namelist())
        backup = client.app.state.backup.state.backups.create()
        assert backup["size_bytes"] > 0
        child_prefix = prefix + "/childcare"
        child_admin = {"Authorization": "Bearer " + main.ADMIN_TOKEN}
        assert client.post(child_prefix + "/v1/children", headers=child_admin,
                           json={"id": "baby", "name": "Baby"}).status_code == 200
        assert client.get(child_prefix + "/v1/children/baby/events", headers=auth).status_code == 403
        assert client.get(prefix + "/api/v1/usage", headers=child_admin).status_code == 401
        assert client.get(child_prefix + "/admin/api/skills/hermes").status_code == 403
        login = client.post(prefix + "/console/login", json={"key": configured.admin_token})
        assert login.status_code == 200
        assert "Path=/cloud-backup;" in login.headers["set-cookie"]
        page = client.get(child_prefix + "/admin")
        assert 'const BASE="/cloud-backup"' in page.text
        assert "__CHILDCARE_BASE_JSON__" not in page.text
        skill = client.get(child_prefix + "/admin/api/skills/hermes")
        assert skill.status_code == 200
        assert skill.headers["cache-control"] == "no-store"
        with zipfile.ZipFile(io.BytesIO(skill.content)) as bundle:
            import json
            connection = json.loads(bundle.read("childcare-hermes/connection.json"))
            assert connection["token"] == main.HERMES_TOKEN
            assert main.ADMIN_TOKEN.encode() not in skill.content
        for role, token in (("device", main.DEVICE_TOKEN), ("public", main.PUBLIC_READ_TOKEN)):
            skill = client.get(child_prefix + "/admin/api/skills/" + role)
            assert skill.status_code == 200
            with zipfile.ZipFile(io.BytesIO(skill.content)) as bundle:
                connection = json.loads(bundle.read(f"childcare-{role}/connection.json"))
                assert connection["token"] == token
                assert connection["role"] == role
        client.post(prefix + "/console/logout")
        assert "统一管理 Key" in client.get(child_prefix + "/admin").text

    after = inventory(configured.database_path, configured.data_dir)
    assert before["object_inventory_sha256"] == after["object_inventory_sha256"]
    assert before["counts"] == after["counts"]
    assert before["metadata_sha256"] == after["metadata_sha256"]
    # Rollback to the original app keeps the latest file and key usable.
    with TestClient(cloud.create_app(configured)) as client:
        assert client.get("/api/v1/files/fitness/archive.sqlite", headers=auth).content == b"new-fitness-data"


def test_phone_client_requires_login_and_matches_device_service(configured):
    with TestClient(create_app(configured),base_url="https://service.example") as client:
        asset='/cloud-backup/console/api/bluetooth.js'
        assert client.get(asset).status_code==401
        assert client.post('/cloud-backup/console/login',json={'key':configured.admin_token}).status_code==200
        response=client.get(asset)
        assert response.status_code==200
        assert response.headers['content-type'].startswith('text/javascript')
        assert response.headers['cache-control']=='no-store'
        assert '7e24a7f0-9b52-4f36-a7f8-84e9d9b00001' in response.text
        assert configured.admin_token not in response.text
        page=client.get('/cloud-backup/console').text
        assert 'id="phone"' in page
        assert 'src="/cloud-backup/console/api/bluetooth.js"' in page
        assert '__BLUETOOTH_SCRIPT_URL__' not in page


def test_phone_entry_redirect_keeps_device_http_api(configured,monkeypatch):
    monkeypatch.setenv('XIGUA_PHONE_HTTPS_URL','https://service.example/cloud-backup/console')
    with TestClient(create_app(configured),base_url='http://service.example',follow_redirects=False) as client:
        redirect=client.get('/cloud-backup/console/login')
        assert redirect.status_code==303
        assert redirect.headers['location']=='https://service.example/cloud-backup/console/login'
        assert client.get('/cloud-backup/readyz').status_code==200
        assert client.get('/childcare/healthz').status_code==200
    with TestClient(create_app(configured),base_url='https://service.example') as client:
        assert client.post('/cloud-backup/console/login',json={'key':configured.admin_token}).status_code==200
        assert '手机蓝牙配网' in client.get('/cloud-backup/console').text
        assert client.get('/cloud-backup/console/api/bluetooth.js').status_code==200


def test_unified_fails_closed_and_rejects_shared_db(configured, monkeypatch):
    monkeypatch.setattr(main, "DEVICE_TOKEN", "")
    with pytest.raises(ValueError, match="role tokens"):
        create_app(configured)
    monkeypatch.setattr(main, "DEVICE_TOKEN", "test-device")
    monkeypatch.setattr(main, "DB_PATH", configured.database_path)
    with pytest.raises(ValueError, match="separate"):
        create_app(configured)


def test_inventory_rejects_corrupted_object(configured):
    app = cloud.create_app(configured)
    objects = configured.data_dir
    with TestClient(app) as client:
        admin = {"Authorization": "Bearer " + configured.admin_token}
        user = client.post("/admin/v1/users", headers=admin, json={"name": "corrupt", "quota_bytes": 10000}).json()
        key = client.post(f"/admin/v1/users/{user['id']}/keys", headers=admin,
                          json={"scopes": ["read", "write"]}).json()["key"]
        assert client.put("/api/v1/files/sample", headers={"Authorization": "Bearer " + key},
                          content=b"original").status_code == 201
        row = app.state.database.query_one("SELECT object_key FROM versions WHERE state='active'")
        (objects / row["object_key"]).write_bytes(b"corrupt!")
        with pytest.raises(ValueError, match="integrity"):
            inventory(configured.database_path, objects)


def test_upgrade_verification_blocks_writes(configured, monkeypatch):
    monkeypatch.setenv("PERSONAL_SERVICES_VERIFY_ONLY", "1")
    with TestClient(create_app(configured)) as client:
        assert client.get("/readyz").json()["verification_read_only"] is True
        assert client.post("/admin/v1/users", json={"name": "blocked"}).status_code == 503
        assert client.put("/api/v1/files/blocked", content=b"blocked").status_code == 503
        assert client.post("/childcare/v1/children", json={"id": "blocked", "name": "x"}).status_code == 503
        assert client.app.state.backup.state.database.query_one("SELECT COUNT(*) AS n FROM users")["n"] == 0
