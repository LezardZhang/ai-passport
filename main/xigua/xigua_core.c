#include "xigua_core.h"
#include <limits.h>
#include <stdlib.h>
#include <string.h>

static bool clock_valid(const xigua_clock_t *c)
{
    return c && c->boot_id && c->quality <= XIGUA_TIME_RECONSTRUCTED &&
           c->quality >= XIGUA_TIME_UNKNOWN &&
           (c->quality == XIGUA_TIME_UNKNOWN || c->unix_ms >= 0);
}
static bool dated(const xigua_clock_t *c) { return c->quality != XIGUA_TIME_UNKNOWN; }
static bool duration(const xigua_clock_t *a, const xigua_clock_t *b,
                     uint64_t *ms, xigua_duration_quality_t *quality)
{
    if (a->boot_id == b->boot_id && b->monotonic_ms >= a->monotonic_ms) {
        *ms = b->monotonic_ms - a->monotonic_ms;
        *quality = XIGUA_DURATION_MONOTONIC;
        return true;
    }
    if (a->boot_id != b->boot_id && dated(a) && dated(b) && b->unix_ms >= a->unix_ms) {
        *ms = (uint64_t)b->unix_ms - (uint64_t)a->unix_ms;
        *quality = XIGUA_DURATION_UTC;
        return true;
    }
    *ms = 0; *quality = XIGUA_DURATION_UNKNOWN;
    return false;
}
void xigua_init(xigua_state_t *s, uint32_t boot_id)
{
    if (!s) return;
    memset(s, 0, sizeof(*s)); s->boot_id = boot_id; s->next_event_id = 1;
}
bool xigua_reboot(xigua_state_t *s, uint32_t boot_id)
{
    if (!s || !boot_id || boot_id <= s->boot_id || s->revision == UINT32_MAX) return false;
    s->boot_id = boot_id; ++s->revision; s->undo.valid = false;
    return true;
}
const xigua_event_t *xigua_event(const xigua_state_t *s, uint32_t id)
{
    if (s) for (unsigned i = 0; i < s->event_count; ++i) if (s->events[i].id == id) return &s->events[i];
    return NULL;
}
const xigua_event_t *xigua_latest(const xigua_state_t *s, xigua_event_type_t type)
{
    if (s) for (unsigned i = s->event_count; i; --i) if (s->events[i-1].type == type) return &s->events[i-1];
    return NULL;
}
static const xigua_event_t *active(const xigua_state_t *s, xigua_event_type_t type)
{
    const xigua_event_t *e = xigua_latest(s, type);
    return e && e->active ? e : NULL;
}
const xigua_event_t *xigua_active_sleep(const xigua_state_t *s) { return active(s, XIGUA_SLEEP); }
const xigua_event_t *xigua_active_tummy(const xigua_state_t *s) { return active(s, XIGUA_TUMMY); }
bool xigua_elapsed(const xigua_event_t *e, const xigua_clock_t *now, uint64_t *ms)
{
    if (!e || !ms) return false;
    if (!e->active) {
        *ms = e->duration_ms;
        return e->duration_quality != XIGUA_DURATION_UNKNOWN;
    }
    if (!clock_valid(now)) return false;
    xigua_duration_quality_t q;
    return duration(&e->start, now, ms, &q);
}
bool xigua_timer_remaining(const xigua_state_t *s, const xigua_clock_t *now, uint32_t *ms)
{
    if (!s || !ms || !clock_valid(now) || !s->timer.active ||
        s->timer.start.boot_id != now->boot_id || now->monotonic_ms < s->timer.start.monotonic_ms) return false;
    uint64_t elapsed = now->monotonic_ms - s->timer.start.monotonic_ms;
    *ms = elapsed >= s->timer.duration_ms ? 0 : s->timer.duration_ms - (uint32_t)elapsed;
    return true;
}
static bool same_command(const xigua_command_t *a, const xigua_command_t *b)
{
    return a->command_id == b->command_id && a->action == b->action && a->value == b->value &&
           a->target_event_id == b->target_event_id && a->expected_revision == b->expected_revision;
}
xigua_result_t xigua_apply(xigua_state_t *s, const xigua_command_t *c, const xigua_clock_t *now)
{
    xigua_result_t r = {.status = XIGUA_INVALID};
    if (!s || !c || !c->command_id) return r;
    for (unsigned i = 0; i < s->replay_count; ++i) {
        if (s->replay[i].command.command_id != c->command_id) continue;
        if (!same_command(c, &s->replay[i].command)) { r.status = XIGUA_ID_REUSED; return r; }
        r = s->replay[i].result; r.replayed = true; return r;
    }
    if (!clock_valid(now) || now->boot_id != s->boot_id || s->revision == UINT32_MAX ||
        c->action < XIGUA_FEED_ADD || c->action > XIGUA_UNDO) return r;
    bool add = c->action == XIGUA_FEED_ADD || c->action == XIGUA_SLEEP_START ||
        c->action == XIGUA_DIAPER_ADD || c->action == XIGUA_BATH_ADD || c->action == XIGUA_TUMMY_START;
    bool stop = c->action == XIGUA_SLEEP_STOP || c->action == XIGUA_TUMMY_STOP;
    const xigua_event_t *target = NULL;
    if ((add || c->action == XIGUA_TIMER_START) && (c->target_event_id || c->expected_revision)) return r;
    if ((c->action == XIGUA_FEED_ADD || c->action == XIGUA_FEED_EDIT) && (c->value < 10 || c->value > 400)) return r;
    if (c->action == XIGUA_DIAPER_ADD && (c->value < 1 || c->value > 3)) return r;
    if (c->action == XIGUA_TIMER_START && (c->value < 1000 || c->value > 86400000)) return r;
    if (c->action != XIGUA_FEED_ADD && c->action != XIGUA_FEED_EDIT && c->action != XIGUA_DIAPER_ADD &&
        c->action != XIGUA_TIMER_START && c->value) return r;
    if (add && (s->event_count >= XIGUA_EVENT_CAPACITY || s->next_event_id == UINT32_MAX)) { r.status = XIGUA_FULL; return r; }
    if ((c->action == XIGUA_SLEEP_START && xigua_active_sleep(s)) ||
        (c->action == XIGUA_TUMMY_START && xigua_active_tummy(s)) ||
        (c->action == XIGUA_TIMER_START && s->timer.active)) { r.status = XIGUA_ALREADY_ACTIVE; return r; }
    if (c->action == XIGUA_FEED_EDIT || stop) {
        target = c->action == XIGUA_FEED_EDIT ? xigua_latest(s, XIGUA_FEED) :
            active(s, c->action == XIGUA_SLEEP_STOP ? XIGUA_SLEEP : XIGUA_TUMMY);
        if (!target) { r.status = stop ? XIGUA_NOT_ACTIVE : XIGUA_NOT_FOUND; return r; }
        if (target->id != c->target_event_id || target->revision != c->expected_revision) { r.status = XIGUA_CONFLICT; return r; }
        if (stop && target->start.boot_id == now->boot_id && now->monotonic_ms < target->start.monotonic_ms) return r;
    }
    if (c->action == XIGUA_TIMER_CANCEL) {
        if (!s->timer.active) { r.status = XIGUA_NOT_ACTIVE; return r; }
        if (c->target_event_id || c->expected_revision != s->revision) { r.status = XIGUA_CONFLICT; return r; }
    }
    if (c->action == XIGUA_UNDO) {
        if (!s->undo.valid) { r.status = XIGUA_NOT_FOUND; return r; }
        if (c->target_event_id != s->undo.target_event_id || c->expected_revision != s->revision) { r.status = XIGUA_CONFLICT; return r; }
    }
    /* Validation ends here: no failing path below may leave a partial mutation. */
    ++s->revision;
    r.status = XIGUA_OK; r.revision = s->revision;
    if (c->action == XIGUA_UNDO) {
        r.event_id = s->undo.target_event_id;
        if (s->undo.target_event_id) {
            if (s->undo.had_event) {
                s->events[s->undo.event_index] = s->undo.previous_event;
                s->events[s->undo.event_index].revision = s->revision;
            } else {
                --s->event_count;
                memset(&s->events[s->event_count], 0, sizeof(s->events[0]));
            }
        }
        s->timer = s->undo.previous_timer;
        memset(&s->undo, 0, sizeof(s->undo));
    } else {
        memset(&s->undo, 0, sizeof(s->undo));
        s->undo.valid = true; s->undo.previous_timer = s->timer;
        if (add) {
            unsigned index = s->event_count++;
            xigua_event_t *e = &s->events[index];
            memset(e, 0, sizeof(*e));
            e->id = s->next_event_id++; e->revision = s->revision; e->start = *now;
            e->value = c->value;
            e->type = c->action == XIGUA_FEED_ADD ? XIGUA_FEED : c->action == XIGUA_SLEEP_START ? XIGUA_SLEEP :
                c->action == XIGUA_DIAPER_ADD ? XIGUA_DIAPER : c->action == XIGUA_BATH_ADD ? XIGUA_BATH : XIGUA_TUMMY;
            e->active = e->type == XIGUA_SLEEP || e->type == XIGUA_TUMMY;
            if (!e->active) { e->end = *now; e->duration_quality = XIGUA_DURATION_MONOTONIC; }
            s->undo.event_index = index; s->undo.target_event_id = e->id; r.event_id = e->id;
        } else if (target) {
            unsigned index = (unsigned)(target - s->events);
            s->undo.had_event = true; s->undo.event_index = index;
            s->undo.target_event_id = target->id; s->undo.previous_event = *target;
            xigua_event_t *e = &s->events[index];
            e->revision = s->revision; r.event_id = e->id;
            if (stop) { e->active = false; e->end = *now; (void)duration(&e->start, now, &e->duration_ms, &e->duration_quality); }
            else e->value = c->value;
        } else if (c->action == XIGUA_TIMER_START) {
            s->timer.active = true; s->timer.start = *now; s->timer.duration_ms = c->value;
        } else memset(&s->timer, 0, sizeof(s->timer));
    }
    s->replay[s->replay_next] = (xigua_replay_t){*c, r};
    s->replay_next = (uint8_t)((s->replay_next + 1) % XIGUA_REPLAY_CAPACITY);
    if (s->replay_count < XIGUA_REPLAY_CAPACITY) ++s->replay_count;
    return r;
}

