#ifndef XIGUA_UI_H
#define XIGUA_UI_H

#include <stdbool.h>
#include <stdint.h>
#include "xigua_core.h"
#include "xigua_service.h"
#include "xigua_audio.h"

/* All functions run in the LVGL task or while the caller holds bsp_lvgl_lock().
 * UI never reads hardware or persists data. The caller owns view.state. */
typedef enum {
    XIGUA_UI_UP_CLICK, XIGUA_UI_DOWN_CLICK, XIGUA_UI_OK_CLICK,
    XIGUA_UI_UP_LONG, XIGUA_UI_DOWN_LONG, XIGUA_UI_OK_LONG
} xigua_ui_key_t;

typedef enum {
    XIGUA_UI_IDLE, XIGUA_UI_SAVING, XIGUA_UI_SAVED,
    XIGUA_UI_ERROR, XIGUA_UI_STORAGE_FULL, XIGUA_UI_READ_ONLY
} xigua_ui_feedback_t;

typedef struct {
    const xigua_state_t *state;
    xigua_clock_t now;
    int battery_pct;            /* -1 when unavailable */
    bool read_only;
    xigua_ui_feedback_t feedback;
    bool undo_available;        /* controller limits this to five seconds */
    xigua_service_status_t service;
    xigua_audio_status_t audio;
} xigua_ui_view_t;

typedef enum { XIGUA_UI_AUDIO_NONE, XIGUA_UI_AUDIO_PLAY,
    XIGUA_UI_AUDIO_PAUSE, XIGUA_UI_AUDIO_VOLUME } xigua_ui_audio_t;
typedef struct {
    bool valid;
    xigua_action_t action;
    uint32_t value;             /* ml, diaper kind, or timer ms; start/stop uses 0 */
    xigua_ui_audio_t audio;     /* audio intents never mutate event storage */
} xigua_ui_intent_t;

void xigua_ui_create(void);
void xigua_ui_destroy(void);
void xigua_ui_render(const xigua_ui_view_t *view);
xigua_ui_intent_t xigua_ui_key(xigua_ui_key_t key, const xigua_ui_view_t *view);
/* Call after a persisted command finishes. Success returns to overview.
 * Failure leaves the editor open so the user can retry or go back. */
void xigua_ui_command_result(bool saved);

#endif
