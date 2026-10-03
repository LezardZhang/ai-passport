from __future__ import annotations

import hashlib
import hmac
import json
import os
import secrets
import shutil
import sqlite3
import uuid
from datetime import date, datetime, time, timedelta, timezone
from pathlib import Path
from typing import Annotated, Literal
from urllib.parse import quote
from zoneinfo import ZoneInfo, ZoneInfoNotFoundError

from fastapi import Depends, FastAPI, Header, HTTPException, Query, Request, Response, status
from fastapi.staticfiles import StaticFiles
from pydantic import BaseModel, ConfigDict, Field, field_validator, model_validator


ROOT = Path(__file__).resolve().parents[1]
DB_PATH = Path(os.getenv("CHILDCARE_DB", str(ROOT / "data" / "childcare.db")))
MEDIA_DIR = Path(os.getenv("CHILDCARE_MEDIA_DIR", str(ROOT / "media")))
PUBLIC_BASE_URL = os.getenv("CHILDCARE_PUBLIC_BASE_URL", "").rstrip("/")
API_TOKEN = os.getenv("CHILDCARE_API_TOKEN", "").strip()
DEVICE_TOKEN = os.getenv("CHILDCARE_DEVICE_TOKEN", "").strip()
ADMIN_TOKEN = os.getenv("CHILDCARE_ADMIN_TOKEN", "").strip()
HERMES_TOKEN = os.getenv("CHILDCARE_HERMES_TOKEN", "").strip()
PUBLIC_READ_TOKEN = os.getenv("CHILDCARE_PUBLIC_READ_TOKEN", "").strip()
ADMIN_PASSWORD = os.getenv("CHILDCARE_ADMIN_PASSWORD", "").strip()
SESSION_SECRET = os.getenv("CHILDCARE_SESSION_SECRET", ADMIN_PASSWORD or API_TOKEN or secrets.token_urlsafe(32))
SESSION_COOKIE_SECURE = os.getenv("CHILDCARE_SESSION_COOKIE_SECURE", "1") != "0"

EVENT_TYPES = ("feeding", "diaper", "sleep", "bath", "tummy", "timer")
AUDIO_CATEGORIES = ("song", "white_noise", "story", "classical", "other")


def utc_now() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def iso(value: datetime) -> str:
    if value.tzinfo is None:
        value = value.replace(tzinfo=timezone.utc)
    return value.astimezone(timezone.utc).replace(microsecond=0).isoformat()


def get_db() -> sqlite3.Connection:
    DB_PATH.parent.mkdir(parents=True, exist_ok=True)
    connection = sqlite3.connect(DB_PATH)
    connection.row_factory = sqlite3.Row
    connection.execute("PRAGMA foreign_keys = ON")
    connection.execute("PRAGMA busy_timeout = 5000")
    connection.execute("PRAGMA journal_mode = WAL")
    return connection


def init_db() -> None:
    with get_db() as db:
        db.executescript(
            """
            CREATE TABLE IF NOT EXISTS children (
                id TEXT PRIMARY KEY,
                name TEXT NOT NULL,
                birth_date TEXT,
                timezone TEXT NOT NULL DEFAULT 'Asia/Shanghai',
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS devices (
                id TEXT PRIMARY KEY,
                name TEXT,
                child_id TEXT NOT NULL REFERENCES children(id),
                last_seen_at TEXT,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );
            CREATE TABLE IF NOT EXISTS events (
                id TEXT PRIMARY KEY,
                client_event_id TEXT NOT NULL,
                child_id TEXT NOT NULL REFERENCES children(id),
                device_id TEXT NOT NULL REFERENCES devices(id),
                type TEXT NOT NULL CHECK(type IN ('feeding','diaper','sleep','bath','tummy','timer')),
                occurred_at TEXT NOT NULL,
                amount_ml INTEGER,
                ingredient TEXT,
                diaper_kind TEXT,
                duration_min INTEGER,
                start_at TEXT,
                end_at TEXT,
                timer_seconds INTEGER,
                note TEXT,
                raw_json TEXT,
                created_at TEXT NOT NULL,
                UNIQUE(child_id, client_event_id)
            );
            CREATE INDEX IF NOT EXISTS events_child_time ON events(child_id, occurred_at DESC);
            CREATE TABLE IF NOT EXISTS audio_tracks (
                id TEXT PRIMARY KEY,
                title TEXT NOT NULL,
                category TEXT NOT NULL CHECK(category IN ('song','white_noise','story','classical','other')),
                file_name TEXT,
                play_url TEXT,
                mime_type TEXT NOT NULL DEFAULT 'audio/mpeg',
                duration_ms INTEGER,
                sha256 TEXT,
                size_bytes INTEGER,
                active INTEGER NOT NULL DEFAULT 1,
                created_at TEXT NOT NULL,
                updated_at TEXT NOT NULL
            );
            CREATE INDEX IF NOT EXISTS audio_tracks_category ON audio_tracks(category, active, title);
            CREATE TABLE IF NOT EXISTS playback_events (
                id TEXT PRIMARY KEY,
                device_id TEXT NOT NULL REFERENCES devices(id),
                track_id TEXT NOT NULL REFERENCES audio_tracks(id),
                action TEXT NOT NULL CHECK(action IN ('start','pause','stop','complete','error')),
                position_ms INTEGER,
                occurred_at TEXT NOT NULL,
                error TEXT
            );
            """
        )
        migrate_audio_categories(db)


