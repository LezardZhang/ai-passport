#pragma once

#include "esp_err.h"
#include "xigua_audio_policy.h"
#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool available;          /* Worker exists; codec faults are reported in error. */
    bool busy;               /* Worker is preparing or switching audio. */
    bool playing;            /* Background PCM was most recently written. */
    bool alerting;           /* Alert PCM was most recently written. */
    bool requested_playing;  /* Latest UI intent, updated before the worker runs. */
    uint8_t requested_track;
    uint8_t track;           /* Current or most recently selected background track. */
    uint8_t volume;          /* Session setting: 0..100 in steps of 5. */
    esp_err_t error;         /* Last codec/PCM failure; ESP_OK after a successful retry. */
    uint32_t max_feed_gap_ms;
    uint32_t stack_min_bytes;
} xigua_audio_status_t;

/* Call once from the controller task. The codec is initialized only on play/alert. */
esp_err_t xigua_audio_start(void);

/* Nonblocking controller/task calls. Latest intent wins; no queue can drop STOP.
 * Requests may wake the audio worker but never touch codec or LVGL objects. */
void xigua_audio_request_play(uint8_t track); /* 0 white, 1 rain, 2 waves */
void xigua_audio_request_pause(void);
void xigua_audio_request_stop(void);
void xigua_audio_request_alert(void);
void xigua_audio_request_dismiss_alert(void);
void xigua_audio_request_volume(uint8_t level);
void xigua_audio_get_status(xigua_audio_status_t *out);
