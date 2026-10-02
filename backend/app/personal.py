"""Single-family console, analytics and versioned device snapshot protocol."""
import csv
import io
import json
import os
import uuid
import wave
from collections import Counter
from datetime import date, datetime, time, timedelta
from pathlib import Path
from typing import Literal
from zoneinfo import ZoneInfo

from fastapi import APIRouter, Depends, HTTPException, Query, Request, Response
from pydantic import BaseModel, Field, model_validator

from . import main as m

router = APIRouter()
ADMIN = Depends(m.authorize_roles("admin"))
DEVICE = Depends(m.authorize_roles("device"))


def initialize():
    now = m.utc_now()
    with m.get_db() as db:
        db.executescript('''
        CREATE TABLE IF NOT EXISTS personal_settings (key TEXT PRIMARY KEY, value TEXT NOT NULL);
        CREATE TABLE IF NOT EXISTS device_snapshots (device_id TEXT PRIMARY KEY, revision INTEGER NOT NULL,
          floor INTEGER NOT NULL, sleeping_since TEXT, received_at TEXT NOT NULL, firmware TEXT, pending INTEGER DEFAULT 0);
        CREATE TABLE IF NOT EXISTS device_commands (id TEXT PRIMARY KEY, action TEXT NOT NULL, track_id TEXT,
          status TEXT NOT NULL, created_at TEXT NOT NULL, updated_at TEXT NOT NULL, error TEXT);
        CREATE TABLE IF NOT EXISTS deleted_records (id TEXT PRIMARY KEY, child_id TEXT NOT NULL,
          client_event_id TEXT NOT NULL, deleted_at TEXT NOT NULL, record_json TEXT NOT NULL,
          UNIQUE(child_id,client_event_id));
        CREATE TABLE IF NOT EXISTS audio_sources (track_id TEXT PRIMARY KEY, source_url TEXT NOT NULL,
          creator TEXT NOT NULL, license TEXT NOT NULL, license_url TEXT NOT NULL, changes TEXT NOT NULL);
        ''')
        columns = {r[1] for r in db.execute('PRAGMA table_info(events)')}
        if 'sync_seq' not in columns:
            db.execute('ALTER TABLE events ADD COLUMN sync_seq INTEGER')
        if 'time_known' not in columns:
            db.execute('ALTER TABLE events ADD COLUMN time_known INTEGER NOT NULL DEFAULT 1')
        if 'duration_known' not in columns:
            db.execute('ALTER TABLE events ADD COLUMN duration_known INTEGER NOT NULL DEFAULT 1')
        child = db.execute("SELECT value FROM personal_settings WHERE key='child_id'").fetchone()
        if not child:
            first = db.execute('SELECT id FROM children ORDER BY created_at LIMIT 1').fetchone()
            child_id = first['id'] if first else 'baby'
            db.execute('INSERT OR IGNORE INTO children VALUES(?,?,?,?,?,?)', (child_id,'宝宝',None,'Asia/Shanghai',now,now))
            db.execute('INSERT INTO personal_settings VALUES(?,?)', ('child_id',child_id))
        else:
            child_id = child['value']
        device = db.execute("SELECT value FROM personal_settings WHERE key='device_id'").fetchone()
        if not device:
            first = db.execute('SELECT id FROM devices WHERE child_id=? ORDER BY created_at LIMIT 1',(child_id,)).fetchone()
            device_id = first['id'] if first else 'xigua-device'
            db.execute('INSERT OR IGNORE INTO devices VALUES(?,?,?,NULL,?,?)',(device_id,'西瓜育儿设备',child_id,now,now))
            db.execute('INSERT INTO personal_settings VALUES(?,?)',('device_id',device_id))


def ids():
    with m.get_db() as db:
        s = dict(db.execute('SELECT key,value FROM personal_settings').fetchall())
    return s['child_id'], s['device_id']


def context():
    child_id, device_id = ids()
    with m.get_db() as db:
        child = dict(db.execute('SELECT * FROM children WHERE id=?',(child_id,)).fetchone())
        device = dict(db.execute('SELECT * FROM devices WHERE id=?',(device_id,)).fetchone())
        snap = db.execute('SELECT * FROM device_snapshots WHERE device_id=?',(device_id,)).fetchone()
        totals = dict(db.execute('SELECT COUNT(*) AS records, SUM(CASE WHEN time_known=0 THEN 1 ELSE 0 END) AS untimed FROM events WHERE child_id=?',(child_id,)).fetchone())
    return {'child':child,'device':device,'sync':dict(snap) if snap else None,'totals':totals}