def seed_builtin_audio() -> None:
    """Install a small original family-safe catalog once, without touching user tracks."""
    if os.getenv("CHILDCARE_SEED_BUILTIN_AUDIO", "0") != "1":
        return
    source_dir = ROOT / "assets" / "childcare_audio"
    entries = (
        ("xigua-original-lullaby", "西瓜原创 · 轻柔儿歌", "song", "watermelon_lullaby.wav"),
        ("xigua-bedtime-story-music", "晚安小故事 · 月亮配乐", "story", "watermelon_story.wav"),
        ("xigua-original-classical", "西瓜原创 · 古典旋律", "classical", "watermelon_classical.wav"),
    )
    now = utc_now()
    with get_db() as db:
        for track_id, title, category, filename in entries:
            if db.execute("SELECT 1 FROM audio_tracks WHERE id=?", (track_id,)).fetchone():
                continue
            source = source_dir / filename
            if not source.is_file():
                continue
            target_name = f"builtin-{filename}"
            target = MEDIA_DIR / target_name
            if not target.is_file():
                shutil.copyfile(source, target)
            data = target.read_bytes()
            db.execute(
                """INSERT INTO audio_tracks(id,title,category,file_name,play_url,mime_type,duration_ms,sha256,size_bytes,active,created_at,updated_at)
                   VALUES(?,?,?,?,?,?,?,?,?,1,?,?)""",
                (track_id, title, category, target_name, None, "audio/wav", len(data) * 1000 // 24000,
                 hashlib.sha256(data).hexdigest(), len(data), now, now),
            )
            try:
                db.execute(
                    "INSERT OR IGNORE INTO audio_sources VALUES(?,?,?,?,?,?)",
                    (track_id, "https://162.14.108.234/cloud-backup/childcare/audio/original",
                     "西瓜原创", "原创家庭内容", "https://162.14.108.234/cloud-backup/childcare/audio/original", "首次内置"),
                )
            except sqlite3.OperationalError:
                # The legacy-only app creates audio_sources in its personal extension.
                pass


def migrate_audio_categories(db: sqlite3.Connection) -> None:
    """Replace the old CHECK atomically while preserving referenced track IDs."""
    schema = db.execute("SELECT sql FROM sqlite_master WHERE name='audio_tracks'").fetchone()[0]
    if "'classical'" in schema:
        return
    # PRAGMA must precede BEGIN; playback history still references audio_tracks.
    db.execute("PRAGMA foreign_keys = OFF")
    try:
        db.execute("BEGIN IMMEDIATE")
        replacement = schema.replace("audio_tracks", "audio_tracks_new", 1).replace(
            "'story','other'", "'story','classical','other'")
        if replacement == schema or "'classical'" not in replacement:
            raise RuntimeError("unsupported audio category schema")
        db.execute(replacement)
        db.execute("INSERT INTO audio_tracks_new SELECT * FROM audio_tracks")
        db.execute("DROP TABLE audio_tracks")
        db.execute("ALTER TABLE audio_tracks_new RENAME TO audio_tracks")
        db.execute("CREATE INDEX audio_tracks_category ON audio_tracks(category, active, title)")
        if db.execute("PRAGMA foreign_key_check").fetchall():
            raise RuntimeError("audio category migration would break references")
        db.commit()
    except Exception:
        db.rollback()
        raise
    finally:
        db.execute("PRAGMA foreign_keys = ON")


class ChildIn(BaseModel):
    model_config = ConfigDict(extra="forbid")
    id: str = Field(min_length=1, max_length=64)
    name: str = Field(min_length=1, max_length=80)
    birth_date: str | None = None
    timezone: str = Field(default="Asia/Shanghai", max_length=64)


    @field_validator("timezone")
    @classmethod
    def valid_timezone(cls, value: str) -> str:
        try:
            ZoneInfo(value)
        except (ZoneInfoNotFoundError, ValueError) as exc:
            raise ValueError("timezone must be a valid IANA timezone") from exc
        return value


