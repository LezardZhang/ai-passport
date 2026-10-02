import io
import json
import wave
from datetime import datetime, timezone

import pytest
from fastapi.testclient import TestClient
from backend.app import main
from backend.app.unified import create_app
from test_unified import configured


@pytest.fixture
def client(configured):
    with TestClient(create_app(configured),base_url='https://service.example') as c:
        assert c.post('/cloud-backup/console/login',json={'key':configured.admin_token}).status_code==200
        yield c


P='/cloud-backup/childcare'

def snap(rev,events,floor=1):
    return {'schema':'xigua-device-snapshot-v1','revision':rev,'floor':floor,'events':events,'firmware':'test'}

def event(seq,epoch=1790776800):
    return {'seq':seq,'type':'feeding','epoch':epoch,'amount_ml':120}


def test_single_admin_login_and_default_profile(client,configured):
    assert client.get('/cloud-backup/console').status_code==200
    assert client.get('/cloud-backup/admin/v1/users').status_code==200
    assert client.get(P+'/admin/api/personal').json()['child']['id']=='baby'
    assert client.get(P+'/admin/api/devices').json()['items'][0]['id']=='xigua-device'
    assert client.get('/cloud-backup/console/api/device-config').status_code==200
    bad=client.post(P+'/admin/api/records',json={},headers={'Origin':'https://evil.example'})
    assert bad.status_code==403
    # Client API keys are not elevated to admin permissions.
    client.cookies.clear()
    assert client.get('/cloud-backup/admin/v1/users',headers={'Authorization':'Bearer DEVICE_TOKEN-test'}).status_code==401
    assert client.get('/cloud-backup/console/api/device-config').status_code==401
    assert client.get(P+'/admin/api/personal',headers={'Authorization':'Bearer PUBLIC_READ_TOKEN-test'}).status_code==403
    assert client.post('/cloud-backup/console/login',json={'key':'password-test'}).status_code==401


def test_snapshot_retry_undo_and_rollover_keep_old_history(client):
    headers={'Authorization':'Bearer '+main.DEVICE_TOKEN}
    for rev,events,floor in [(1,[event(1),event(2)],1),(2,[event(2),event(3)],2)]:
        assert client.post(P+'/v1/device/snapshot',json=snap(rev,events,floor),headers=headers).status_code==200
    with main.get_db() as db:
        assert db.execute('SELECT COUNT(*) FROM events').fetchone()[0]==3
    # Undo event 3 while event 1 is already outside the device ring: retain 1.
    assert client.post(P+'/v1/device/snapshot',json=snap(3,[event(2)],2),headers=headers).status_code==200
    assert client.post(P+'/v1/device/snapshot',json=snap(2,[event(2),event(3)],2),headers=headers).json()['stale']
    with main.get_db() as db:
        assert [r[0] for r in db.execute('SELECT sync_seq FROM events ORDER BY sync_seq')]==[1,2]
    assert client.post(P+'/v1/device/snapshot',json=snap(4,[event(1)],1),headers=headers).status_code==409
    assert client.post(P+'/v1/device/snapshot',json=snap(4,[event(2),event(2)],2),headers=headers).status_code==422


def test_cross_midnight_sleep_and_complete_export(client):
    def epoch(text): return int(datetime.fromisoformat(text).timestamp())
    events=[event(1,epoch('2026-09-30T23:59:00+08:00')),
            {'seq':2,'type':'sleep','epoch':epoch('2026-10-01T02:00:00+08:00'),'duration_min':180,
             'start_epoch':epoch('2026-09-30T23:00:00+08:00'),'end_epoch':epoch('2026-10-01T02:00:00+08:00')},
            event(3,0)]
    headers={'Authorization':'Bearer '+main.DEVICE_TOKEN}
    assert client.post(P+'/v1/device/snapshot',json=snap(1,events),headers=headers).status_code==200
    data=client.get(P+'/admin/api/statistics?start=2026-09-30&end=2026-10-01').json()
    assert [r['sleep_minutes'] for r in data['series']]==[60,120]
    assert data['totals']['milk_ml']==120
    assert data['context']['totals']['untimed']==1
    exported=client.get(P+'/admin/api/export?start=2026-09-30&end=2026-10-01').json()
    assert len(exported['events'])==3
    assert client.get(P+'/admin/api/export?start=2026-09-30&end=2026-10-01&format=csv').content.startswith(b'\xef\xbb\xbf')
    assert client.get(P+'/admin/api/statistics?start=2026-02-30').status_code==400


