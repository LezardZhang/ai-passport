#pragma once

#include "bsp_button.h"
#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

// Offline childcare application. Slow work and persistence run outside LVGL;
// quota_app remains a separate practice application and is not part of this UI.
void xigua_app_enter(void);
void xigua_app_exit(void);
esp_err_t xigua_app_start(void);
esp_err_t xigua_app_stop(void);
void xigua_app_key(bsp_btn_t btn, bsp_btn_ev_t ev);

// Called by the voice/network worker after a model returns JSON.
// An optional root command_id makes retries idempotent when it is reused for
// the same request. Command actions are applied to local records;
// reply_text/tts_text are surfaced to the current assistant page without
// treating normal prose as a device command.
esp_err_t xigua_app_ai_response(const char *json);

/* AI worker: validate and persist a single structured record, replacing its
 * JSON with a local confirmation. Plain prose remains text; incomplete
 * commands are rejected. No LVGL objects are accessed. */
esp_err_t xigua_app_process_ai_reply(char *text, size_t capacity, bool truncated);