@router.get('/admin/api/personal',dependencies=[ADMIN])
def personal():
    return context()


class Profile(BaseModel):
    name: str = Field(min_length=1,max_length=80)
    birth_date: date | None = None
    timezone: str = 'Asia/Shanghai'


@router.put('/admin/api/personal',dependencies=[ADMIN])
def profile(payload: Profile):
    child, _ = ids()
    return m.upsert_child(m.ChildIn(id=child,name=payload.name,birth_date=payload.birth_date.isoformat() if payload.birth_date else None,timezone=payload.timezone))


class SnapshotEvent(BaseModel):
    seq: int = Field(ge=1,le=2**53-1)
    type: Literal['feeding','diaper','sleep','bath','tummy','timer']
    epoch: int = Field(ge=0)
    amount_ml: int | None = Field(default=None,ge=0,le=2000)
    ingredient: str | None = Field(default=None,max_length=32)
    diaper_kind: Literal['pee','poop','both'] | None = None
    duration_min: int | None = Field(default=None,ge=0,le=65535)
    start_epoch: int = Field(default=0,ge=0)
    end_epoch: int = Field(default=0,ge=0)
    duration_known: bool = True

    @model_validator(mode='after')
    def validate_record(self):
        if self.type=='feeding' and self.amount_ml is None: raise ValueError('milk amount required')
        if self.type=='diaper' and self.diaper_kind is None: raise ValueError('diaper kind required')
        if self.type=='sleep' and self.duration_min is None: raise ValueError('sleep duration required')
        if self.start_epoch and self.end_epoch and self.end_epoch < self.start_epoch: raise ValueError('sleep end precedes start')
        return self


class Snapshot(BaseModel):
    schema: Literal['xigua-device-snapshot-v1']
    revision: int = Field(ge=1,le=2**53-1)
    floor: int = Field(ge=1,le=2**53-1)
    events: list[SnapshotEvent] = Field(max_length=32)
    sleeping_since: int = Field(default=0,ge=0)
    firmware: str = Field(default='unknown',max_length=80)

    @model_validator(mode='after')
    def unique_seqs(self):
        seqs = [e.seq for e in self.events]
        if len(set(seqs))!=len(seqs) or any(n < self.floor for n in seqs): raise ValueError('invalid snapshot sequence window')
        return self


def epoch_iso(epoch):
    try:
        return m.iso(datetime.fromtimestamp(epoch,m.timezone.utc)) if epoch >= 1700000000 else None
    except (OverflowError, ValueError, OSError) as exc:
        raise HTTPException(422,'timestamp out of range') from exc


@router.get('/v1/device/config',dependencies=[DEVICE])
def device_config():
    child, device = ids()
    return {'schema':'xigua-device-config-v1','child_id':child,'device_id':device,
            'snapshot_path':'/v1/device/snapshot','audio_path':'/v1/audio/tracks',
            'max_snapshot_records':32,'audio_format':'PCM WAV, mono, 16-bit, 12000 Hz'}


