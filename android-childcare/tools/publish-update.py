#!/usr/bin/env python3
"""Verify an Android release with official SDK tools, then publish over authenticated HTTPS."""
import argparse
import base64
import hashlib
import http.client
import json
import os
from pathlib import Path
import re
import ssl
import subprocess
import sys
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = "cn.xigua.childcare"
LIMIT = 64 * 1024 * 1024


class PublicationError(ValueError):
    pass


def digest(path):
    h = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(65536), b""):
            h.update(chunk)
    return h.hexdigest()


def configuration(path):
    cfg = json.loads(path.read_text())
    u = urlsplit(cfg["public_base"])
    if u.scheme != "https" or not u.hostname or u.username or u.password or u.query or u.fragment:
        raise PublicationError("Update origin must be canonical HTTPS")
    for key in ("publish_token", "download_token"):
        token = cfg[key]
        if not 32 <= len(token) <= 2048 or not all(32 <= ord(c) <= 126 for c in token):
            raise PublicationError("Invalid scoped credential")
    if cfg["publish_token"] == cfg["download_token"]:
        raise PublicationError("Publishing and download credentials must differ")
    if not re.fullmatch("[a-f0-9]{64}", cfg["certificate_sha256"]):
        raise PublicationError("Expected original Android signing certificate required")
    cfg["public_base"] = cfg["public_base"].rstrip("/")
    return cfg


def inspect(apk, cfg, sdk, notes):
    if not apk.is_file() or not 0 < apk.stat().st_size <= LIMIT:
        raise PublicationError("APK must be 1 byte to 64 MiB")
    tools = sdk / "build-tools" / "35.0.1"
    java = os.environ.get("JAVA_HOME")
    if not java:
        candidates = list((ROOT / ".local").glob("jdk-*/Contents/Home"))
        java = str(candidates[0]) if candidates else ""
    env = dict(os.environ)
    if java:
        env["JAVA_HOME"] = java
        env["PATH"] = java + "/bin:" + env.get("PATH", "")
    signed = subprocess.run([str(tools / "apksigner"), "verify", "--print-certs", str(apk)],
                            check=True, capture_output=True, text=True, env=env).stdout
    certificates = re.findall(r"Signer #\d+ certificate SHA-256 digest: ([a-fA-F0-9]{64})", signed)
    if [c.lower() for c in certificates] != [cfg["certificate_sha256"]]:
        raise PublicationError("APK signer differs from installed phone; publication refused")
    badging = subprocess.run([str(tools / "aapt"), "dump", "badging", str(apk)],
                             check=True, capture_output=True, text=True, env=env).stdout
    package = re.search(r"package: name='([^']+)' versionCode='(\d+)' versionName='([^']+)'", badging)
    minimum = re.search(r"^sdkVersion:'(\d+)'", badging, re.M)
    if not package or package[1] != PACKAGE or not minimum:
        raise PublicationError("Unexpected Android package or missing minimum SDK")
    if "test" in package[3].lower():
        raise PublicationError("Synthetic test APK cannot be published")
    metadata = dict(package_name=PACKAGE, version_code=int(package[2]), version_name=package[3],
                    min_sdk=int(minimum[1]), size_bytes=apk.stat().st_size, sha256=digest(apk),
                    certificate_sha256=certificates[0].lower(), notes=notes)
    encoded = base64.b64encode(json.dumps(metadata, ensure_ascii=False, separators=(",", ":")).encode()).decode()
    # Fit an ordinary Nginx 8 KiB header line without changing the shared virtual host.
    if len(notes) > 8000 or len(encoded) > 7600:
        raise PublicationError("Release notes too long; shorten to fit the 7.6 KiB encoded metadata limit")
    return metadata, encoded


def request(cfg, method, route, token, apk=None, encoded=None):
    u = urlsplit(cfg["public_base"])
    conn = http.client.HTTPSConnection(u.hostname, u.port or 443, timeout=120, context=ssl.create_default_context())
    headers = {"Authorization": "Bearer " + token, "Cache-Control": "no-cache"}
    if apk:
        headers.update({"Content-Type": "application/octet-stream", "Content-Length": str(apk.stat().st_size),
                        "X-Release-Metadata": encoded})
    try:
        conn.putrequest(method, u.path + route)
        for key, value in headers.items():
            conn.putheader(key, value)
        conn.endheaders()
        if apk:
            with apk.open("rb") as source:
                for chunk in iter(lambda: source.read(65536), b""):
                    conn.send(chunk)
        response = conn.getresponse()
        # Never follow redirects or expose credentials/errors in routine output.
        raw = response.read(65537)
        if len(raw) > 65536:
            raise PublicationError("Cloud response exceeds manifest limit")
        if response.status == 404 and route == "/latest.json":
            return None
        if response.status not in (200, 201):
            raise PublicationError(f"Cloud request failed (HTTP {response.status}); current release remains unchanged")
        return json.loads(raw)
    finally:
        conn.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("action", choices=("prepare", "publish", "check"))
    parser.add_argument("--config", type=Path, default=ROOT / ".local/update-publisher.json")
    parser.add_argument("--apk", type=Path)
    parser.add_argument("--sdk", type=Path, default=Path(os.environ.get("ANDROID_SDK_ROOT", Path.home() / "Library/Android/sdk")))
    parser.add_argument("--notes-file", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "reports/update-release.json")
    args = parser.parse_args()
    cfg = configuration(args.config)
    if args.action == "check":
        latest = request(cfg, "GET", "/latest.json", cfg["download_token"])
        print("No release published" if latest is None else f"Cloud release: {latest['version_name']} (code {latest['version_code']})")
        return
    apk = args.apk or ROOT / json.loads((ROOT / "reports/build-manifest.json").read_text())["apk"]
    notes = args.notes_file.read_text() if args.notes_file else "云端更新：自动检查新版本、验证安装包并保留现有记录。"
    metadata, encoded = inspect(apk, cfg, args.sdk, notes)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(metadata, ensure_ascii=False, indent=2) + "\n")
    if args.action == "publish":
        old = request(cfg, "GET", "/latest.json", cfg["publish_token"])
        if old and metadata["version_code"] <= old["version_code"] and any(old.get(k) != v for k, v in metadata.items()):
            raise PublicationError("Version code must increase; cloud release was not changed")
        result = request(cfg, "POST", "/publish", cfg["publish_token"], apk, encoded)
        latest = request(cfg, "GET", "/latest.json", cfg["download_token"])
        if any(result.get(k) != v or latest.get(k) != v for k, v in metadata.items()):
            raise PublicationError("Cloud receipt differs from verified release; inspect the dedicated release channel")
        print(f"Published and read back: {metadata['version_name']} (code {metadata['version_code']})")
    else:
        print(f"Verified for publication: {metadata['version_name']} (code {metadata['version_code']}); SHA256 {metadata['sha256']}")


if __name__ == "__main__":
    try:
        main()
    except PublicationError as error:
        print(str(error), file=sys.stderr)
        sys.exit(1)
    except (ValueError, OSError, KeyError, subprocess.CalledProcessError, http.client.HTTPException):
        # Some exception strings contain headers, URLs or subprocess arguments.
        print("Update operation failed. Check SDK verification, original signer, version, configuration and cloud availability.", file=sys.stderr)
        sys.exit(1)
