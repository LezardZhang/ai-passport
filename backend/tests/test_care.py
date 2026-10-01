from datetime import datetime, timezone

import pytest
from fastapi.testclient import TestClient

from backend.app import care, main


class FixedDatetime(datetime):
    @classmethod
    def now(cls, tz=None):
        return datetime(2026, 10, 1, 12, tzinfo=timezone.utc).astimezone(tz)


@pytest.fixture
def client(tmp_path, monkeypatch):
    monkeypatch.setattr(main, 'DB_PATH', tmp_path / 'care.db')
    monkeypatch.setattr(main, 'API_TOKEN', '')
    for role in ('DEVICE', 'ADMIN', 'HERMES', 'PUBLIC_READ'):
        monkeypatch.setattr(main, role + '_TOKEN', role.lower() + '-test')
    monkeypatch.setattr(care, 'datetime', FixedDatetime)
    with TestClient(main.app) as c:
        yield c


D = {'Authorization': 'Bearer device-test'}
A = {'Authorization': 'Bearer admin-test'}


def snapshot(client, rev, amount=120, ingredient='formula'):
    def epoch(text):
        return int(datetime.fromisoformat(text).timestamp())
    payload = {'schema': 'xigua-device-snapshot-v1', 'revision': rev, 'floor': 1, 'events': [
        {'seq': 1, 'type': 'feeding', 'epoch': epoch('2026-10-01T10:00:00+08:00'),
         'amount_ml': amount, 'ingredient': ingredient},
        {'seq': 2, 'type': 'sleep', 'epoch': epoch('2026-10-01T06:00:00+08:00'),
         'start_epoch': epoch('2026-09-30T21:00:00+08:00'),
         'end_epoch': epoch('2026-10-01T06:00:00+08:00'), 'duration_min': 540},
        {'seq': 3, 'type': 'diaper', 'epoch': 0, 'diaper_kind': 'pee'},
    ]}
    assert client.post('/v1/device/snapshot', headers=D, json=payload).status_code == 200


def test_context_uses_calculated_days_and_reports_freshness(client):
    snapshot(client, 1)
    context = client.get('/admin/api/care', headers=A).json()
    assert len(context['series']) == 7
    assert context['series'][-1]['milk_ml'] == 120
    assert context['series'][-1]['sleep_minutes'] == 360
    assert context['series'][-2]['sleep_minutes'] == 180
    assert context['untimed'] == 1
    assert '时间未知记录：1条' in context['text']
    assert '10-01 10:00，120毫升' in context['handoff']
    bounded = client.get('/v1/device/context', headers=D)
    assert bounded.status_code == 200
    assert bounded.json()['revision'] == 1
    assert 'latest' not in bounded.json()
    assert len(bounded.content) < 8192
    for token in (main.ADMIN_TOKEN, main.DEVICE_TOKEN, main.HERMES_TOKEN):
        assert token not in bounded.text


def test_edit_is_synced_without_duplicate_or_stale_overwrite(client):
    snapshot(client, 1)
    snapshot(client, 2, amount=150, ingredient='breast_milk')
    snapshot(client, 1, amount=120)
    result = client.get('/admin/api/care', headers=A).json()
    assert result['series'][-1]['milk_ml'] == 150
    assert result['latest']['feeding']['ingredient'] == 'breast_milk'
    with main.get_db() as db:
        assert db.execute('SELECT COUNT(*) FROM events').fetchone()[0] == 3


def test_handoff_note_persists_is_bounded_and_requires_admin(client):
    text = '出门用品在门口 <script>alert(1)</script>'
    assert client.put('/admin/api/handoff', headers=D, json={'note': text}).status_code == 403
    assert client.put('/admin/api/handoff', headers=A, json={'note': text}).json()['saved']
    assert text in client.get('/v1/device/context', headers=D).json()['handoff']
    assert client.get('/admin/api/care', headers=A).json()['note'] == text
    assert client.put('/admin/api/handoff', headers=A, json={'note': '字' * 161}).status_code == 422
    assert client.get('/v1/device/context', headers={'Authorization': 'Bearer public_read-test'}).status_code == 403
    assert client.get('/v1/device/context').status_code in (401, 403)
    assert client.put('/admin/api/handoff', headers=A, json={'note': ''}).status_code == 200
    assert '家长留言' not in client.get('/v1/device/context', headers=D).json()['handoff']


def test_empty_context_does_not_fabricate_events(client):
    result = client.get('/v1/device/context', headers=D).json()
    assert result['revision'] == 0
    assert '暂无记录' in result['handoff']
    assert '云端数据截至 时间未知' in result['handoff']


def test_maximum_profile_and_note_fit_device_cache(client):
    assert client.put('/admin/api/personal', headers=A,
                      json={'name': '名' * 80, 'timezone': 'Asia/Shanghai'}).status_code == 200
    assert client.put('/admin/api/handoff', headers=A, json={'note': '字' * 160}).status_code == 200
    data = client.get('/v1/device/context', headers=D)
    assert len(data.json()['text'].encode()) < 3072
    assert len(data.json()['handoff'].encode()) < 1536
    assert len(data.content) < 8192