@router.post('/v1/device/snapshot',dependencies=[DEVICE])
def snapshot(payload: Snapshot):
    child, device = ids()
    now = m.utc_now()
    with m.get_db() as db:
        last=db.execute('SELECT * FROM device_snapshots WHERE device_id=?',(device,)).fetchone()
        if last and payload.revision <= last['revision']:
            db.execute('UPDATE devices SET last_seen_at=? WHERE id=?',(now,device))
            return {'accepted':True,'stale':True,'revision':last['revision']}
        if last and payload.floor < last['floor']: raise HTTPException(409,'device sequence regressed; preserve NVS')
        # Reconcile ONLY the recent sequence window. Older cloud history survives ring rollover.
        seqs=[e.seq for e in payload.events]
        sql='DELETE FROM events WHERE child_id=? AND device_id=? AND sync_seq>=?'
        args=[child,device,payload.floor]
        if seqs:
            sql+=' AND sync_seq NOT IN ('+','.join('?' for _ in seqs)+')'
            args+=seqs
        db.execute(sql,args)
        for e in payload.events:
            if db.execute('SELECT 1 FROM deleted_records WHERE child_id=? AND client_event_id=?',
                          (child,'device:'+str(e.seq))).fetchone():
                continue
            occurred=epoch_iso(e.epoch)
            start,end=epoch_iso(e.start_epoch),epoch_iso(e.end_epoch)
            db.execute('''INSERT INTO events
            (id,client_event_id,child_id,device_id,type,occurred_at,amount_ml,ingredient,diaper_kind,
             duration_min,start_at,end_at,raw_json,created_at,sync_seq,time_known,duration_known)
            VALUES(?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?,?) ON CONFLICT(child_id,client_event_id) DO UPDATE SET
             occurred_at=excluded.occurred_at,amount_ml=excluded.amount_ml,ingredient=excluded.ingredient,
             duration_min=excluded.duration_min,
             start_at=excluded.start_at,end_at=excluded.end_at,time_known=excluded.time_known,duration_known=excluded.duration_known''',
            (str(uuid.uuid4()),'device:'+str(e.seq),child,device,e.type,occurred or '',e.amount_ml,e.ingredient,e.diaper_kind,
             e.duration_min,start,end,json.dumps({'source':'firmware','seq':e.seq}),now,e.seq,int(bool(occurred)),int(e.duration_known)))
        db.execute('''INSERT INTO device_snapshots(device_id,revision,floor,sleeping_since,received_at,firmware)
          VALUES(?,?,?,?,?,?) ON CONFLICT(device_id) DO UPDATE SET revision=excluded.revision,floor=excluded.floor,
          sleeping_since=excluded.sleeping_since,received_at=excluded.received_at,firmware=excluded.firmware''',
          (device,payload.revision,payload.floor,epoch_iso(payload.sleeping_since),now,payload.firmware))
        db.execute('UPDATE devices SET last_seen_at=?,updated_at=? WHERE id=?',(now,now,device))
    return {'accepted':True,'stale':False,'revision':payload.revision,'records_in_snapshot':len(seqs)}


def bounds(start,end,zone):
    try:
        lo=date.fromisoformat(start); hi=date.fromisoformat(end)
    except ValueError as exc: raise HTTPException(400,'invalid date') from exc
    if hi < lo or (hi-lo).days>366: raise HTTPException(400,'choose a range of at most 367 days')
    return lo,hi,datetime.combine(lo,time.min,zone),datetime.combine(hi+timedelta(days=1),time.min,zone)


def period(start=None,end=None):
    c=context(); zone=ZoneInfo(c['child']['timezone']); today=datetime.now(zone).date()
    lo,hi,a,b=bounds(start or (today-timedelta(days=6)).isoformat(),end or today.isoformat(),zone)
    return c,zone,lo,hi,a,b


def event_rows(c,a,b,kind=None):
    with m.get_db() as db:
        sql='SELECT * FROM events WHERE child_id=? AND occurred_at>=? AND occurred_at<?'
        values=[c['child']['id'],m.iso(a),m.iso(b)]
        if kind:
            if kind not in m.EVENT_TYPES: raise HTTPException(400,'invalid record type')
            sql+=' AND type=?'; values.append(kind)
        return [dict(r) for r in db.execute(sql+' ORDER BY occurred_at DESC',values)]


@router.get('/admin/api/records',dependencies=[ADMIN])
def records(start: str | None=None,end: str | None=None,type: str | None=None,
            offset: int=Query(0,ge=0),limit: int=Query(100,ge=1,le=500)):
    c,zone,lo,hi,a,b=period(start,end)
    rows=event_rows(c,a,b,type)
    with m.get_db() as db:
        untimed=[dict(r) for r in db.execute('SELECT * FROM events WHERE child_id=? AND time_known=0 ORDER BY created_at DESC',(c['child']['id'],))]
    return {'items':rows[offset:offset+limit],'total':len(rows),'offset':offset,'untimed':untimed,'timezone':str(zone)}


@router.post('/admin/api/records',dependencies=[ADMIN])
def add_record(event: m.EventIn):
    child,device=ids()
    return m.ingest_events(child,device,m.EventBatch(events=[event]))


def archive_records(db, rows):
    now=m.utc_now()
    for row in rows:
        db.execute('INSERT OR IGNORE INTO deleted_records VALUES(?,?,?,?,?)',
                   (row['id'],row['child_id'],row['client_event_id'],now,json.dumps(dict(row))))
        db.execute('DELETE FROM events WHERE id=?',(row['id'],))
    return len(rows)


@router.delete('/admin/api/records/{record_id}',dependencies=[ADMIN])
def delete_record(record_id: str):
    child,_=ids()
    with m.get_db() as db:
        row=db.execute('SELECT * FROM events WHERE id=? AND child_id=?',(record_id,child)).fetchone()
        if not row: raise HTTPException(404,'record not found')
        archive_records(db,[row])
    return {'deleted':True,'recoverable':True}


