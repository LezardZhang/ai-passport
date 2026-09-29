#include "xigua_export.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    xigua_state_t state = {0};
    state.boot_id = 7; state.revision = 12; state.event_count = 2;
    state.events[0].id = 41; state.events[0].type = XIGUA_FEED;
    state.events[0].start.unix_ms = 1780000000000LL;
    state.events[1].id = 42; state.events[1].revision = 3;
    state.events[1].type = XIGUA_SLEEP; state.events[1].active = true;
    xigua_export_result_t result;
    xigua_export_request_t request = {.op = XG_EXPORT_BEGIN};
    assert(xigua_export_read(&state, &request, &result) == XG_EXPORT_OK);
    assert(result.boot_id == 7 && result.revision == 12 && result.count == 2 && !result.has_event);
    request = (xigua_export_request_t){.op = XG_EXPORT_ITEM, .boot_id = result.boot_id,
        .revision = result.revision, .count = result.count, .index = 1};
    assert(xigua_export_read(&state, &request, &result) == XG_EXPORT_OK);
    assert(result.has_event && result.event.id == 42 && result.event.revision == 3 && result.event.active);
    request.index = 2;
    assert(xigua_export_read(&state, &request, &result) == XG_EXPORT_RANGE);
    request.op = XG_EXPORT_FINISH;
    assert(xigua_export_read(&state, &request, &result) == XG_EXPORT_OK && !result.has_event);
    ++state.revision; /* Includes edits, additions, and clock reconstruction. */
    assert(xigua_export_read(&state, &request, &result) == XG_EXPORT_STALE);
    request.revision = state.revision; state.boot_id = 8;
    assert(xigua_export_read(&state, &request, &result) == XG_EXPORT_STALE);
    request.boot_id = state.boot_id; request.count = 1;
    assert(xigua_export_read(&state, &request, &result) == XG_EXPORT_STALE);
    puts("Xigua record export tests: PASS");
    return 0;
}
