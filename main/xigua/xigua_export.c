#include "xigua_export.h"
#include <string.h>

xigua_export_status_t xigua_export_read(const xigua_state_t *state,
    const xigua_export_request_t *request, xigua_export_result_t *result)
{
    if (!state || !request || !result || state->event_count > XIGUA_EVENT_CAPACITY)
        return XG_EXPORT_RANGE;
    if (request->op != XG_EXPORT_BEGIN && request->op != XG_EXPORT_ITEM &&
        request->op != XG_EXPORT_FINISH) return XG_EXPORT_RANGE;
    if (request->op != XG_EXPORT_BEGIN &&
        (request->boot_id != state->boot_id || request->revision != state->revision ||
         request->count != state->event_count)) return XG_EXPORT_STALE;
    if (request->op == XG_EXPORT_ITEM && request->index >= state->event_count)
        return XG_EXPORT_RANGE;
    if (request->op == XG_EXPORT_FINISH && request->index != state->event_count)
        return XG_EXPORT_RANGE;
    memset(result, 0, sizeof(*result));
    result->boot_id = state->boot_id;
    result->revision = state->revision;
    result->count = state->event_count;
    result->index = request->index;
    if (request->op == XG_EXPORT_ITEM) {
        result->has_event = true;
        result->event = state->events[request->index];
    }
    return XG_EXPORT_OK;
}