@router.get('/admin/api/trash',dependencies=[ADMIN])
def trash(offset: int=Query(0,ge=0),limit: int=Query(100,ge=1,le=500)):
    child,_=ids()
    with m.get_db() as db:
        rows=db.execute('SELECT * FROM deleted_records WHERE child_id=? ORDER BY deleted_at DESC LIMIT ? OFFSET ?',
                        (child,limit,offset)).fetchall()
        total=db.execute('SELECT COUNT(*) FROM deleted_records WHERE child_id=?',(child,)).fetchone()[0]
    return {'items':[{'deleted_at':r['deleted_at'],**json.loads(r['record_json'])} for r in rows],'total':total}


@router.post('/admin/api/trash/{record_id}/restore',dependencies=[ADMIN])
def restore_record(record_id: str):
    child,_=ids()
    with m.get_db() as db:
        row=db.execute('SELECT * FROM deleted_records WHERE id=? AND child_id=?',(record_id,child)).fetchone()
        if not row: raise HTTPException(404,'deleted record not found')
        record=json.loads(row['record_json'])
        columns={r[1] for r in db.execute('PRAGMA table_info(events)')}
        values={k:v for k,v in record.items() if k in columns}
        db.execute('INSERT INTO events ('+','.join(values)+') VALUES ('+','.join('?' for _ in values)+')',list(values.values()))
        db.execute('DELETE FROM deleted_records WHERE id=?',(record_id,))
    return {'restored':True}


@router.post('/admin/api/records/clear',dependencies=[ADMIN])
def clear_records():
    child,_=ids()
    now=m.utc_now(); ident=str(uuid.uuid4())
    with m.get_db() as db:
        rows=db.execute('SELECT * FROM events WHERE child_id=?',(child,)).fetchall()
        count=archive_records(db,rows)
        # Clearing the web records must clear the device ring as well.  Treat
        # this as a high-priority command so the next snapshot cannot restore
        # records that the administrator just archived.
        db.execute("UPDATE device_commands SET status='superseded',updated_at=? "
                   "WHERE status IN ('queued','received')",(now,))
        db.execute('INSERT INTO device_commands VALUES(?,?,?,?,?,?,NULL)',
                   (ident,'clear_records',None,'queued',now,now))
    return {'deleted':count,'recoverable':True,'device_command_id':ident}


@router.get('/admin/api/statistics',dependencies=[ADMIN])
def statistics(start: str | None=None,end: str | None=None):
    c,zone,lo,hi,a,b=period(start,end)
    buckets={}
    d=lo
    while d<=hi:
        buckets[d.isoformat()]={'day':d.isoformat(),'milk_ml':0,'feeding_count':0,'sleep_minutes':0,'sleep_count':0,'diaper_count':0,'pee_count':0,'poop_count':0,'bath_count':0,'tummy_count':0}
        d+=timedelta(days=1)
    rows=event_rows(c,a,b)
    for e in rows:
        r=buckets[datetime.fromisoformat(e['occurred_at']).astimezone(zone).date().isoformat()]
        if e['type']=='feeding': r['milk_ml']+=e['amount_ml'] or 0; r['feeding_count']+=1
        if e['type']=='diaper':
            r['diaper_count']+=1
            r['pee_count']+=int(e['diaper_kind'] in ('pee','both'))
            r['poop_count']+=int(e['diaper_kind'] in ('poop','both'))
        if e['type'] in ('bath','tummy'): r[e['type']+'_count']+=1
    # Attribute exact sleep intervals to each local calendar day, including crossing midnight.
    with m.get_db() as db:
        sleeps=[dict(r) for r in db.execute("SELECT * FROM events WHERE child_id=? AND type='sleep' AND time_known=1",(c['child']['id'],))]
    for e in sleeps:
        if not e['duration_known']: continue
        finish=datetime.fromisoformat(e['end_at'] or e['occurred_at']).astimezone(zone)
        begin=datetime.fromisoformat(e['start_at']).astimezone(zone) if e['start_at'] else finish-timedelta(minutes=e['duration_min'] or 0)
        if finish<begin: continue
        for day,r in buckets.items():
            da=datetime.combine(date.fromisoformat(day),time.min,zone)
            db=da+timedelta(days=1)
            seconds=max(0,(min(finish,db).astimezone(m.timezone.utc)-max(begin,da).astimezone(m.timezone.utc)).total_seconds())
            if seconds: r['sleep_minutes']+=seconds/60; r['sleep_count']+=1
    series=list(buckets.values())
    for r in series: r['sleep_minutes']=round(r['sleep_minutes'],1)
    totals={k:sum(r[k] for r in series) for k in series[0] if k!='day'}
    feeding=sorted(datetime.fromisoformat(e['occurred_at']) for e in rows if e['type']=='feeding')
    intervals=[(q-p).total_seconds()/3600 for p,q in zip(feeding,feeding[1:])]
    return {'start':lo.isoformat(),'end':hi.isoformat(),'timezone':str(zone),'series':series,'totals':totals,
            'average_milk_ml_per_day':round(totals['milk_ml']/len(series),1),
            'average_feeding_interval_hours':round(sum(intervals)/len(intervals),2) if intervals else None,
            'context':c}


