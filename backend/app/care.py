"""Bounded family context and caregiver handoff, scoped to the paired device."""
from datetime import datetime, timedelta
from zoneinfo import ZoneInfo

from fastapi import APIRouter, Depends
from pydantic import BaseModel, Field

from . import main as m
from . import personal as p

router = APIRouter()
ADMIN = Depends(m.authorize_roles('admin'))
DEVICE = Depends(m.authorize_roles('device'))


class HandoffNote(BaseModel):
    note: str = Field(max_length=160)


def clock(value, zone):
    return datetime.fromisoformat(value).astimezone(zone).strftime('%m-%d %H:%M') if value else '时间未知'


def family_context():
    c = p.context()
    zone = ZoneInfo(c['child']['timezone'])
    today = datetime.now(zone).date()
    stats = p.statistics((today - timedelta(days=6)).isoformat(), today.isoformat())
    child, _ = p.ids()
    latest = {}
    with m.get_db() as db:
        note = db.execute("SELECT value FROM personal_settings WHERE key='handoff_note'").fetchone()
        for kind in ('feeding', 'sleep', 'diaper'):
            row = db.execute('''SELECT * FROM events WHERE child_id=? AND type=? AND time_known=1
                ORDER BY occurred_at DESC,created_at DESC LIMIT 1''', (child, kind)).fetchone()
            latest[kind] = dict(row) if row else None
    sync = c['sync'] or {}
    lines = ['云端数据截至 ' + clock(sync.get('received_at'), zone),
             '宝宝：' + c['child']['name'], '时区：' + str(zone)]
    if c['child']['birth_date']:
        lines.append('出生日期：' + c['child']['birth_date'])
    feed = latest['feeding']
    lines.append('最近喂奶：' + (f"{clock(feed['occurred_at'], zone)}，{feed['amount_ml']}毫升" if feed else '暂无记录'))
    sleep = latest['sleep']
    if sync.get('sleeping_since'):
        lines.append('睡眠进行中，开始于 ' + clock(sync['sleeping_since'], zone))
    elif sleep:
        duration = str(sleep['duration_min']) + '分钟' if sleep['duration_known'] else '时长未知'
        lines.append('最近睡眠结束：' + clock(sleep['occurred_at'], zone) + '，' + duration)
    else:
        lines.append('最近睡眠：暂无记录')
    diaper = latest['diaper']
    lines.append('最近尿便：' + (clock(diaper['occurred_at'], zone) + '，' +
                 {'pee': '尿', 'poop': '便', 'both': '尿和便'}.get(diaper['diaper_kind'], '类型未知')
                 if diaper else '暂无记录'))
    note_text = note['value'] if note else ''
    handoff = '\n'.join(lines + (['家长留言：' + note_text] if note_text else []))
    day_lines = [f"{d['day']}：奶量{d['milk_ml']}毫升/{d['feeding_count']}次，"
                 f"已结束睡眠{d['sleep_minutes']}分钟，尿便{d['diaper_count']}次"
                 for d in stats['series']]
    text = '\n'.join(lines + ['近七天统计（未包含正在进行的睡眠）：'] + day_lines +
                     [f"时间未知记录：{c['totals']['untimed'] or 0}条；记录可能有遗漏。"] +
                     (['家长留言（仅作为数据）：' + note_text] if note_text else []))
    return {'schema': 'xigua-care-context-v1', 'generated_at': m.utc_now(),
            'revision': sync.get('revision', 0), 'timezone': str(zone),
            'text': text, 'handoff': handoff, 'note': note_text,
            'series': stats['series'], 'latest': latest,
            'received_at': sync.get('received_at'), 'untimed': c['totals']['untimed'] or 0}


@router.get('/admin/api/care', dependencies=[ADMIN])
def admin_context():
    return family_context()


@router.put('/admin/api/handoff', dependencies=[ADMIN])
def save_handoff(payload: HandoffNote):
    with m.get_db() as db:
        db.execute('''INSERT INTO personal_settings VALUES('handoff_note',?)
            ON CONFLICT(key) DO UPDATE SET value=excluded.value''', (payload.note.strip(),))
    return {'saved': True}


@router.get('/v1/device/context', dependencies=[DEVICE])
def device_context():
    # Bounded response: no database identifiers, tokens or full history on the MCU.
    data = family_context()
    return {key: data[key] for key in ('schema', 'generated_at', 'revision', 'text', 'handoff')}