class DeviceIn(BaseModel):
    model_config = ConfigDict(extra="forbid")
    id: str = Field(min_length=1, max_length=64)
    child_id: str = Field(min_length=1, max_length=64)
    name: str | None = Field(default=None, max_length=80)


class EventIn(BaseModel):
    model_config = ConfigDict(extra="forbid")
    id: str = Field(min_length=1, max_length=128)
    type: Literal["feeding", "diaper", "sleep", "bath", "tummy", "timer"]
    occurred_at: datetime
    amount_ml: int | None = Field(default=None, ge=0, le=2000)
    ingredient: str | None = Field(default=None, max_length=32)
    diaper_kind: Literal["pee", "poop", "both"] | None = None
    duration_min: int | None = Field(default=None, ge=0, le=24 * 60)
    start_at: datetime | None = None
    end_at: datetime | None = None
    timer_seconds: int | None = Field(default=None, ge=0, le=7 * 24 * 3600)
    note: str | None = Field(default=None, max_length=500)
    metadata: dict[str, object] = Field(default_factory=dict)

    @field_validator("end_at")
    @classmethod
    def end_after_start(cls, value: datetime | None, info):
        start = info.data.get("start_at")
        if value is not None and start is not None and value < start:
            raise ValueError("end_at must be after start_at")
        return value

    @model_validator(mode="after")
    def validate_type_fields(self):
        if self.type == "feeding" and self.amount_ml is None:
            raise ValueError("feeding requires amount_ml")
        if self.type == "diaper" and self.diaper_kind is None:
            raise ValueError("diaper requires diaper_kind")
        if self.type == "sleep" and self.duration_min is None:
            raise ValueError("sleep requires duration_min")
        return self


class EventBatch(BaseModel):
    model_config = ConfigDict(extra="forbid")
    events: list[EventIn] = Field(min_length=1, max_length=100)


class TrackIn(BaseModel):
    model_config = ConfigDict(extra="forbid")
    id: str | None = Field(default=None, max_length=128)
    title: str = Field(min_length=1, max_length=120)
    category: Literal["song", "white_noise", "story", "classical", "other"]
    file_name: str | None = Field(default=None, max_length=240)
    play_url: str | None = Field(default=None, max_length=1000)
    mime_type: str = Field(default="audio/mpeg", max_length=80)
    duration_ms: int | None = Field(default=None, ge=0, le=24 * 3600 * 1000)
    sha256: str | None = Field(default=None, min_length=64, max_length=64)
    size_bytes: int | None = Field(default=None, ge=0)


class PlaybackIn(BaseModel):
    model_config = ConfigDict(extra="forbid")
    action: Literal["start", "pause", "stop", "complete", "error"]
    position_ms: int | None = Field(default=None, ge=0)
    occurred_at: datetime | None = None
    error: str | None = Field(default=None, max_length=240)


app = FastAPI(title="Xigua Childcare API", version="0.1.0")
MEDIA_DIR.mkdir(parents=True, exist_ok=True)



@app.get("/admin/login")
def admin_login_page(request: Request) -> Response:
    return Response(
        """<!doctype html><meta charset='utf-8'><title>西瓜育儿登录</title>
        <style>body{font-family:system-ui;max-width:360px;margin:15vh auto;padding:24px}input,button{box-sizing:border-box;width:100%;padding:10px;margin:6px 0}button{background:#0b7285;color:white;border:0;border-radius:6px}</style>
        <h1>西瓜育儿管理台</h1><form id='f'><input id='p' type='password' placeholder='管理密码' autofocus><button>登录</button></form><p id='m'></p>
        <script>const BASE=__CHILDCARE_BASE_JSON__;f.onsubmit=async e=>{e.preventDefault();let r=await fetch(BASE+'/admin/login',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify({password:p.value})});if(r.ok)location=BASE+'/admin';else m.textContent=(await r.json()).detail}</script>"""
        .replace("__CHILDCARE_BASE_JSON__", json.dumps(request.scope.get("root_path", ""))),
        media_type="text/html; charset=utf-8",
    )


class LoginIn(BaseModel):
    password: str


@app.post("/admin/login")
def admin_login(payload: LoginIn, response: Response, request: Request) -> dict[str, bool]:
    if not ADMIN_PASSWORD and any((API_TOKEN, DEVICE_TOKEN, ADMIN_TOKEN, HERMES_TOKEN, PUBLIC_READ_TOKEN)):
        raise HTTPException(status_code=503, detail="CHILDCARE_ADMIN_PASSWORD must be configured")
    if ADMIN_PASSWORD and not hmac.compare_digest(payload.password, ADMIN_PASSWORD):
        raise HTTPException(status_code=401, detail="invalid admin password")
    response.set_cookie(
        "childcare_admin_session", session_value(), httponly=True, samesite="lax",
        secure=SESSION_COOKIE_SECURE, max_age=12 * 3600,
        path=request.scope.get("root_path", "") or "/",
    )
    return {"ok": True}