void xigua_stats(const xigua_state_t *s, const xigua_clock_t *now, bool all_time,
                 int64_t begin, int64_t end, xigua_stats_t *out)
{
    if (!out) return;
    memset(out, 0, sizeof(*out));
    if (!s || (!all_time && (begin < 0 || end <= begin))) return;
    uint64_t tummy_ms = 0;
    for (unsigned i = 0; i < s->event_count; ++i) {
        const xigua_event_t *e = &s->events[i];
        bool timed = e->type == XIGUA_SLEEP || e->type == XIGUA_TUMMY;
        uint64_t ms = 0;
        const xigua_clock_t *finish_clock = e->active ? now : &e->end;
        bool pending = !dated(&e->start) ||
            (timed && (!clock_valid(finish_clock) || !dated(finish_clock)));
        if (timed) {
            bool known_duration = xigua_elapsed(e, now, &ms);
            if (!known_duration) pending = true;
            if (pending) ++out->pending_count;
            if (!known_duration || (!all_time && pending)) continue;
            if (!all_time) {
                /* Allocate monotonic duration on the anchored start's absolute timeline. */
                uint64_t start = (uint64_t)e->start.unix_ms;
                if (ms > (uint64_t)INT64_MAX - start) { ++out->pending_count; continue; }
                uint64_t finish = start + ms;
                uint64_t lo = start > (uint64_t)begin ? start : (uint64_t)begin;
                uint64_t hi = finish < (uint64_t)end ? finish : (uint64_t)end;
                ms = hi > lo ? hi - lo : 0;
            }
            uint64_t *total = e->type == XIGUA_SLEEP ? &out->sleep_ms : &tummy_ms;
            *total = ms > UINT64_MAX - *total ? UINT64_MAX : *total + ms;
        } else {
            if (pending) ++out->pending_count;
            if (all_time || (!pending && e->start.unix_ms >= begin && e->start.unix_ms < end)) {
                if (e->type == XIGUA_FEED) { ++out->feed_count; out->feed_ml += e->value; }
                if (e->type == XIGUA_DIAPER) ++out->diaper_count;
                if (e->type == XIGUA_BATH) ++out->bath_count;
            }
        }
    }
    uint64_t minutes = tummy_ms / 60000u;
    out->tummy_minutes = minutes > UINT32_MAX ? UINT32_MAX : (uint32_t)minutes;
}
static unsigned repair_clock(xigua_clock_t *c, const xigua_clock_t *a)
{
    if (c->quality != XIGUA_TIME_UNKNOWN || c->boot_id != a->boot_id) return 0;
    if (c->monotonic_ms <= a->monotonic_ms) {
        uint64_t d = a->monotonic_ms - c->monotonic_ms;
        if (d > (uint64_t)a->unix_ms) return 0;
        c->unix_ms = a->unix_ms - (int64_t)d;
    } else {
        uint64_t d = c->monotonic_ms - a->monotonic_ms;
        if (d > (uint64_t)(INT64_MAX - a->unix_ms)) return 0;
        c->unix_ms = a->unix_ms + (int64_t)d;
    }
    c->quality = XIGUA_TIME_RECONSTRUCTED; return 1;
}
unsigned xigua_reconstruct(xigua_state_t *s, const xigua_clock_t *a)
{
    if (!s || !clock_valid(a) || a->quality != XIGUA_TIME_TRUSTED || a->boot_id != s->boot_id || s->revision == UINT32_MAX) return 0;
    unsigned n = 0;
    for (unsigned i = 0; i < s->event_count; ++i) {
        n += repair_clock(&s->events[i].start, a);
        if (!s->events[i].active) {
            n += repair_clock(&s->events[i].end, a);
            (void)duration(&s->events[i].start, &s->events[i].end,
                           &s->events[i].duration_ms, &s->events[i].duration_quality);
        }
    }
    if (s->timer.active) n += repair_clock(&s->timer.start, a);
    if (s->undo.valid) {
        if (s->undo.had_event) {
            n += repair_clock(&s->undo.previous_event.start, a);
            if (!s->undo.previous_event.active) {
                n += repair_clock(&s->undo.previous_event.end, a);
                (void)duration(&s->undo.previous_event.start, &s->undo.previous_event.end,
                    &s->undo.previous_event.duration_ms, &s->undo.previous_event.duration_quality);
            }
        }
        if (s->undo.previous_timer.active) n += repair_clock(&s->undo.previous_timer.start, a);
    }
    if (n) ++s->revision;
    return n;
}