@router.get('/admin/api/export',dependencies=[ADMIN])
def export(start: str | None=None,end: str | None=None,format: Literal['json','csv']='json'):
    c,zone,lo,hi,a,b=period(start,end)
    rows=event_rows(c,a,b)
    with m.get_db() as db:
        unknown=[dict(r) for r in db.execute('SELECT * FROM events WHERE child_id=? AND time_known=0',(c['child']['id'],))]
    rows+=unknown
    if format=='json':
        body=json.dumps({'schema':'xigua-childcare-console-export-v1','child':c['child'],'device':c['device'],
            'start':lo.isoformat(),'end':hi.isoformat(),'timezone':str(zone),'events':rows,'statistics':statistics(start,end)},ensure_ascii=False)
        mime='application/json'; ext='json'
    else:
        out=io.StringIO(); columns=['id','client_event_id','type','occurred_at','local_time','amount_ml','ingredient','diaper_kind','duration_min','start_at','end_at','note','time_known','duration_known']
        writer=csv.DictWriter(out,fieldnames=columns,extrasaction='ignore'); writer.writeheader()
        for e in rows:
            e=dict(e); e['local_time']=datetime.fromisoformat(e['occurred_at']).astimezone(zone).isoformat() if e['occurred_at'] else ''
            # Spreadsheet formula injection is not permitted in notes/labels.
            writer.writerow({k:('\''+str(v) if isinstance(v,str) and v.startswith(('=','+','-','@','\t','\r')) else v) for k,v in e.items()})
        body='\ufeff'+out.getvalue(); mime='text/csv'; ext='csv'
    return Response(body,media_type=mime,headers={'Content-Disposition':f'attachment; filename="childcare-{lo}-{hi}.{ext}"','Cache-Control':'no-store'})