@app.post("/admin/logout")
def admin_logout(response: Response, request: Request) -> dict[str, bool]:
    response.delete_cookie("childcare_admin_session", path=request.scope.get("root_path", "") or "/")
    return {"ok": True}


@app.get("/admin")
def admin_page(request: Request) -> Response:
    require_admin_session(request)
    html = (ROOT / "web" / "index.html").read_text(encoding="utf-8")
    html = html.replace("__CHILDCARE_BASE_JSON__", json.dumps(request.scope.get("root_path", "")))
    return Response(html, media_type="text/html; charset=utf-8")


@app.get("/admin/api/children")
def admin_children(request: Request) -> dict:
    require_admin_session(request)
    with get_db() as db:
        rows = db.execute("SELECT * FROM children ORDER BY name").fetchall()
    return {"items": [row_dict(row) for row in rows]}


@app.get("/admin/api/audio")
def admin_audio(request: Request) -> dict:
    require_admin_session(request)
    with get_db() as db:
        rows = db.execute("SELECT * FROM audio_tracks WHERE active=1 ORDER BY category,title").fetchall()
    return {"items": [track_dict(row) for row in rows]}


@app.get("/admin/api/devices")
def admin_devices(request: Request) -> dict:
    require_admin_session(request)
    with get_db() as db:
        rows = db.execute("SELECT * FROM devices ORDER BY updated_at DESC").fetchall()
    return {"items": [row_dict(row) for row in rows]}


@app.on_event("startup")
def startup() -> None:
    init_db()
    from .personal import initialize
    initialize()
    seed_builtin_audio()


def authorize(
    authorization: Annotated[str | None, Header()] = None,
    x_api_key: Annotated[str | None, Header()] = None,
) -> None:
    configured = {token for token in (API_TOKEN, DEVICE_TOKEN, ADMIN_TOKEN, HERMES_TOKEN, PUBLIC_READ_TOKEN) if token}
    if not configured:
        return
    candidate = x_api_key or (authorization[7:] if authorization and authorization.startswith("Bearer ") else "")
    if candidate not in configured:
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="invalid API token")


Auth = Annotated[None, Depends(authorize)]


def token_for_role(role: str, candidate: str) -> bool:
    configured = {
        "device": {DEVICE_TOKEN, API_TOKEN},
        "admin": {ADMIN_TOKEN, API_TOKEN},
        "hermes": {HERMES_TOKEN, API_TOKEN},
        "public": {PUBLIC_READ_TOKEN, API_TOKEN},
        "external": {PUBLIC_READ_TOKEN, HERMES_TOKEN, ADMIN_TOKEN, API_TOKEN},
    }.get(role, set())
    return bool(candidate and candidate in {value for value in configured if value})


def authorize_role(role: str):
    def dependency(
        request: Request,
        authorization: Annotated[str | None, Header()] = None,
        x_api_key: Annotated[str | None, Header()] = None,
    ) -> None:
        candidate = x_api_key or (authorization[7:] if authorization and authorization.startswith("Bearer ") else "")
        if not any((API_TOKEN, DEVICE_TOKEN, ADMIN_TOKEN, HERMES_TOKEN, PUBLIC_READ_TOKEN)):
            return
        if role in ("admin", "write", "external") and session_is_valid(request.cookies.get("childcare_admin_session")):
            return
        if not token_for_role(role, candidate):
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail=f"{role} token required")
    return dependency


def authorize_roles(*roles: str):
    def dependency(
        request: Request,
        authorization: Annotated[str | None, Header()] = None,
        x_api_key: Annotated[str | None, Header()] = None,
    ) -> None:
        candidate = x_api_key or (authorization[7:] if authorization and authorization.startswith("Bearer ") else "")
        if not any((API_TOKEN, DEVICE_TOKEN, ADMIN_TOKEN, HERMES_TOKEN, PUBLIC_READ_TOKEN)):
            return
        if ("admin" in roles or "external" in roles) and session_is_valid(request.cookies.get("childcare_admin_session")):
            return
        if not any(token_for_role(role, candidate) for role in roles):
            raise HTTPException(status_code=status.HTTP_403_FORBIDDEN, detail=f"one of {roles} tokens required")
    return dependency


def session_value() -> str:
    timestamp = str(int(datetime.now(timezone.utc).timestamp()))
    nonce = secrets.token_urlsafe(18)
    payload = f"{timestamp}.{nonce}"
    signature = hmac.new(SESSION_SECRET.encode(), payload.encode(), hashlib.sha256).hexdigest()
    return f"{payload}.{signature}"


