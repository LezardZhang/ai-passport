#pragma once
#include "xigua_export.h"

/* Called only by the USB service worker. The controller is the sole state reader
 * for this path; the queue copies requests/results so timeout cannot leave a
 * dangling caller pointer. */
bool xigua_app_export(const xigua_export_request_t *request,
    xigua_export_status_t *status, xigua_export_result_t *result);
void xigua_app_start(void);