@router.put('/admin/api/audio/files',dependencies=[ADMIN])
async def upload_audio(request: Request):
    # Canonical WAV header and format allow bounded-memory playback on ESP32-C3.
    body=bytearray()
    async for chunk in request.stream():
        body.extend(chunk)
        if len(body)>25*1024*1024: raise HTTPException(413,'audio exceeds 25 MiB')
    try:
        with wave.open(io.BytesIO(body),'rb') as source:
            if (source.getnchannels(),source.getsampwidth(),source.getframerate(),source.getcomptype())!=(1,2,12000,'NONE'):
                raise ValueError('format')
            frames=source.readframes(source.getnframes())
            if len(frames)!=source.getnframes()*2: raise ValueError('truncated')
        canonical=io.BytesIO()
        with wave.open(canonical,'wb') as dest:
            dest.setnchannels(1); dest.setsampwidth(2); dest.setframerate(12000); dest.writeframes(frames)
        data=canonical.getvalue()
    except (wave.Error,EOFError,ValueError):
        raise HTTPException(422,'Please upload PCM WAV: mono, 16-bit, 12000 Hz')
    name=uuid.uuid4().hex+'.wav'; path=m.MEDIA_DIR/name
    path.write_bytes(data)
    return {'file_name':name,'mime_type':'audio/wav','size_bytes':len(data),'sha256':m.file_sha256(path),
            'duration_ms':len(frames)*1000//24000,'play_url':f'{m.PUBLIC_BASE_URL}/media/{name}'}


class TrackPatch(BaseModel):
    title: str | None = Field(None,min_length=1,max_length=120)
    category: Literal['song','white_noise','story','classical','other'] | None = None
    active: bool | None = None
    source_url: str | None = Field(default=None,max_length=1000)
    creator: str | None = Field(default=None,max_length=200)
    license: str | None = Field(default=None,max_length=100)
    license_url: str | None = Field(default=None,max_length=1000)
    changes: str | None = Field(default=None,max_length=500)


@router.patch('/admin/api/audio/{track_id}',dependencies=[ADMIN])
def edit_track(track_id: str,payload: TrackPatch):
    values=payload.model_dump(exclude_none=True)
    attribution={k:values.pop(k) for k in ('source_url','creator','license','license_url','changes') if k in values}
    if attribution and (len(attribution)!=5 or not all(v for v in attribution.values())):
        raise HTTPException(422,'complete audio attribution required')
    if attribution and any(not attribution[k].startswith(('https://','http://')) for k in ('source_url','license_url')):
        raise HTTPException(422,'invalid attribution URL')
    with m.get_db() as db:
        if not db.execute('SELECT 1 FROM audio_tracks WHERE id=?',(track_id,)).fetchone(): raise HTTPException(404,'track not found')
        if values:
            db.execute('UPDATE audio_tracks SET '+','.join(k+'=?' for k in values)+',updated_at=? WHERE id=?',[*values.values(),m.utc_now(),track_id])
        if attribution:
            db.execute('INSERT OR REPLACE INTO audio_sources VALUES(?,?,?,?,?,?)',
                       (track_id,attribution['source_url'],attribution['creator'],attribution['license'],attribution['license_url'],attribution['changes']))
        updated=db.execute('SELECT * FROM audio_tracks WHERE id=?',(track_id,)).fetchone()
    return m.track_dict(updated)


class Command(BaseModel):
    action: Literal['play','pause','resume','stop']
    track_id: str | None = None


@router.post('/admin/api/playback',dependencies=[ADMIN])
def command(payload: Command):
    now=m.utc_now(); ident=str(uuid.uuid4())
    with m.get_db() as db:
        if payload.action=='play':
            row=db.execute('SELECT * FROM audio_tracks WHERE id=? AND active=1',(payload.track_id,)).fetchone()
            if not row or row['mime_type']!='audio/wav' or not row['file_name']: raise HTTPException(422,'device playback requires a locally uploaded WAV')
        db.execute("UPDATE device_commands SET status='superseded',updated_at=? WHERE status IN ('queued','received')",(now,))
        db.execute('INSERT INTO device_commands VALUES(?,?,?,?,?,?,NULL)',(ident,payload.action,payload.track_id,'queued',now,now))
    return {'id':ident,'status':'queued','message':'Waiting for device acknowledgement'}


@router.get('/admin/api/playback',dependencies=[ADMIN])
def playback_status():
    with m.get_db() as db:
        rows=db.execute('SELECT * FROM device_commands ORDER BY created_at DESC LIMIT 20').fetchall()
    return {'items':[dict(r) for r in rows]}


@router.get('/v1/device/command',dependencies=[DEVICE])
def next_command():
    with m.get_db() as db:
        row=db.execute("SELECT * FROM device_commands WHERE status IN ('queued','received') ORDER BY created_at DESC LIMIT 1").fetchone()
        if not row: return {'command':None}
        track=db.execute('SELECT * FROM audio_tracks WHERE id=?',(row['track_id'],)).fetchone() if row['track_id'] else None
    return {'command':dict(row),'track':m.track_dict(track) if track else None}


class Ack(BaseModel):
    status: Literal['received','playing','paused','complete','stopped','error']
    error: str | None = Field(None,max_length=240)


@router.post('/v1/device/command/{command_id}',dependencies=[DEVICE])
def acknowledge(command_id: str,payload: Ack):
    with m.get_db() as db:
        changed=db.execute("UPDATE device_commands SET status=?,error=?,updated_at=? WHERE id=? AND status!='superseded'",(payload.status,payload.error,m.utc_now(),command_id)).rowcount
    return {'accepted':bool(changed)}


@router.get('/admin/api/audio/all',dependencies=[ADMIN])
def all_audio():
    with m.get_db() as db:
        return {'items':[m.track_dict(r) for r in db.execute('SELECT * FROM audio_tracks ORDER BY active DESC,category,title')]}


@router.get('/v1/personal',dependencies=[Depends(m.authorize_roles('external'))])
def external_profile():
    return context()


@router.get('/v1/personal/statistics',dependencies=[Depends(m.authorize_roles('external'))])
def external_statistics(start: str | None=None,end: str | None=None):
    return statistics(start,end)


@router.get('/v1/personal/export',dependencies=[Depends(m.authorize_roles('external'))])
def external_export(start: str | None=None,end: str | None=None,format: Literal['json','csv']='json'):
    return export(start,end,format)