def test_more_than_500_exported_and_csv_formula_escaped(client):
    child,device='baby','xigua-device'
    with main.get_db() as db:
        for n in range(520):
            db.execute('''INSERT INTO events(id,client_event_id,child_id,device_id,type,occurred_at,amount_ml,note,created_at)
                VALUES(?,?,?,?,?,?,?,?,?)''',(str(n),str(n),child,device,'feeding','2026-09-30T08:00:00+00:00',10,'=danger',main.utc_now()))
    assert len(client.get(P+'/admin/api/export?start=2026-09-30&end=2026-09-30').json()['events'])==520
    assert "'=danger" in client.get(P+'/admin/api/export?start=2026-09-30&end=2026-09-30&format=csv').text
    assert len(client.get(P+'/admin/api/records?start=2026-09-30&end=2026-09-30&offset=500').json()['items'])==20


def test_audio_upload_playback_ack_and_soft_disable(client,tmp_path,monkeypatch):
    monkeypatch.setattr(main,'MEDIA_DIR',tmp_path)
    stream=io.BytesIO()
    with wave.open(stream,'wb') as w:
        w.setnchannels(1);w.setsampwidth(2);w.setframerate(12000);w.writeframes(b'\0\1'*1200)
    meta=client.put(P+'/admin/api/audio/files',content=stream.getvalue()).json()
    track=client.post(P+'/v1/audio/tracks',json={**meta,'title':'Song','category':'song'}).json()
    command=client.post(P+'/admin/api/playback',json={'action':'play','track_id':track['id']}).json()
    headers={'Authorization':'Bearer '+main.DEVICE_TOKEN}
    assert client.get(P+'/v1/device/command',headers=headers).json()['command']['id']==command['id']
    assert client.post(P+'/v1/device/command/'+command['id'],headers=headers,json={'status':'playing'}).json()['accepted']
    assert client.get(P+'/admin/api/playback').json()['items'][0]['status']=='playing'
    assert client.patch(P+'/admin/api/audio/'+track['id'],json={'active':False}).status_code==200
    assert client.get(P+'/admin/api/audio/all').json()['items'][0]['active'] is False
    assert client.get(P+'/v1/audio/tracks',headers=headers).json()['items']==[]
    assert (tmp_path/meta['file_name']).is_file()
    client.cookies.clear()
    assert client.get(P+'/media/'+meta['file_name']).status_code==403
    assert client.get(P+'/media/'+meta['file_name'],headers=headers).status_code==200
    resumed=client.get(P+'/media/'+meta['file_name'],headers={**headers,'Range':'bytes=44-'})
    assert resumed.status_code==206
    assert resumed.headers['content-range']==f"bytes 44-{len(stream.getvalue())-1}/{len(stream.getvalue())}"
    assert resumed.content==stream.getvalue()[44:]
    assert client.get(P+'/media/'+meta['file_name'],headers={**headers,'Range':'bytes=999999-'}).status_code==416
    assert client.put(P+'/admin/api/audio/files',content=b'bad').status_code==403


def test_delete_restore_and_device_resync(client):
    h={'Authorization':'Bearer '+main.DEVICE_TOKEN}
    assert client.post(P+'/v1/device/snapshot',json=snap(1,[event(1),event(2)]),headers=h).status_code==200
    rows=client.get(P+'/admin/api/records?start=2026-09-30&end=2026-10-01').json()['items']
    ident=rows[0]['id']
    assert client.delete(P+'/admin/api/records/'+ident,headers={'Origin':'https://evil.example'}).status_code==403
    assert client.delete(P+'/admin/api/records/'+ident).json()['recoverable']
    assert client.post(P+'/v1/device/snapshot',json=snap(2,[event(1),event(2)]),headers=h).status_code==200
    assert client.get(P+'/admin/api/personal').json()['totals']['records']==1
    assert client.get(P+'/admin/api/statistics?start=2026-09-30&end=2026-10-01').json()['totals']['milk_ml']==120
    assert client.get(P+'/admin/api/trash').json()['total']==1
    assert client.post(P+'/admin/api/trash/'+ident+'/restore').status_code==200
    assert client.get(P+'/admin/api/personal').json()['totals']['records']==2
    cleared=client.post(P+'/admin/api/records/clear').json()
    assert cleared['deleted']==2
    assert cleared['device_command_id']
    command=client.get(P+'/v1/device/command',headers=h).json()['command']
    assert command['id']==cleared['device_command_id']
    assert command['action']=='clear_records'
    assert client.post(P+'/v1/device/command/'+command['id'],headers=h,
                       json={'status':'complete'}).json()['accepted']
    assert client.post(P+'/v1/device/snapshot',json=snap(3,[event(1),event(2)]),headers=h).status_code==200
    assert client.get(P+'/admin/api/personal').json()['totals']['records']==0
    assert client.get(P+'/admin/api/trash').json()['total']==2
    assert client.delete(P+'/admin/api/records/missing').status_code==404
    client.cookies.clear()
    assert client.delete(P+'/admin/api/records/'+ident,headers=h).status_code==403


