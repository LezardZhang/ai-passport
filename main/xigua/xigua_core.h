#ifndef XIGUA_CORE_H
#define XIGUA_CORE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XIGUA_EVENT_CAPACITY 128u
#define XIGUA_REPLAY_CAPACITY 16u
/* Schema 1 maximum: header16 + state14 + 128*event65 + timer26 + undo101
 * + replay metadata2 + 16*replay31 = 8975 bytes. Keep a small bounded margin. */
#define XIGUA_SNAPSHOT_MAX 9216u
#define XIGUA_SCHEMA_VERSION 1u

typedef enum { XIGUA_TIME_UNKNOWN, XIGUA_TIME_TRUSTED, XIGUA_TIME_RECONSTRUCTED } xigua_time_quality_t;
typedef enum { XIGUA_DURATION_UNKNOWN, XIGUA_DURATION_MONOTONIC, XIGUA_DURATION_UTC } xigua_duration_quality_t;
typedef enum { XIGUA_FEED = 1, XIGUA_SLEEP, XIGUA_DIAPER, XIGUA_BATH, XIGUA_TUMMY } xigua_event_type_t;
typedef enum {
    XIGUA_FEED_ADD = 1, XIGUA_FEED_EDIT, XIGUA_SLEEP_START, XIGUA_SLEEP_STOP,
    XIGUA_DIAPER_ADD, XIGUA_BATH_ADD, XIGUA_TUMMY_START, XIGUA_TUMMY_STOP,
    XIGUA_TIMER_START, XIGUA_TIMER_CANCEL, XIGUA_UNDO
} xigua_action_t;
typedef enum {
    XIGUA_OK, XIGUA_INVALID, XIGUA_CONFLICT, XIGUA_FULL, XIGUA_NOT_FOUND,
    XIGUA_ALREADY_ACTIVE, XIGUA_NOT_ACTIVE, XIGUA_ID_REUSED
} xigua_status_t;

typedef struct {
    uint32_t boot_id;
    uint64_t monotonic_ms;
    int64_t unix_ms;
    xigua_time_quality_t quality;
} xigua_clock_t;
typedef struct {
    uint32_t id, revision;
    xigua_event_type_t type;
    uint32_t value; /* feed: ml (10..400); diaper: wet=1, dirty=2, both=3 */
    xigua_clock_t start, end;
    uint64_t duration_ms;
    xigua_duration_quality_t duration_quality;
    bool active;
} xigua_event_t;
typedef struct {
    bool active;
    xigua_clock_t start;
    uint32_t duration_ms;
} xigua_timer_t;
typedef struct {
    uint64_t command_id; /* nonzero; persist caller counter across boots */
    xigua_action_t action;
    uint32_t target_event_id, expected_revision, value;
} xigua_command_t;
typedef struct {
    xigua_status_t status;
    uint32_t event_id, revision;
    bool replayed;
} xigua_result_t;
typedef struct {
    bool valid, had_event;
    uint32_t event_index, target_event_id;
    xigua_event_t previous_event;
    xigua_timer_t previous_timer;
} xigua_undo_t;
typedef struct { xigua_command_t command; xigua_result_t result; } xigua_replay_t;
typedef struct {
    uint32_t boot_id, revision, next_event_id;
    uint16_t event_count;
    xigua_event_t events[XIGUA_EVENT_CAPACITY];
    xigua_timer_t timer;
    xigua_undo_t undo;
    uint8_t replay_count, replay_next;
    xigua_replay_t replay[XIGUA_REPLAY_CAPACITY];
} xigua_state_t;
typedef struct {
    uint32_t feed_count, feed_ml, diaper_count, bath_count, tummy_minutes;
    uint32_t pending_count;
    uint64_t sleep_ms;
} xigua_stats_t;

void xigua_init(xigua_state_t *state, uint32_t boot_id);
/* On a decoded state call once with a new nonzero boot ID, then persist before accepting commands.
 * Starts retain their original boot ID. Timers from another boot require explicit cancel/restart. */
bool xigua_reboot(xigua_state_t *state, uint32_t boot_id);
/* Caller works on a candidate, persists successful result, then publishes. Errors never mutate.
 * Last 16 successful IDs survive snapshots; identical retry returns original result.
 * Edits target latest feed and require its revision. Undo targets the last operation's event
 * (zero for timer) and requires STATE revision. Start/add commands use target=revision=0.
 * Sleep stop requires active event ID and its event revision. Timer cancel uses state revision.
 * Timer start value is milliseconds (1000..86400000). No implicit overwrite or eviction. */
xigua_result_t xigua_apply(xigua_state_t *state, const xigua_command_t *command, const xigua_clock_t *now);
const xigua_event_t *xigua_event(const xigua_state_t *state, uint32_t id);
const xigua_event_t *xigua_latest(const xigua_state_t *state, xigua_event_type_t type);
const xigua_event_t *xigua_active_sleep(const xigua_state_t *state);
const xigua_event_t *xigua_active_tummy(const xigua_state_t *state);
bool xigua_elapsed(const xigua_event_t *event, const xigua_clock_t *now, uint64_t *elapsed_ms);
/* Returns false for inactive or cross-boot/uncertain timer. Zero remaining means due. */
bool xigua_timer_remaining(const xigua_state_t *state, const xigua_clock_t *now, uint32_t *remaining_ms);
/* Absolute range [begin,end), supplied by caller's timezone/calendar service. A record
 * with unknown start/end (now for ongoing records), unknown duration, or an unrepresentable
 * daily interval contributes pending_count once. Unknown endpoints/durations are excluded
 * from daily totals. all_time=true includes all known durations/counts even when undated.
 * Same-boot duration uses monotonic time, anchored to start UTC for daily clipping; wall
 * clock corrections never invent elapsed time. Pending count covers the complete store.
 * Invalid state/range inputs clear the output; callers must gate the daily view on valid
 * calendar bounds. Duration sums saturate on overflow. No records or schema are modified. */
void xigua_stats(const xigua_state_t *state, const xigua_clock_t *now, bool all_time,
                 int64_t begin_ms, int64_t end_ms, xigua_stats_t *stats);
/* Reconstruct unknown same-boot endpoints from first trusted anchor; caller persists candidate.
 * Does not supply a current time after a reboot. Returns number of repaired endpoints,
 * including the saved undo state, so any nonzero result must be persisted. */
unsigned xigua_reconstruct(xigua_state_t *state, const xigua_clock_t *anchor);
/* Canonical little-endian encoding, schema/length/CRC checked. Never persist raw structs.
 * Decode failure leaves destination untouched. Caller must provide scratch destination if
 * keeping a live state. Encoded maximum is below XIGUA_SNAPSHOT_MAX (9,216 bytes). */
size_t xigua_snapshot_encode(const xigua_state_t *state, uint8_t *output, size_t capacity);
bool xigua_snapshot_decode(xigua_state_t *state, const uint8_t *input, size_t length);

#endif
