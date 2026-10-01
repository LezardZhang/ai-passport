"""Install into the identified Cockpit backup service; preserve old image/data."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import secrets
import shutil
import subprocess
import sys
import time

TARGET = "cockpit-cloud-cloud-backup-1"
EXPECTED_MAIN = "eeb4ba8d33e823f39ba6599e2a57166460a7e1a7454ac91b0170093d40f79840"


def run(args, *, capture=False):
    result = subprocess.run(args, check=True, text=True, stdout=subprocess.PIPE if capture else None)
    return result.stdout if capture else ""


def remote(code):
    return run(["docker", "exec", TARGET, "python", "-c", code], capture=True)


def wait_ready():
    for _ in range(60):
        result = subprocess.run(["docker", "exec", TARGET, "python", "-c",
            "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8080/readyz',timeout=2).read()"],
            stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if result.returncode == 0:
            return
        time.sleep(1)
    raise RuntimeError("Service readiness timeout")


def main():
    os.umask(0o077)
    release = Path(__file__).resolve().parents[1]
    state = release / "deployment-state"
    state.mkdir(exist_ok=False)
    inspection = json.loads(run(["docker", "inspect", TARGET], capture=True))[0]
    labels = inspection["Config"]["Labels"]
    root = Path(labels["com.docker.compose.project.working_dir"])
    files = labels["com.docker.compose.project.config_files"].split(",")
    if root != Path("/home/chatgpt/cockpit-cloud-fresh") or labels["com.docker.compose.service"] != "cloud-backup":
        raise RuntimeError("Unexpected target; refusing to modify it")
    env = dict(x.split("=", 1) for x in inspection["Config"]["Env"] if "=" in x)
    digest = remote("import cloud_backup.main,hashlib,pathlib; print(hashlib.sha256(pathlib.Path(cloud_backup.main.__file__).read_bytes()).hexdigest())").strip()
    if digest != EXPECTED_MAIN:
        raise RuntimeError("Backup code differs from tested version")
    for required in ("CLOUDBACKUP_ADMIN_TOKEN", "CLOUDBACKUP_KEY_ENCRYPTION_KEY", "CLOUDBACKUP_PUBLIC_BASE_URL"):
        if not env.get(required):
            raise RuntimeError("Required backup configuration missing")
    configs = {m["Destination"]: m["Source"] for m in inspection["Mounts"]}
    config_path, data_path = Path(configs["/config"]), Path(configs["/data"])
    if (config_path / "childcare.db").exists():
        raise RuntimeError("Existing childcare database requires explicit migration; refusing to overwrite")
    stamp = time.strftime("%Y%m%d-%H%M%S", time.gmtime())
    old_image = inspection["Image"]
    base_tag = "personal-services-backup-base:" + stamp
    new_tag = "personal-services:" + stamp
    run(["docker", "tag", old_image, base_tag])
    (state / "old-image.txt").write_text(old_image + "\n")
    (state / "original-inspection.json").write_text(json.dumps(inspection))
    envfiles = [root / n for n in (".env", ".env.official-backend-v25", ".env.cloud-backup") if (root / n).is_file()]
    for path in envfiles:
        shutil.copy2(path, state / path.name)
    for i, path in enumerate(files):
        shutil.copy2(path, state / (str(i) + "-" + Path(path).name))
    media = config_path.parent / "childcare-media"
    media.mkdir(exist_ok=True)
    uid, gid = map(int, inspection["Config"]["User"].split(":"))
    os.chown(media, uid, gid)
    child_env = release / "childcare.env"
    child_values = {"CHILDCARE_" + name: secrets.token_urlsafe(36) for name in (
        "DEVICE_TOKEN", "ADMIN_TOKEN", "HERMES_TOKEN", "PUBLIC_READ_TOKEN", "ADMIN_PASSWORD", "SESSION_SECRET")}
    child_values["CHILDCARE_ADMIN_TOKEN"] = env["CLOUDBACKUP_ADMIN_TOKEN"]
    child_values["CHILDCARE_ADMIN_PASSWORD"] = env["CLOUDBACKUP_ADMIN_TOKEN"]
    child_values.update({"CHILDCARE_PUBLIC_BASE_URL": env["CLOUDBACKUP_PUBLIC_BASE_URL"].rstrip("/") + "/childcare",
                        "CHILDCARE_SESSION_COOKIE_SECURE": "0" if env["CLOUDBACKUP_PUBLIC_BASE_URL"].startswith("http:") else "1"})
    # This is the first deployment; preserve all existing backup credentials.
    child_env.write_text("".join(k + "=" + v + "\n" for k, v in child_values.items()))
    override = release / "installed.override.json"
    settings = {"services": {"cloud-backup": {
        "image": new_tag,
        "build": {"context": str(release), "dockerfile": "deploy/Dockerfile.installed", "args": {"BACKUP_IMAGE": base_tag}},
        "env_file": [str(child_env)],
        "environment": {"PERSONAL_SERVICES_VERIFY_ONLY": "1", "CHILDCARE_DB": "/config/childcare.db", "CHILDCARE_MEDIA_DIR": "/childcare-media"},
        "volumes": [str(media) + ":/childcare-media"],
        "healthcheck": {"test": ["CMD", "python", "-c", "import urllib.request; urllib.request.urlopen('http://127.0.0.1:8080/readyz', timeout=3)"]}
    }}}
    override.write_text(json.dumps(settings))
    rollback = release / "rollback.override.json"
    rollback.write_text(json.dumps({"services": {"cloud-backup": {"image": old_image}}}))
    compose = ["docker", "compose", "--project-name", labels["com.docker.compose.project"], "--project-directory", str(root)]
    for path in envfiles:
        compose += ["--env-file", str(path)]
    for path in files:
        compose += ["-f", path]
    candidate = compose + ["-f", str(override)]
    run(candidate + ["config", "--quiet"])
    print("Candidate configuration: PASS", flush=True)
    run(candidate + ["build", "cloud-backup"])
    print("Candidate image build: PASS", flush=True)
    backup = json.loads(remote("import os,json,urllib.request; r=urllib.request.Request('http://127.0.0.1:8080/admin/v1/backups',data=b'',headers={'Authorization':'Bearer '+os.environ['CLOUDBACKUP_ADMIN_TOKEN']},method='POST'); print(urllib.request.urlopen(r,timeout=900).read().decode())"))
    archive = data_path / "backups" / (backup["id"] + ".tar.gz")
    if not archive.is_file():
        raise RuntimeError("Backup archive missing")
    import tarfile
    h = hashlib.sha256()
    with archive.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            h.update(block)
    if h.hexdigest() != backup["sha256"] or archive.stat().st_size != backup["size_bytes"]:
        raise RuntimeError("Backup archive hash/size verification failed")
    with tarfile.open(archive, "r:gz") as bundle:
        manifest = json.load(bundle.extractfile("manifest.json"))
        if len(manifest["objects"]) != backup["object_count"]:
            raise RuntimeError("Backup object manifest mismatch")
        for item in manifest["objects"]:
            member = bundle.extractfile("data/" + item["key"])
            checksum = hashlib.sha256()
            size = 0
            for block in iter(lambda: member.read(1024 * 1024), b""):
                checksum.update(block)
                size += len(block)
            if size != item["size"] or checksum.hexdigest() != item["sha256"]:
                raise RuntimeError("Backup archived object verification failed")
    shutil.copy2(archive, state / archive.name)
    (state / "backup.json").write_text(json.dumps(backup))
    print("Consistent backup created and retained", flush=True)
    spec = importlib.util.spec_from_file_location("inventory", release / "deploy/check_existing_data.py")
    inventory = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(inventory)
    stopped = False
    try:
        # Only stop the backup service. Its data/config directories remain in place.
        run(["docker", "stop", TARGET])
        stopped = True
        before = inventory.inventory(config_path / "cloud-backup.db", data_path)
        (state / "before.json").write_text(json.dumps(before, indent=2))
        run(candidate + ["up", "-d", "--no-deps", "--no-build", "cloud-backup"])
        wait_ready()
        after = inventory.inventory(config_path / "cloud-backup.db", data_path)
        if before != after:
            raise RuntimeError("Existing backup data changed during read-only verification")
        (state / "after.json").write_text(json.dumps(after, indent=2))
        print("Existing metadata and all active object hashes: PASS", flush=True)
        # Verify both original and new Skill generation using server-local credentials.
        remote("import os,urllib.request,io,zipfile; b='http://127.0.0.1:8080'; t=os.environ['CLOUDBACKUP_ADMIN_TOKEN']; r=urllib.request.Request(b+'/admin/v1/skill/download',headers={'Authorization':'Bearer '+t}); z=zipfile.ZipFile(io.BytesIO(urllib.request.urlopen(r).read())); assert any(n.endswith('SKILL.md') for n in z.namelist()); t=os.environ['CHILDCARE_ADMIN_TOKEN']; r=urllib.request.Request(b+'/childcare/admin/api/skills/hermes',headers={'Authorization':'Bearer '+t}); z=zipfile.ZipFile(io.BytesIO(urllib.request.urlopen(r).read())); assert 'childcare-hermes/connection.json' in z.namelist(); print('Skill exports: PASS')")
        settings["services"]["cloud-backup"]["environment"]["PERSONAL_SERVICES_VERIFY_ONLY"] = "0"
        override.write_text(json.dumps(settings))
        run(candidate + ["up", "-d", "--no-deps", "--no-build", "cloud-backup"])
        wait_ready()
        (state / "result.json").write_text(json.dumps({"status": "installed", "image": new_tag, "old_image": old_image, "backup": backup["id"], "counts": before["counts"], "public_base_url": child_values["CHILDCARE_PUBLIC_BASE_URL"]}, indent=2))
        print("INSTALLATION PASS; normal operations resumed", flush=True)
    except Exception:
        if stopped:
            print("Verification failed; restoring original backup image", flush=True)
            run(compose + ["-f", str(rollback), "up", "-d", "--no-deps", "--no-build", "cloud-backup"])
            wait_ready()
        raise
    rollback_cmd = compose + ["-f", str(rollback), "up", "-d", "--no-deps", "--no-build", "cloud-backup"]
    import shlex
    (release / "rollback.sh").write_text("#!/bin/sh\nset -eu\n" + shlex.join(rollback_cmd) + "\n")
    (release / "service-compose.sh").write_text("#!/bin/sh\nset -eu\n" + shlex.join(candidate) + ' "$@"\n')
    print("Deployment files: " + str(release), flush=True)


if __name__ == "__main__":
    main()
