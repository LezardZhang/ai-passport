#include "xigua_core.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static xigua_state_t s, saved, decoded;
static uint8_t wire[XIGUA_SNAPSHOT_MAX], wire2[XIGUA_SNAPSHOT_MAX];
static uint64_t sequence;
static xigua_clock_t now;
static void reset(void)
{
    xigua_init(&s, 1); sequence = 0;
    now = (xigua_clock_t){1, 1000, 0, XIGUA_TIME_UNKNOWN};
}
static xigua_command_t command(xigua_action_t action, unsigned value)
{
    xigua_command_t c = {.command_id = ++sequence, .action = action, .value = value};
    const xigua_event_t *e = NULL;
    if (action == XIGUA_FEED_EDIT) e = xigua_latest(&s, XIGUA_FEED);
    if (action == XIGUA_SLEEP_STOP) e = xigua_active_sleep(&s);
    if (action == XIGUA_TUMMY_STOP) e = xigua_active_tummy(&s);
    if (e) { c.target_event_id = e->id; c.expected_revision = e->revision; }
    if (action == XIGUA_UNDO) c.target_event_id = s.undo.target_event_id;
    if (action == XIGUA_UNDO || action == XIGUA_TIMER_CANCEL) c.expected_revision = s.revision;
    return c;
}
static xigua_result_t apply(xigua_action_t a, unsigned value)
{
    xigua_command_t c = command(a, value);
    return xigua_apply(&s, &c, &now);
}
static void rejected(xigua_command_t c, xigua_status_t expected)
{
    saved = s;
    assert(xigua_apply(&s, &c, &now).status == expected);
    assert(!memcmp(&s, &saved, sizeof(s)));
}
static void roundtrip(void)
{
    size_t n = xigua_snapshot_encode(&s, wire, sizeof(wire));
    assert(n > 16 && n < XIGUA_SNAPSHOT_MAX);
    assert(xigua_snapshot_decode(&decoded, wire, n));
    size_t n2 = xigua_snapshot_encode(&decoded, wire2, sizeof(wire2));
    assert(n == n2 && !memcmp(wire, wire2, n));
    saved = decoded;
    for (size_t i = 0; i < n; ++i) {
        wire[i] ^= 0x80;
        assert(!xigua_snapshot_decode(&decoded, wire, n));
        assert(!memcmp(&decoded, &saved, sizeof(saved)));
        wire[i] ^= 0x80;
    }
    assert(!xigua_snapshot_decode(&decoded, wire, n - 1));
    assert(!xigua_snapshot_encode(&s, wire2, n - 1));
}
static void test_feed_undo_replay(void)
{
    reset();
    rejected(command(XIGUA_FEED_ADD, 9), XIGUA_INVALID);
    rejected(command(XIGUA_FEED_ADD, 401), XIGUA_INVALID);
    assert(apply(XIGUA_FEED_ADD, 10).status == XIGUA_OK);
    xigua_command_t add = command(XIGUA_FEED_ADD, 400);
    xigua_result_t original = xigua_apply(&s, &add, &now);
    assert(original.status == XIGUA_OK && s.event_count == 2);
    saved = s;
    xigua_result_t retry = xigua_apply(&s, &add, &now);
    assert(retry.replayed && retry.revision == original.revision && retry.event_id == original.event_id);
    assert(!memcmp(&s, &saved, sizeof(s)));
    add.value = 399; rejected(add, XIGUA_ID_REUSED);
    xigua_command_t edit = command(XIGUA_FEED_EDIT, 100);
    --edit.expected_revision; rejected(edit, XIGUA_CONFLICT);
    edit = command(XIGUA_FEED_EDIT, 100); edit.target_event_id = 1; rejected(edit, XIGUA_CONFLICT);
    assert(apply(XIGUA_FEED_EDIT, 100).status == XIGUA_OK);
    xigua_command_t stale = command(XIGUA_UNDO, 0); --stale.expected_revision; rejected(stale, XIGUA_CONFLICT);
    assert(apply(XIGUA_UNDO, 0).status == XIGUA_OK);
    assert(s.events[1].value == 400 && s.events[1].revision == s.revision);
    rejected(command(XIGUA_UNDO, 0), XIGUA_NOT_FOUND);
    assert(apply(XIGUA_BATH_ADD, 0).status == XIGUA_OK);
    unsigned next = s.next_event_id;
    assert(apply(XIGUA_UNDO, 0).status == XIGUA_OK && s.event_count == 2 && s.next_event_id == next);
    roundtrip();
}
static void test_sleep_tummy_boots(void)
{
    reset();
    rejected(command(XIGUA_SLEEP_STOP, 0), XIGUA_NOT_ACTIVE);
    assert(apply(XIGUA_SLEEP_START, 0).status == XIGUA_OK);
    rejected(command(XIGUA_SLEEP_START, 0), XIGUA_ALREADY_ACTIVE);
    now.monotonic_ms += 61000;
    uint64_t ms;
    assert(xigua_elapsed(xigua_active_sleep(&s), &now, &ms) && ms == 61000);
    assert(apply(XIGUA_SLEEP_STOP, 0).status == XIGUA_OK && s.events[0].duration_ms == 61000);
    assert(apply(XIGUA_UNDO, 0).status == XIGUA_OK && xigua_active_sleep(&s));
    assert(apply(XIGUA_TUMMY_START, 0).status == XIGUA_OK);
    now.monotonic_ms += 120000;
    assert(apply(XIGUA_TUMMY_STOP, 0).status == XIGUA_OK && s.events[1].duration_ms == 120000);
    assert(xigua_reboot(&s, 2)); now.boot_id = 2; now.monotonic_ms = 10;
    assert(xigua_active_sleep(&s) && s.events[0].start.boot_id == 1);
    assert(!xigua_elapsed(xigua_active_sleep(&s), &now, &ms));
    assert(apply(XIGUA_SLEEP_STOP, 0).status == XIGUA_OK && s.events[0].duration_quality == XIGUA_DURATION_UNKNOWN);
    roundtrip();
    reset(); now.quality = XIGUA_TIME_TRUSTED; now.unix_ms = 100000;
    assert(apply(XIGUA_SLEEP_START, 0).status == XIGUA_OK);
    assert(xigua_reboot(&s, 2)); now.boot_id = 2; now.monotonic_ms = 0; now.unix_ms = 200000;
    assert(apply(XIGUA_SLEEP_STOP, 0).status == XIGUA_OK);
    assert(s.events[0].duration_ms == 100000 && s.events[0].duration_quality == XIGUA_DURATION_UTC);
    roundtrip();
}
static void test_timer_and_time(void)
{
    reset();
    rejected(command(XIGUA_TIMER_START, 999), XIGUA_INVALID);
    rejected(command(XIGUA_TIMER_START, 86400001), XIGUA_INVALID);
    assert(apply(XIGUA_TIMER_START, 60000).status == XIGUA_OK);
    rejected(command(XIGUA_TIMER_START, 1000), XIGUA_ALREADY_ACTIVE);
    now.monotonic_ms += 20000; uint32_t ms;
    assert(xigua_timer_remaining(&s, &now, &ms) && ms == 40000);
    assert(apply(XIGUA_TIMER_CANCEL, 0).status == XIGUA_OK && !s.timer.active);
    assert(apply(XIGUA_UNDO, 0).status == XIGUA_OK && s.timer.active);
    now.monotonic_ms += 40000;
    assert(xigua_timer_remaining(&s, &now, &ms) && ms == 0);
    assert(xigua_reboot(&s, 2)); now.boot_id = 2;
    assert(!xigua_timer_remaining(&s, &now, &ms));
    assert(apply(XIGUA_TIMER_CANCEL, 0).status == XIGUA_OK);
    roundtrip();
    reset(); assert(apply(XIGUA_SLEEP_START, 0).status == XIGUA_OK);
    now.monotonic_ms += 90000;
    assert(apply(XIGUA_SLEEP_STOP, 0).status == XIGUA_OK);
    xigua_clock_t anchor = {1, 101000, 86410000, XIGUA_TIME_TRUSTED};
    assert(xigua_reconstruct(&s, &anchor) == 3); /* start/end plus undo's active start */
    assert(s.events[0].start.unix_ms == 86310000 && s.events[0].end.unix_ms == 86400000);
    assert(xigua_reconstruct(&s, &anchor) == 0);
    assert(s.events[0].start.quality == XIGUA_TIME_RECONSTRUCTED);
    roundtrip();
    reset(); now.quality = XIGUA_TIME_TRUSTED; now.unix_ms = 100000;
    assert(apply(XIGUA_SLEEP_START, 0).status == XIGUA_OK);
    assert(xigua_reboot(&s, 2)); now = (xigua_clock_t){2, 1000, 0, XIGUA_TIME_UNKNOWN};
    assert(apply(XIGUA_SLEEP_STOP, 0).status == XIGUA_OK);
    assert(s.events[0].duration_quality == XIGUA_DURATION_UNKNOWN);
    anchor = (xigua_clock_t){2, 2000, 201000, XIGUA_TIME_TRUSTED};
    assert(xigua_reconstruct(&s, &anchor) == 1);
    assert(s.events[0].duration_quality == XIGUA_DURATION_UTC && s.events[0].duration_ms == 100000);
    roundtrip();
}
static void test_statistics(void)
{
    reset(); now.quality = XIGUA_TIME_TRUSTED; now.unix_ms = 86340000;
    assert(apply(XIGUA_SLEEP_START, 0).status == XIGUA_OK);
    now.monotonic_ms += 180000; now.unix_ms += 180000;
    assert(apply(XIGUA_SLEEP_STOP, 0).status == XIGUA_OK);
    assert(apply(XIGUA_FEED_ADD, 120).status == XIGUA_OK);
    assert(apply(XIGUA_DIAPER_ADD, 3).status == XIGUA_OK);
    assert(apply(XIGUA_BATH_ADD, 0).status == XIGUA_OK);
    assert(apply(XIGUA_TUMMY_START, 0).status == XIGUA_OK);
    now.monotonic_ms += 120000; now.unix_ms += 120000;
    assert(apply(XIGUA_TUMMY_STOP, 0).status == XIGUA_OK);
    now.quality = XIGUA_TIME_UNKNOWN; now.unix_ms = 0;
    assert(apply(XIGUA_FEED_ADD, 60).status == XIGUA_OK);
    xigua_stats_t stats;
    xigua_stats(&s, &now, false, 0, 86400000, &stats);
    assert(stats.sleep_ms == 60000 && stats.feed_count == 0 && stats.pending_count == 1);
    xigua_stats(&s, &now, false, 86400000, 172800000, &stats);
    assert(stats.sleep_ms == 120000 && stats.feed_count == 1 && stats.feed_ml == 120 &&
        stats.diaper_count == 1 && stats.bath_count == 1 && stats.tummy_minutes == 2 && stats.pending_count == 1);
    xigua_stats(&s, &now, true, 0, 0, &stats);
    assert(stats.sleep_ms == 180000 && stats.feed_count == 2 && stats.feed_ml == 180 && stats.pending_count == 1);
    reset();
    for (unsigned i = 0; i < 2; ++i) {
        assert(apply(XIGUA_TUMMY_START, 0).status == XIGUA_OK);
        now.monotonic_ms += 30000;
        assert(apply(XIGUA_TUMMY_STOP, 0).status == XIGUA_OK);
    }
    xigua_stats(&s, &now, true, 0, 0, &stats);
    assert(stats.tummy_minutes == 1 && stats.pending_count == 2);
    assert(apply(XIGUA_SLEEP_START, 0).status == XIGUA_OK);
    assert(xigua_reboot(&s, 2)); now.boot_id = 2;
    xigua_stats(&s, &now, true, 0, 0, &stats);
    assert(stats.pending_count == 3);
}
static void test_daily_statistics_edges(void)
{
    xigua_stats_t stats;
    reset();
    memset(&stats, 0xff, sizeof(stats));
    xigua_stats(&s, &now, false, 0, 86400000, &stats);
    assert(!stats.feed_count && !stats.sleep_ms && !stats.pending_count);
    assert(apply(XIGUA_FEED_ADD, 100).status == XIGUA_OK);
    xigua_stats(&s, &now, false, 1, 1, &stats);
    assert(!stats.feed_count && !stats.pending_count);
    xigua_stats(&s, &now, false, -1, 1, &stats);
    assert(!stats.feed_count && !stats.pending_count);
    xigua_stats(&s, &now, false, 2, 1, &stats);
    assert(!stats.feed_count && !stats.pending_count);

    /* Exact midnight belongs only to the next day; a stopped interval excludes its end. */
    reset(); now.quality = XIGUA_TIME_TRUSTED; now.unix_ms = 86340000;
    assert(apply(XIGUA_SLEEP_START, 0).status == XIGUA_OK);
    now.monotonic_ms += 60000; now.unix_ms += 60000;
    assert(apply(XIGUA_SLEEP_STOP, 0).status == XIGUA_OK);
    assert(apply(XIGUA_FEED_ADD, 100).status == XIGUA_OK);
    xigua_stats(&s, &now, false, 0, 86400000, &stats);
    assert(stats.sleep_ms == 60000 && !stats.feed_count);
    xigua_stats(&s, &now, false, 86400000, 172800000, &stats);
    assert(!stats.sleep_ms && stats.feed_count == 1);

    /* Both ongoing duration types split across midnight without mutating the store. */
    reset(); now.quality = XIGUA_TIME_TRUSTED; now.unix_ms = 86340000;
    assert(apply(XIGUA_SLEEP_START, 0).status == XIGUA_OK);
    assert(apply(XIGUA_TUMMY_START, 0).status == XIGUA_OK);
    now.monotonic_ms += 180000; now.unix_ms += 180000;
    saved = s;
    xigua_stats(&s, &now, false, 0, 86400000, &stats);
    assert(stats.sleep_ms == 60000 && stats.tummy_minutes == 1 && !stats.pending_count);
    xigua_stats(&s, &now, false, 86400000, 172800000, &stats);
    assert(stats.sleep_ms == 120000 && stats.tummy_minutes == 2 && !stats.pending_count);
    assert(!memcmp(&s, &saved, sizeof(s)));

    /* A wall clock step cannot turn three monotonic minutes into hours or negative time. */
    now.unix_ms += 7200000;
    xigua_stats(&s, &now, false, 86400000, 172800000, &stats);
    assert(stats.sleep_ms == 120000 && stats.tummy_minutes == 2);
    now.unix_ms = 1000;
    assert(apply(XIGUA_SLEEP_STOP, 0).status == XIGUA_OK);
    assert(apply(XIGUA_TUMMY_STOP, 0).status == XIGUA_OK);
    xigua_stats(&s, &now, false, 86400000, 172800000, &stats);
    assert(stats.sleep_ms == 120000 && stats.tummy_minutes == 2);
    roundtrip();

    /* An undated end/ongoing clock is pending once, while all-time retains duration. */
    reset(); now.quality = XIGUA_TIME_TRUSTED; now.unix_ms = 100000;
    assert(apply(XIGUA_SLEEP_START, 0).status == XIGUA_OK);
    now.monotonic_ms += 60000; now.quality = XIGUA_TIME_UNKNOWN; now.unix_ms = 0;
    xigua_stats(&s, &now, false, 0, 86400000, &stats);
    assert(!stats.sleep_ms && stats.pending_count == 1);
    xigua_stats(&s, &now, true, 0, 0, &stats);
    assert(stats.sleep_ms == 60000 && stats.pending_count == 1);
    assert(apply(XIGUA_SLEEP_STOP, 0).status == XIGUA_OK);
    xigua_stats(&s, &now, false, 0, 86400000, &stats);
    assert(!stats.sleep_ms && stats.pending_count == 1);
    xigua_stats(&s, NULL, true, 0, 0, &stats);
    assert(stats.sleep_ms == 60000 && stats.pending_count == 1);
    xigua_clock_t anchor = {1, now.monotonic_ms, 160000, XIGUA_TIME_TRUSTED};
    assert(xigua_reconstruct(&s, &anchor) == 1);
    xigua_stats(&s, NULL, false, 0, 86400000, &stats);
    assert(stats.sleep_ms == 60000 && !stats.pending_count);
    roundtrip();

    /* Reboot with an unknown start stays pending even if the end is dated. */
    reset(); assert(apply(XIGUA_TUMMY_START, 0).status == XIGUA_OK);
    assert(xigua_reboot(&s, 2));
    now = (xigua_clock_t){2, 1000, 86460000, XIGUA_TIME_TRUSTED};
    assert(apply(XIGUA_TUMMY_STOP, 0).status == XIGUA_OK);
    xigua_stats(&s, &now, true, 0, 0, &stats);
    assert(!stats.tummy_minutes && stats.pending_count == 1);
    xigua_stats(&s, &now, false, 86400000, 172800000, &stats);
    assert(!stats.tummy_minutes && stats.pending_count == 1);

    /* Known cross-boot endpoints can split a duration across days. */
    reset(); now.quality = XIGUA_TIME_TRUSTED; now.unix_ms = 86340000;
    assert(apply(XIGUA_SLEEP_START, 0).status == XIGUA_OK);
    assert(xigua_reboot(&s, 2));
    now = (xigua_clock_t){2, 1000, 86460000, XIGUA_TIME_TRUSTED};
    xigua_stats(&s, &now, false, 86400000, 172800000, &stats);
    assert(stats.sleep_ms == 60000 && !stats.pending_count);
    now.unix_ms = 86000000; /* Reversed cross-boot UTC cannot supply a duration. */
    xigua_stats(&s, &now, true, 0, 0, &stats);
    assert(!stats.sleep_ms && stats.pending_count == 1);

    /* Guard extrapolated UTC overflow and saturate extreme all-time totals. */
    reset(); now.quality = XIGUA_TIME_TRUSTED; now.unix_ms = INT64_MAX - 1000;
    assert(apply(XIGUA_SLEEP_START, 0).status == XIGUA_OK);
    now.monotonic_ms += 2000;
    xigua_stats(&s, &now, false, INT64_MAX - 10000, INT64_MAX, &stats);
    assert(!stats.sleep_ms && stats.pending_count == 1);
    reset();
    for (unsigned i = 0; i < 2; ++i) {
        assert(apply(XIGUA_SLEEP_START, 0).status == XIGUA_OK);
        now.monotonic_ms = UINT64_MAX;
        assert(apply(XIGUA_SLEEP_STOP, 0).status == XIGUA_OK);
        assert(xigua_reboot(&s, s.boot_id + 1));
        now = (xigua_clock_t){s.boot_id, 0, 0, XIGUA_TIME_UNKNOWN};
    }
    assert(apply(XIGUA_TUMMY_START, 0).status == XIGUA_OK);
    now.monotonic_ms = UINT64_MAX;
    xigua_stats(&s, &now, true, 0, 0, &stats);
    assert(stats.sleep_ms == UINT64_MAX && stats.tummy_minutes == UINT32_MAX && stats.pending_count == 3);
}
static void test_capacity_and_replay_window(void)
{
    reset(); xigua_command_t first = command(XIGUA_FEED_ADD, 100);
    assert(xigua_apply(&s, &first, &now).status == XIGUA_OK);
    for (unsigned i = 1; i < XIGUA_EVENT_CAPACITY; ++i) assert(apply(XIGUA_FEED_ADD, 100).status == XIGUA_OK);
    assert(s.replay_count == 16 && s.event_count == 128);
    assert(xigua_snapshot_encode(&s, wire, sizeof(wire)) == 8975);
    rejected(command(XIGUA_BATH_ADD, 0), XIGUA_FULL);
    assert(apply(XIGUA_FEED_EDIT, 200).status == XIGUA_OK);
    roundtrip();
    s = decoded;
    xigua_replay_t last = s.replay[(s.replay_next + 15) % 16];
    saved = s;
    assert(xigua_apply(&s, &last.command, &now).replayed);
    assert(!memcmp(&s, &saved, sizeof(s)));
    assert(apply(XIGUA_UNDO, 0).status == XIGUA_OK);
    assert(s.events[127].value == 100);
    /* Evicted IDs no longer replay; the full store still rejects them without mutation. */
    rejected(first, XIGUA_FULL);
}
static void test_reconstruct_undo_and_limits(void)
{
    reset();
    assert(apply(XIGUA_TIMER_START, 60000).status == XIGUA_OK);
    assert(apply(XIGUA_TIMER_CANCEL, 0).status == XIGUA_OK);
    uint32_t revision = s.revision;
    xigua_clock_t anchor = {1, 2000, 100000, XIGUA_TIME_TRUSTED};
    assert(xigua_reconstruct(&s, &anchor) == 1);
    assert(s.revision == revision + 1 && !s.timer.active);
    roundtrip();
    s = decoded;
    assert(apply(XIGUA_UNDO, 0).status == XIGUA_OK);
    assert(s.timer.active && s.timer.start.quality == XIGUA_TIME_RECONSTRUCTED &&
        s.timer.start.unix_ms == 99000);
    saved = s;
    assert(xigua_reconstruct(&s, &anchor) == 0);
    assert(!memcmp(&s, &saved, sizeof(s)));

    reset();
    assert(apply(XIGUA_SLEEP_START, 0).status == XIGUA_OK);
    now.monotonic_ms = 999;
    rejected(command(XIGUA_SLEEP_STOP, 0), XIGUA_INVALID);
    anchor = (xigua_clock_t){1, 2000, 500, XIGUA_TIME_TRUSTED};
    saved = s;
    assert(xigua_reconstruct(&s, &anchor) == 0); /* Would predate the epoch. */
    assert(!memcmp(&s, &saved, sizeof(s)));
    anchor = (xigua_clock_t){1, 0, INT64_MAX, XIGUA_TIME_TRUSTED};
    assert(xigua_reconstruct(&s, &anchor) == 0); /* Would overflow UTC. */
    assert(!memcmp(&s, &saved, sizeof(s)));
    assert(!xigua_reboot(&s, 1) && !xigua_reboot(&s, 0));
    assert(!memcmp(&s, &saved, sizeof(s)));
    s.revision = UINT32_MAX;
    rejected(command(XIGUA_BATH_ADD, 0), XIGUA_INVALID);
    assert(!xigua_reboot(&s, 2));
}
static void test_snapshot_semantics(void)
{
    reset();
    assert(apply(XIGUA_BATH_ADD, 0).status == XIGUA_OK);
    saved = s;
    ++s.events[0].end.monotonic_ms;
    s.events[0].duration_ms = 1;
    assert(!xigua_snapshot_encode(&s, wire, sizeof(wire)));
    s = saved;
    size_t n = xigua_snapshot_encode(&s, wire, sizeof(wire));
    assert(n);
    /* Unknown event type with a recomputed CRC must still fail semantic validation. */
    wire[16 + 14 + 8] = 99;
    uint32_t crc = UINT32_MAX;
    for (size_t i = 16; i < n; ++i) {
        crc ^= wire[i];
        for (unsigned j = 0; j < 8; ++j) crc = (crc >> 1) ^ (0xedb88320u & (0u - (crc & 1u)));
    }
    crc = ~crc;
    for (unsigned i = 0; i < 4; ++i) wire[12 + i] = (uint8_t)(crc >> (8 * i));
    decoded = saved;
    assert(!xigua_snapshot_decode(&decoded, wire, n));
    assert(!memcmp(&decoded, &saved, sizeof(saved)));

    reset();
    assert(apply(XIGUA_SLEEP_START, 0).status == XIGUA_OK);
    assert(apply(XIGUA_SLEEP_STOP, 0).status == XIGUA_OK);
    assert(apply(XIGUA_SLEEP_START, 0).status == XIGUA_OK);
    assert(apply(XIGUA_SLEEP_STOP, 0).status == XIGUA_OK);
    s.events[0].active = true;
    s.events[0].duration_quality = XIGUA_DURATION_UNKNOWN;
    assert(!xigua_snapshot_encode(&s, wire, sizeof(wire))); /* Active event cannot be hidden by a newer record. */
}
static void test_command_snapshot_sequences(void)
{
    reset();
    uint32_t random = 17;
    for (unsigned i = 0; i < 2000; ++i) {
        random = random * 1664525u + 1013904223u;
        xigua_action_t action = (xigua_action_t)(1 + random % XIGUA_UNDO);
        unsigned value = action == XIGUA_FEED_ADD || action == XIGUA_FEED_EDIT ? 100 :
            action == XIGUA_DIAPER_ADD ? 3 : action == XIGUA_TIMER_START ? 60000 : 0;
        now.monotonic_ms += 1234;
        if (now.quality != XIGUA_TIME_UNKNOWN) now.unix_ms += 1234;
        saved = s;
        xigua_result_t r = apply(action, value);
        if (r.status != XIGUA_OK) assert(!memcmp(&s, &saved, sizeof(s)));
        if (i % 73 == 0) {
            xigua_clock_t anchor = {s.boot_id, now.monotonic_ms, 100000000 + (int64_t)i * 1234, XIGUA_TIME_TRUSTED};
            (void)xigua_reconstruct(&s, &anchor);
            now = anchor;
        }
        if (i % 101 == 0) {
            assert(xigua_reboot(&s, s.boot_id + 1));
            now = (xigua_clock_t){s.boot_id, 0, 0, XIGUA_TIME_UNKNOWN};
        }
        size_t n = xigua_snapshot_encode(&s, wire, sizeof(wire));
        assert(n && xigua_snapshot_decode(&decoded, wire, n));
        assert(xigua_snapshot_encode(&decoded, wire2, sizeof(wire2)) == n && !memcmp(wire, wire2, n));
        s = decoded;
    }
}
int main(void)
{
    test_feed_undo_replay(); test_sleep_tummy_boots(); test_timer_and_time();
    test_statistics(); test_daily_statistics_edges(); test_capacity_and_replay_window();
    test_reconstruct_undo_and_limits(); test_snapshot_semantics(); test_command_snapshot_sequences();
    puts("xigua core tests passed"); return 0;
}