def test_pause_resume_commands_and_device_ack(client):
    device={'Authorization':'Bearer '+main.DEVICE_TOKEN}
    pause=client.post(P+'/admin/api/playback',json={'action':'pause'}).json()
    assert client.get(P+'/v1/device/command',headers=device).json()['command']['action']=='pause'
    assert client.post(P+'/v1/device/command/'+pause['id'],headers=device,json={'status':'paused'}).json()['accepted']
    assert client.get(P+'/v1/device/command',headers=device).json()['command'] is None
    assert client.get(P+'/admin/api/playback').json()['items'][0]['status']=='paused'
    resume=client.post(P+'/admin/api/playback',json={'action':'resume'}).json()
    assert client.get(P+'/v1/device/command',headers=device).json()['command']['action']=='resume'
    assert client.post(P+'/v1/device/command/'+resume['id'],headers=device,json={'status':'playing'}).json()['accepted']
    assert client.post(P+'/v1/device/command/'+resume['id'],headers=device,json={'status':'complete'}).json()['accepted']
    # A rapid pause then stop cancels the queued pause, not the last completed play.
    pending=client.post(P+'/admin/api/playback',json={'action':'pause'}).json()
    stop=client.post(P+'/admin/api/playback',json={'action':'stop'}).json()
    assert client.get(P+'/v1/device/command',headers=device).json()['command']['id']==stop['id']
    assert not client.post(P+'/v1/device/command/'+pending['id'],headers=device,json={'status':'paused'}).json()['accepted']
    assert client.post(P+'/admin/api/playback',json={'action':'rewind'}).status_code==422
    assert client.post(P+'/admin/api/playback',headers={'Origin':'https://evil.example'},json={'action':'pause'}).status_code==403
    client.cookies.clear()
    assert client.post(P+'/admin/api/playback',headers=device,json={'action':'resume'}).status_code==403


def test_deleted_manual_record_retry_does_not_return(client):
    record={'id':'manual-test','type':'feeding','occurred_at':'2026-10-01T01:00:00Z','amount_ml':80}
    assert client.post(P+'/admin/api/records',json=record).status_code==200
    with main.get_db() as db:ident=db.execute('SELECT id FROM events').fetchone()[0]
    assert client.delete(P+'/admin/api/records/'+ident).status_code==200
    assert client.post(P+'/admin/api/records',json=record).json()['accepted']==0
    assert client.get(P+'/admin/api/personal').json()['totals']['records']==0


def test_audio_attribution_reaches_device_catalog(client,monkeypatch,tmp_path):
    monkeypatch.setattr(main,'MEDIA_DIR',tmp_path)
    out=io.BytesIO()
    with wave.open(out,'wb') as w:
        w.setnchannels(1);w.setsampwidth(2);w.setframerate(12000);w.writeframes(b'\0\0'*1200)
    meta=client.put(P+'/admin/api/audio/files',content=out.getvalue()).json()
    track=client.post(P+'/v1/audio/tracks',json={**meta,'title':'Song','category':'song'}).json()
    attribution={'source_url':'https://commons.wikimedia.org/wiki/File:Example.ogg','creator':'Creator','license':'CC BY-SA 3.0','license_url':'https://creativecommons.org/licenses/by-sa/3.0/','changes':'Resampled to PCM WAV'}
    assert client.patch(P+'/admin/api/audio/'+track['id'],json={'source_url':attribution['source_url']}).status_code==422
    assert client.patch(P+'/admin/api/audio/'+track['id'],json=attribution).json()['attribution']==attribution
    device=client.get(P+'/v1/audio/tracks?category=song',headers={'Authorization':'Bearer '+main.DEVICE_TOKEN}).json()['items']
    assert device[0]['attribution']==attribution
