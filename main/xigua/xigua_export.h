#ifndef XIGUA_EXPORT_H
#define XIGUA_EXPORT_H

#include "xigua_core.h"

/* One controller-owned read copies at most one record. No whole-state snapshot is
 * allocated; callers must present the same token for every item and finish. */
typedef enum { XG_EXPORT_BEGIN, XG_EXPORT_ITEM, XG_EXPORT_FINISH } xigua_export_op_t;
typedef enum { XG_EXPORT_OK, XG_EXPORT_STALE, XG_EXPORT_RANGE,
    XG_EXPORT_UNAVAILABLE } xigua_export_status_t;
typedef struct {
    xigua_export_op_t op;
    uint32_t boot_id, revision;
    uint16_t count, index;
} xigua_export_request_t;
typedef struct {
    uint32_t boot_id, revision;
    uint16_t count, index;
    bool has_event;
    xigua_event_t event;
} xigua_export_result_t;

xigua_export_status_t xigua_export_read(const xigua_state_t *state,
    const xigua_export_request_t *request, xigua_export_result_t *result);

#endif