def session_is_valid(value: str | None) -> bool:
    if not value:
        return False
    try:
        timestamp, nonce, signature = value.split(".", 2)
        payload = f"{timestamp}.{nonce}"
        age = int(datetime.now(timezone.utc).timestamp()) - int(timestamp)
    except (ValueError, TypeError):
        return False
    expected = hmac.new(SESSION_SECRET.encode(), payload.encode(), hashlib.sha256).hexdigest()
    return 0 <= age <= 12 * 3600 and hmac.compare_digest(signature, expected)


def require_admin_session(request: Request) -> None:
    if not ADMIN_PASSWORD and any((API_TOKEN, DEVICE_TOKEN, ADMIN_TOKEN, HERMES_TOKEN, PUBLIC_READ_TOKEN)):
        raise HTTPException(status_code=503, detail="CHILDCARE_ADMIN_PASSWORD must be configured")
    if ADMIN_PASSWORD and not session_is_valid(request.cookies.get("childcare_admin_session")):
        raise HTTPException(status_code=status.HTTP_401_UNAUTHORIZED, detail="admin login required")


def row_dict(row: sqlite3.Row) -> dict:
    return dict(row)


def track_dict(row: sqlite3.Row) -> dict:
    data = row_dict(row)
    data["active"] = bool(data["active"])
    with get_db() as db:
        source=db.execute('SELECT source_url,creator,license,license_url,changes FROM audio_sources WHERE track_id=?',(data['id'],)).fetchone()
    if source: data['attribution']=dict(source)
    if not data.get("play_url") and data.get("file_name"):
        base = PUBLIC_BASE_URL or ""
        data["play_url"] = f"{base}/media/{quote(data['file_name'])}"
    return data


@app.get("/healthz")
def healthz() -> dict[str, str]:
    return {"status": "ok", "service": "xigua-childcare-api"}


@app.post("/v1/children", dependencies=[Depends(authorize_roles("admin"))])
def upsert_child(payload: ChildIn) -> dict:
    now = utc_now()
    with get_db() as db:
        db.execute(
            """INSERT INTO children(id,name,birth_date,timezone,created_at,updated_at)
               VALUES(?,?,?,?,?,?) ON CONFLICT(id) DO UPDATE SET name=excluded.name,
               birth_date=excluded.birth_date, timezone=excluded.timezone, updated_at=excluded.updated_at""",
            (payload.id, payload.name, payload.birth_date, payload.timezone, now, now),
        )
        row = db.execute("SELECT * FROM children WHERE id=?", (payload.id,)).fetchone()
    return row_dict(row)


@app.post("/v1/devices/register", dependencies=[Depends(authorize_roles("admin"))])
def register_device(payload: DeviceIn) -> dict:
    now = utc_now()
    with get_db() as db:
        if not db.execute("SELECT 1 FROM children WHERE id=?", (payload.child_id,)).fetchone():
            raise HTTPException(status_code=404, detail="child not found")
        db.execute(
            """INSERT INTO devices(id,name,child_id,last_seen_at,created_at,updated_at)
               VALUES(?,?,?,NULL,?,?) ON CONFLICT(id) DO UPDATE SET name=excluded.name,
               child_id=excluded.child_id, updated_at=excluded.updated_at""",
            (payload.id, payload.name, payload.child_id, now, now),
        )
        row = db.execute("SELECT * FROM devices WHERE id=?", (payload.id,)).fetchone()
    return row_dict(row)


def require_device(db: sqlite3.Connection, device_id: str, child_id: str) -> None:
    if not db.execute("SELECT 1 FROM devices WHERE id=? AND child_id=?", (device_id, child_id)).fetchone():
        raise HTTPException(status_code=404, detail="device not registered for child")


@app.post("/v1/children/{child_id}/events", dependencies=[Depends(authorize_roles("device"))])
def ingest_events(child_id: str, device_id: Annotated[str, Header(alias="X-Device-Id")], payload: EventBatch) -> dict:
    now = utc_now()
    accepted = 0
    duplicates = 0
    with get_db() as db:
        if not db.execute("SELECT 1 FROM children WHERE id=?", (child_id,)).fetchone():
            raise HTTPException(status_code=404, detail="child not found")
        require_device(db, device_id, child_id)
        for event in payload.events:
            if db.execute("SELECT 1 FROM deleted_records WHERE child_id=? AND client_event_id=?",(child_id,event.id)).fetchone():
                duplicates += 1
                continue
            cursor = db.execute(
                """INSERT OR IGNORE INTO events
                (id,client_event_id,child_id,device_id,type,occurred_at,amount_ml,ingredient,
                 diaper_kind,duration_min,start_at,end_at,timer_seconds,note,raw_json,created_at)
                VALUES (?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?)""",
                (
                    str(uuid.uuid4()), event.id, child_id, device_id, event.type, iso(event.occurred_at),
                    event.amount_ml, event.ingredient, event.diaper_kind, event.duration_min,
                    iso(event.start_at) if event.start_at else None, iso(event.end_at) if event.end_at else None,
                    event.timer_seconds, event.note, json.dumps(event.metadata, ensure_ascii=False), now,
                ),
            )
            if cursor.rowcount:
                accepted += 1
            else:
                duplicates += 1
        db.execute("UPDATE devices SET last_seen_at=?, updated_at=? WHERE id=?", (now, now, device_id))
    return {"accepted": accepted, "duplicates": duplicates, "server_time": now}


