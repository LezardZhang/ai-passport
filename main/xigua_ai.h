#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stddef.h>

#define XIGUA_AI_REPLY_BYTES 4096

typedef enum {
    XIGUA_AI_MODEL_CHAT = 0,
    XIGUA_AI_MODEL_CHAT_PRO,
    XIGUA_AI_MODEL_CHAT_LEGACY_PRO,
    XIGUA_AI_MODEL_CHAT_LEGACY,
    XIGUA_AI_MODEL_ASR,
    XIGUA_AI_MODEL_TTS,
    XIGUA_AI_MODEL_TTS_VOICECLONE,
    XIGUA_AI_MODEL_TTS_VOICEDESIGN,
    XIGUA_AI_MODEL_COUNT,
} xigua_ai_model_t;

typedef enum {
    XIGUA_AI_VOICE_IDLE = 0,
    XIGUA_AI_VOICE_RECORDING,
    XIGUA_AI_VOICE_TRANSCRIBING,
    XIGUA_AI_VOICE_THINKING,
} xigua_ai_voice_phase_t;

typedef enum {
    XIGUA_AI_HEALTH_OFFLINE = 0,
    XIGUA_AI_HEALTH_CHECKING,
    XIGUA_AI_HEALTH_NETWORK_FAILED,
    XIGUA_AI_HEALTH_MODEL_FAILED,
    XIGUA_AI_HEALTH_READY,
} xigua_ai_health_t;

esp_err_t xigua_ai_start(void);
void xigua_ai_stop(void);
bool xigua_ai_configured(void);
const char *xigua_ai_model(xigua_ai_model_t model);

/* Queues one bounded text request for the background worker. */
esp_err_t xigua_ai_request_text(const char *prompt);

/* Starts a push-to-talk capture; release it with xigua_ai_stop_voice(). */
esp_err_t xigua_ai_request_voice(void);
void xigua_ai_stop_voice(void);
xigua_ai_voice_phase_t xigua_ai_voice_phase(void);
xigua_ai_health_t xigua_ai_health(void);

/* Called from the LVGL task to collect a completed response. */
bool xigua_ai_take_text(char *text, size_t text_size, esp_err_t *error, bool *truncated);
