import sqlite3
import pytest
from backend.app import main
from test_personal import client, P
from test_unified import configured


def legacy_audio_db(path):
    db = sqlite3.connect(path)
    db.execute("PRAGMA foreign_keys=ON")
    db.executescript("""
        CREATE TABLE audio_tracks(id TEXT PRIMARY KEY,title TEXT NOT NULL,
          category TEXT NOT NULL CHECK(category IN ('song','white_noise','story','other')),
          active INTEGER NOT NULL DEFAULT 1,updated_at TEXT);
        CREATE INDEX audio_tracks_category ON audio_tracks(category,active,title);
        CREATE TABLE playback_events(id TEXT PRIMARY KEY,track_id TEXT REFERENCES audio_tracks(id));
        CREATE TABLE audio_sources(track_id TEXT PRIMARY KEY,creator TEXT);
        INSERT INTO audio_tracks VALUES('old','原有儿歌','song',0,'original-time');
        INSERT INTO playback_events VALUES('history','old');
        INSERT INTO audio_sources VALUES('old','original performer');
    """)
    return db


def test_category_migration_preserves_history_and_is_repeatable(tmp_path):
    with legacy_audio_db(tmp_path / 'old.db') as db:
        main.migrate_audio_categories(db)
        main.migrate_audio_categories(db)
        assert db.execute('PRAGMA foreign_keys').fetchone()[0] == 1
        assert db.execute('PRAGMA foreign_key_check').fetchall() == []
        assert db.execute('SELECT * FROM audio_tracks').fetchone() == ('old','原有儿歌','song',0,'original-time')
        assert db.execute('SELECT * FROM playback_events').fetchone() == ('history','old')
        assert db.execute('SELECT * FROM audio_sources').fetchone() == ('old','original performer')
        db.execute("INSERT INTO audio_tracks VALUES('new','巴赫','classical',1,'new-time')")


def test_migration_rolls_back_if_existing_references_are_invalid(tmp_path):
    with legacy_audio_db(tmp_path / 'invalid.db') as db:
        db.execute('PRAGMA foreign_keys=OFF')
        db.execute("INSERT INTO playback_events VALUES('broken','missing')")
        db.commit()
        with pytest.raises(RuntimeError, match='break references'):
            main.migrate_audio_categories(db)
        assert db.execute('PRAGMA foreign_keys').fetchone()[0] == 1
        assert "'classical'" not in db.execute("SELECT sql FROM sqlite_master WHERE name='audio_tracks'").fetchone()[0]
        assert db.execute('SELECT COUNT(*) FROM audio_tracks').fetchone()[0] == 1
        assert not db.execute("SELECT 1 FROM sqlite_master WHERE name='audio_tracks_new'").fetchone()


def test_classical_catalog_pages_are_complete_and_device_compatible(client):
    for index in range(19):
        response = client.post(P+'/v1/audio/tracks',json={
            'id':f'classical-{index:02}','title':f'莫扎特 {index:02}','category':'classical',
            'file_name':f'piece-{index}.wav','mime_type':'audio/wav'})
        assert response.status_code == 200
    assert client.post(P+'/v1/audio/tracks',json={'id':'other-format','title':'MP3',
        'category':'classical','file_name':'original.mp3','mime_type':'audio/mpeg'}).status_code == 200
    assert client.patch(P+'/admin/api/audio/classical-18',json={'active':False}).status_code == 200
    headers={'Authorization':'Bearer '+main.DEVICE_TOKEN}
    ids=[];offset=0
    while True:
        data=client.get(P+f'/v1/audio/catalog?category=classical&offset={offset}',headers=headers).json()
        assert data['total']==18 and len(data['items'])<=8 and data['count']==len(data['items'])
        assert all(set(t)=={'id','title','category','play_url','mime_type'} for t in data['items'])
        ids.extend(t['id'] for t in data['items'])
        if data['next_offset'] is None: break
        offset=data['next_offset']
    assert len(ids)==len(set(ids))==18
    assert client.get(P+'/v1/audio/catalog?category=invalid',headers=headers).status_code==400
    assert client.get(P+'/v1/audio/catalog?category=story&offset=-1',headers=headers).status_code==422
    assert client.get(P+'/v1/audio/catalog?category=song&limit=17',headers=headers).status_code==422
    assert client.get(P+'/v1/audio/catalog?category=white_noise',headers=headers).json()['total']==0