@app.get("/v1/children/{child_id}/events", dependencies=[Depends(authorize_roles("external"))])
def list_events(
    child_id: str,
    type: str | None = Query(default=None),
    since: datetime | None = Query(default=None),
    until: datetime | None = Query(default=None),
    limit: int = Query(default=100, ge=1, le=500),
) -> dict:
    clauses = ["child_id=?"]
    values: list[object] = [child_id]
    if type:
        if type not in EVENT_TYPES:
            raise HTTPException(status_code=400, detail="unsupported event type")
        clauses.append("type=?")
        values.append(type)
    if since:
        clauses.append("occurred_at>=?")
        values.append(iso(since))
    if until:
        clauses.append("occurred_at<?")
        values.append(iso(until))
    values.append(limit)
    with get_db() as db:
        if not db.execute("SELECT 1 FROM children WHERE id=?", (child_id,)).fetchone():
            raise HTTPException(status_code=404, detail="child not found")
        rows = db.execute(
            f"SELECT * FROM events WHERE {' AND '.join(clauses)} ORDER BY occurred_at DESC LIMIT ?", values
        ).fetchall()
    return {"items": [row_dict(row) for row in rows], "count": len(rows)}


@app.get("/v1/children/{child_id}/summary", dependencies=[Depends(authorize_roles("external"))])
def summary(child_id: str, day: str | None = Query(default=None, pattern=r"^\d{4}-\d{2}-\d{2}$")) -> dict:
    with get_db() as db:
        child = db.execute("SELECT timezone FROM children WHERE id=?", (child_id,)).fetchone()
        if not child:
            raise HTTPException(status_code=404, detail="child not found")
        try:
            zone = ZoneInfo(child["timezone"])
            local_day = date.fromisoformat(day) if day else datetime.now(zone).date()
        except (ValueError, ZoneInfoNotFoundError) as exc:
            raise HTTPException(status_code=400, detail="invalid day or child timezone") from exc
        day = local_day.isoformat()
        start = iso(datetime.combine(local_day, time.min, zone))
        end = iso(datetime.combine(local_day + timedelta(days=1), time.min, zone))
        row = db.execute(
            """SELECT COUNT(*) AS total_events,
              COALESCE(SUM(CASE WHEN type='feeding' THEN 1 ELSE 0 END),0) AS feeding_count,
              COALESCE(SUM(CASE WHEN type='feeding' THEN amount_ml ELSE 0 END),0) AS milk_ml,
              COALESCE(SUM(CASE WHEN type='diaper' THEN 1 ELSE 0 END),0) AS diaper_count,
              COALESCE(SUM(CASE WHEN type='sleep' THEN 1 ELSE 0 END),0) AS sleep_count,
              COALESCE(SUM(CASE WHEN type='sleep' THEN duration_min ELSE 0 END),0) AS sleep_minutes,
              COALESCE(SUM(CASE WHEN type='bath' THEN 1 ELSE 0 END),0) AS bath_count,
              COALESCE(SUM(CASE WHEN type='tummy' THEN 1 ELSE 0 END),0) AS tummy_count
            FROM events WHERE child_id=? AND occurred_at>=? AND occurred_at<?""",
            (child_id, start, end),
        ).fetchone()
    return {"child_id": child_id, "day": day, **row_dict(row)}