/* The wire format uses only explicit-width integers; padding and enum ABI never enter storage. */
typedef struct { uint8_t *out; const uint8_t *in; size_t size, pos; bool read, ok; } wire_t;
static uint64_t word(wire_t *w, uint64_t value, unsigned bytes)
{
    if (!w->ok || w->pos > w->size || bytes > w->size - w->pos) { w->ok = false; return 0; }
    uint64_t result = 0;
    for (unsigned i = 0; i < bytes; ++i) {
        if (w->read) result |= (uint64_t)w->in[w->pos++] << (8*i);
        else w->out[w->pos++] = (uint8_t)(value >> (8*i));
    }
    return w->read ? result : value;
}
#define FIELD(w, member, bytes) do { uint64_t v_ = word((w), (uint64_t)(member), (bytes)); if ((w)->read) (member) = v_; } while (0)
static void wire_clock(wire_t *w, xigua_clock_t *c)
{
    FIELD(w, c->boot_id, 4); FIELD(w, c->monotonic_ms, 8); FIELD(w, c->unix_ms, 8); FIELD(w, c->quality, 1);
}
static void wire_bool(wire_t *w, bool *value)
{
    uint64_t v = word(w, *value, 1);
    if (v > 1) w->ok = false;
    if (w->read) *value = v != 0;
}
static void wire_event(wire_t *w, xigua_event_t *e)
{
    FIELD(w, e->id, 4); FIELD(w, e->revision, 4); FIELD(w, e->type, 1); FIELD(w, e->value, 4);
    wire_clock(w, &e->start); wire_clock(w, &e->end);
    FIELD(w, e->duration_ms, 8); FIELD(w, e->duration_quality, 1); wire_bool(w, &e->active);
}
static void wire_timer(wire_t *w, xigua_timer_t *t)
{
    wire_bool(w, &t->active); wire_clock(w, &t->start); FIELD(w, t->duration_ms, 4);
}
static void wire_state(wire_t *w, xigua_state_t *s)
{
    FIELD(w, s->boot_id, 4); FIELD(w, s->revision, 4); FIELD(w, s->next_event_id, 4); FIELD(w, s->event_count, 2);
    if (s->event_count > XIGUA_EVENT_CAPACITY) { w->ok = false; return; }
    for (unsigned i = 0; i < s->event_count; ++i) wire_event(w, &s->events[i]);
    wire_timer(w, &s->timer); wire_bool(w, &s->undo.valid); wire_bool(w, &s->undo.had_event);
    FIELD(w, s->undo.event_index, 4); FIELD(w, s->undo.target_event_id, 4);
    wire_event(w, &s->undo.previous_event); wire_timer(w, &s->undo.previous_timer);
    FIELD(w, s->replay_count, 1); FIELD(w, s->replay_next, 1);
    if (s->replay_count > XIGUA_REPLAY_CAPACITY) { w->ok = false; return; }
    for (unsigned i = 0; i < s->replay_count; ++i) {
        xigua_replay_t *r = &s->replay[i];
        FIELD(w, r->command.command_id, 8); FIELD(w, r->command.action, 1);
        FIELD(w, r->command.target_event_id, 4); FIELD(w, r->command.expected_revision, 4); FIELD(w, r->command.value, 4);
        FIELD(w, r->result.status, 1); FIELD(w, r->result.event_id, 4); FIELD(w, r->result.revision, 4);
        wire_bool(w, &r->result.replayed);
    }
}
static uint32_t crc32(const uint8_t *data, size_t size)
{
    uint32_t crc = UINT32_MAX;
    for (size_t i = 0; i < size; ++i) {
        crc ^= data[i];
        for (unsigned j = 0; j < 8; ++j) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    return ~crc;
}
static bool event_valid(const xigua_event_t *e, const xigua_state_t *s)
{
    if (!e->id || e->id >= s->next_event_id || !e->revision || e->revision > s->revision ||
        e->type < XIGUA_FEED || e->type > XIGUA_TUMMY || !clock_valid(&e->start) ||
        e->start.boot_id > s->boot_id || e->duration_quality < XIGUA_DURATION_UNKNOWN ||
        e->duration_quality > XIGUA_DURATION_UTC) return false;
    if (e->type == XIGUA_FEED ? (e->value < 10 || e->value > 400) :
        e->type == XIGUA_DIAPER ? (e->value < 1 || e->value > 3) : e->value != 0) return false;
    if (e->active) return (e->type == XIGUA_SLEEP || e->type == XIGUA_TUMMY) &&
        e->duration_quality == XIGUA_DURATION_UNKNOWN && !e->duration_ms;
    if (!clock_valid(&e->end) || e->end.boot_id > s->boot_id ||
        e->end.boot_id < e->start.boot_id ||
        (e->end.boot_id == e->start.boot_id && e->end.monotonic_ms < e->start.monotonic_ms)) return false;
    if (e->type != XIGUA_SLEEP && e->type != XIGUA_TUMMY &&
        (e->end.boot_id != e->start.boot_id || e->end.monotonic_ms != e->start.monotonic_ms ||
         e->end.unix_ms != e->start.unix_ms || e->end.quality != e->start.quality)) return false;
    uint64_t ms; xigua_duration_quality_t q;
    (void)duration(&e->start, &e->end, &ms, &q);
    return ms == e->duration_ms && q == e->duration_quality;
}
static bool timer_valid(const xigua_timer_t *t, const xigua_state_t *s)
{
    return !t->active || (clock_valid(&t->start) && t->start.boot_id <= s->boot_id &&
        t->duration_ms >= 1000 && t->duration_ms <= 86400000);
}
static bool state_valid(const xigua_state_t *s)
{
    if (!s->boot_id || !s->next_event_id || s->event_count > XIGUA_EVENT_CAPACITY ||
        s->replay_count > XIGUA_REPLAY_CAPACITY || s->replay_next >= XIGUA_REPLAY_CAPACITY ||
        (s->replay_count < XIGUA_REPLAY_CAPACITY && s->replay_next != s->replay_count) || !timer_valid(&s->timer, s)) return false;
    unsigned sleep = 0, tummy = 0;
    for (unsigned i = 0; i < s->event_count; ++i) {
        if (!event_valid(&s->events[i], s) || (i && s->events[i-1].id >= s->events[i].id)) return false;
        if (s->events[i].active && xigua_latest(s, s->events[i].type) != &s->events[i]) return false;
        if (s->events[i].active && s->events[i].type == XIGUA_SLEEP) ++sleep;
        if (s->events[i].active && s->events[i].type == XIGUA_TUMMY) ++tummy;
    }
    if (sleep > 1 || tummy > 1) return false;
    if (s->undo.valid) {
        if (!timer_valid(&s->undo.previous_timer, s)) return false;
        if (s->undo.target_event_id) {
            if (s->undo.event_index >= s->event_count || s->events[s->undo.event_index].id != s->undo.target_event_id) return false;
            if (s->undo.had_event) {
                if (!event_valid(&s->undo.previous_event, s) || s->undo.previous_event.id != s->undo.target_event_id ||
                    s->undo.previous_event.type != s->events[s->undo.event_index].type) return false;
            } else if (s->undo.event_index != (unsigned)s->event_count - 1) return false;
        } else if (s->undo.had_event) return false;
    }
    for (unsigned i = 0; i < s->replay_count; ++i) {
        const xigua_replay_t *r = &s->replay[i];
        if (!r->command.command_id || r->command.action < XIGUA_FEED_ADD || r->command.action > XIGUA_UNDO ||
            r->result.status != XIGUA_OK || r->result.replayed || !r->result.revision || r->result.revision > s->revision ||
            r->result.event_id >= s->next_event_id) return false;
        for (unsigned j = 0; j < i; ++j) if (s->replay[j].command.command_id == r->command.command_id) return false;
    }
    return true;
}
size_t xigua_snapshot_encode(const xigua_state_t *s, uint8_t *out, size_t capacity)
{
    if (!s || !out || capacity < 16 || !state_valid(s)) return 0;
    wire_t w = {.out = out, .size = capacity < XIGUA_SNAPSHOT_MAX ? capacity : XIGUA_SNAPSHOT_MAX, .pos = 16, .ok = true};
    /* The writer never assigns through its state pointer. */
    wire_state(&w, (xigua_state_t *)s);
    if (!w.ok) return 0;
    size_t length = w.pos;
    w.pos = 0;
    (void)word(&w, 0x31534758u, 4); (void)word(&w, XIGUA_SCHEMA_VERSION, 4);
    (void)word(&w, length, 4); (void)word(&w, crc32(out + 16, length - 16), 4);
    return length;
}
bool xigua_snapshot_decode(xigua_state_t *s, const uint8_t *in, size_t length)
{
    if (!s || !in || length < 16 || length > XIGUA_SNAPSHOT_MAX) return false;
    wire_t w = {.in = in, .size = length, .read = true, .ok = true};
    if (word(&w, 0, 4) != 0x31534758u || word(&w, 0, 4) != XIGUA_SCHEMA_VERSION ||
        word(&w, 0, 4) != length || word(&w, 0, 4) != crc32(in + 16, length - 16)) return false;
    xigua_state_t *candidate = calloc(1, sizeof(*candidate));
    if (!candidate) return false;
    wire_state(&w, candidate);
    bool ok = w.ok && w.pos == length && state_valid(candidate);
    if (ok) *s = *candidate;
    free(candidate); return ok;
}