@app.post("/v1/audio/tracks", dependencies=[Depends(authorize_roles("admin", "hermes"))])
def create_track(payload: TrackIn) -> dict:
    track_id = payload.id or str(uuid.uuid4())
    if payload.file_name and (Path(payload.file_name).is_absolute() or ".." in Path(payload.file_name).parts):
        raise HTTPException(status_code=400, detail="file_name must stay inside media directory")
    if not payload.play_url and not payload.file_name:
        raise HTTPException(status_code=400, detail="play_url or file_name is required")
    now = utc_now()
    with get_db() as db:
        db.execute(
            """INSERT INTO audio_tracks(id,title,category,file_name,play_url,mime_type,duration_ms,sha256,size_bytes,active,created_at,updated_at)
               VALUES(?,?,?,?,?,?,?,?,?,1,?,?) ON CONFLICT(id) DO UPDATE SET title=excluded.title,
               category=excluded.category,file_name=excluded.file_name,play_url=excluded.play_url,
               mime_type=excluded.mime_type,duration_ms=excluded.duration_ms,sha256=excluded.sha256,
               size_bytes=excluded.size_bytes,active=1,updated_at=excluded.updated_at""",
            (track_id, payload.title, payload.category, payload.file_name, payload.play_url, payload.mime_type,
             payload.duration_ms, payload.sha256, payload.size_bytes, now, now),
        )
        row = db.execute("SELECT * FROM audio_tracks WHERE id=?", (track_id,)).fetchone()
    return track_dict(row)


@app.get("/v1/audio/tracks", dependencies=[Depends(authorize_roles("external", "device"))])
def list_tracks(category: str | None = Query(default=None)) -> dict:
    if category and category not in AUDIO_CATEGORIES:
        raise HTTPException(status_code=400, detail="unsupported audio category")
    with get_db() as db:
        rows = db.execute(
            "SELECT * FROM audio_tracks WHERE active=1" + (" AND category=?" if category else "") + " ORDER BY category,title",
            (category,) if category else (),
        ).fetchall()
    return {"items": [track_dict(row) for row in rows], "count": len(rows)}


@app.get("/v1/audio/catalog", dependencies=[Depends(authorize_roles("external", "device"))])
def device_catalog(category: str, offset: int = Query(default=0, ge=0),
                   limit: int = Query(default=8, ge=1, le=16)) -> dict:
    """Small pages of device-compatible media, without the web attribution body."""
    if category not in AUDIO_CATEGORIES:
        raise HTTPException(status_code=400, detail="unsupported audio category")
    predicate = "active=1 AND category=? AND mime_type='audio/wav' AND file_name IS NOT NULL"
    with get_db() as db:
        total = db.execute("SELECT COUNT(*) FROM audio_tracks WHERE " + predicate, (category,)).fetchone()[0]
        rows = db.execute("SELECT id,title,category,file_name,play_url,mime_type FROM audio_tracks WHERE " +
                          predicate + " ORDER BY title,id LIMIT ? OFFSET ?", (category, limit, offset)).fetchall()
    items = [{"id": row["id"], "title": row["title"], "category": row["category"],
              "mime_type": row["mime_type"],
              "play_url": row["play_url"] or f"{PUBLIC_BASE_URL}/media/{quote(row['file_name'])}"} for row in rows]
    return {"items": items, "count": len(items), "total": total, "offset": offset,
            "next_offset": offset + len(items) if offset + len(items) < total else None}


@app.post("/v1/devices/{device_id}/audio/{track_id}/playback", dependencies=[Depends(authorize_roles("device"))])
def record_playback(device_id: str, track_id: str, payload: PlaybackIn) -> dict:
    now = utc_now()
    with get_db() as db:
        if not db.execute("SELECT 1 FROM devices WHERE id=?", (device_id,)).fetchone():
            raise HTTPException(status_code=404, detail="device not found")
        row = db.execute("SELECT * FROM audio_tracks WHERE id=? AND active=1", (track_id,)).fetchone()
        if not row:
            raise HTTPException(status_code=404, detail="track not found")
        db.execute(
            "INSERT INTO playback_events(id,device_id,track_id,action,position_ms,occurred_at,error) VALUES(?,?,?,?,?,?,?)",
            (str(uuid.uuid4()), device_id, track_id, payload.action, payload.position_ms,
             iso(payload.occurred_at) if payload.occurred_at else now, payload.error),
        )
    return {"accepted": True, "server_time": now}


@app.get("/v1/audio/tracks/{track_id}", dependencies=[Depends(authorize_roles("external", "device"))])
def get_track(track_id: str) -> dict:
    with get_db() as db:
        row = db.execute("SELECT * FROM audio_tracks WHERE id=? AND active=1", (track_id,)).fetchone()
    if not row:
        raise HTTPException(status_code=404, detail="track not found")
    return track_dict(row)


def export_child(child_id: str, since: datetime | None, until: datetime | None, limit: int) -> dict:
    clauses = ["child_id=?"]
    values: list[object] = [child_id]
    if since:
        clauses.append("occurred_at>=?")
        values.append(iso(since))
    if until:
        clauses.append("occurred_at<?")
        values.append(iso(until))
    values.append(limit)
    with get_db() as db:
        child = db.execute("SELECT * FROM children WHERE id=?", (child_id,)).fetchone()
        if not child:
            raise HTTPException(status_code=404, detail="child not found")
        events = db.execute(
            f"SELECT * FROM events WHERE {' AND '.join(clauses)} ORDER BY occurred_at DESC LIMIT ?", values
        ).fetchall()
        tracks = db.execute("SELECT * FROM audio_tracks WHERE active=1 ORDER BY category,title").fetchall()
    return {
        "schema": "xigua-childcare-export-v1",
        "exported_at": utc_now(),
        "child": row_dict(child),
        "summary": summary(child_id, None),
        "events": [row_dict(row) for row in events],
        "audio_tracks": [track_dict(row) for row in tracks],
    }


@app.get("/v1/export/children/{child_id}", dependencies=[Depends(authorize_role("external"))])
def export_child_data(
    child_id: str,
    since: datetime | None = Query(default=None),
    until: datetime | None = Query(default=None),
    limit: int = Query(default=500, ge=1, le=5000),
) -> dict:
    """Stable JSON export for Hermes, analytics jobs, and trusted clients."""
    return export_child(child_id, since, until, limit)


@app.get("/v1/hermes/children/{child_id}/analysis-input", dependencies=[Depends(authorize_role("hermes"))])
def hermes_analysis_input(
    child_id: str,
    since: datetime | None = Query(default=None),
    until: datetime | None = Query(default=None),
    limit: int = Query(default=500, ge=1, le=5000),
) -> dict:
    """Hermes-scoped export; it never returns API tokens or raw audio bytes."""
    return export_child(child_id, since, until, limit)


@app.get("/v1/public/audio/manifest", dependencies=[Depends(authorize_role("external"))])
def public_audio_manifest(category: str | None = Query(default=None)) -> dict:
    if category and category not in AUDIO_CATEGORIES:
        raise HTTPException(status_code=400, detail="unsupported audio category")
    return list_tracks(category)


@app.post("/v1/hermes/audio/tracks", dependencies=[Depends(authorize_role("hermes"))])
def hermes_create_audio_track(payload: TrackIn) -> dict:
    """Allow Hermes to add catalogue metadata, with no child/event write access."""
    return create_track(payload)


@app.put("/v1/hermes/audio/files/{file_name:path}", dependencies=[Depends(authorize_role("hermes"))])
async def hermes_upload_audio(file_name: str, request: Request) -> dict:
    """Upload one bounded audio file; register its metadata separately."""
    relative = Path(file_name)
    if relative.is_absolute() or ".." in relative.parts or not relative.name:
        raise HTTPException(status_code=400, detail="invalid media file name")
    declared = request.headers.get("content-length")
    if declared:
        try:
            if int(declared) > 25 * 1024 * 1024:
                raise HTTPException(status_code=413, detail="audio file exceeds 25 MiB")
        except ValueError as exc:
            raise HTTPException(status_code=400, detail="invalid content length") from exc
    body = await request.body()
    if len(body) > 25 * 1024 * 1024:
        raise HTTPException(status_code=413, detail="audio file exceeds 25 MiB")
    destination = MEDIA_DIR / relative
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(body)
    return {
        "file_name": relative.as_posix(),
        "size_bytes": len(body),
        "sha256": hashlib.sha256(body).hexdigest(),
        "play_url": f"{PUBLIC_BASE_URL}/media/{quote(relative.as_posix())}",
    }


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for block in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


@app.get("/admin/api/skills/{role}", dependencies=[Depends(authorize_roles("admin"))])
def download_childcare_skill(role: Literal["hermes", "public", "device"]) -> Response:
    from .skill_bundle import build_bundle
    from .personal import ids
    child_id, device_id = ids()
    try:
        bundle = build_bundle(role, PUBLIC_BASE_URL, {
            "hermes": HERMES_TOKEN, "public": PUBLIC_READ_TOKEN, "device": DEVICE_TOKEN,
        }[role], child_id, device_id)
    except ValueError as exc:
        raise HTTPException(503, str(exc)) from exc
    return Response(bundle, media_type="application/zip", headers={
        "Content-Disposition": f'attachment; filename="childcare-{role}.zip"',
        "Cache-Control": "no-store", "X-Content-Type-Options": "nosniff",
    })


from .personal import router as personal_router
app.include_router(personal_router)
from .care import router as care_router
app.include_router(care_router)


@app.get("/media/{file_name:path}", dependencies=[Depends(authorize_roles("admin", "device", "external"))])
def protected_media(file_name: str):
    from fastapi.responses import FileResponse
    path = (MEDIA_DIR / file_name).resolve()
    if not path.is_relative_to(MEDIA_DIR.resolve()) or not path.is_file():
        raise HTTPException(404, "audio file not found")
    return FileResponse(path)
