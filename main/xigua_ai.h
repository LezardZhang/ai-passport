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
    XIGUA_AI_VOICE_SPEAKING,
    XIGUA_AI_VOICE_PAUSED,
} xigua_ai_voice_phase_t;

typedef enum {
    XIGUA_AI_HEALTH_OFFLINE = 0,
    XIGUA_AI_HEALTH_CHECKING,
    XIGUA_AI_HEALTH_NETWORK_FAILED,
    XIGUA_AI_HEALTH_MODEL_FAILED,
    XIGUA_AI_HEALTH_READY,
} xigua_ai_health_t;

typedef enum { XIGUA_AI_AUDIO_PLAYING, XIGUA_AI_AUDIO_COMPLETE,
               XIGUA_AI_AUDIO_STOPPED, XIGUA_AI_AUDIO_FAILED,
               XIGUA_AI_AUDIO_BUFFERING, XIGUA_AI_AUDIO_PAUSED } xigua_ai_audio_state_t;

esp_err_t xigua_ai_start(void);
void xigua_ai_stop(void);
bool xigua_ai_network_idle(void);
bool xigua_ai_configured(void);
const char *xigua_ai_model(xigua_ai_model_t model);

/* Queues one bounded text request for the background worker. */
esp_err_t xigua_ai_request_text(const char *prompt);

/* Starts a push-to-talk capture; release it with xigua_ai_stop_voice(). */
esp_err_t xigua_ai_request_voice(void);
/* Starts a push-to-talk story capture; the completed story is spoken aloud. */
esp_err_t xigua_ai_request_story(void);
/* Replays the displayed text without recording or requesting another story. */
esp_err_t xigua_ai_read_reply(const char *text);
/* Offline short notification; skips occupied audio rather than interrupting it. */
esp_err_t xigua_ai_reminder_tone(void);
/* These controls retain the current audio position and never request new TTS. */
esp_err_t xigua_ai_pause_voice(void);
esp_err_t xigua_ai_resume_voice(void);
esp_err_t xigua_ai_restart_voice(void);
bool xigua_ai_take_audio_state(xigua_ai_audio_state_t *state);
void xigua_ai_stop_voice(void);
xigua_ai_voice_phase_t xigua_ai_voice_phase(void);
xigua_ai_health_t xigua_ai_health(void);

/* Called from the LVGL task to collect a completed response. A result from
 * another function is consumed with matching_mode=false and preserves text. */
bool xigua_ai_take_text(char *text, size_t text_size, esp_err_t *error, bool *truncated,
                        bool *audio_failed, bool expected_story, bool *matching_mode);

/* Serialized with ASR/TTS on the same worker. Only configured backend media is accepted. */
esp_err_t xigua_ai_play_backend(const char *url, const char *token, const char *command_id);

/* Cloud-song controls return immediately; pause releases HTTP/codec and the AI worker. */
bool xigua_ai_backend_paused(void);
void xigua_ai_cancel_backend(void);
esp_err_t xigua_ai_pause_backend(const char *command_id);
esp_err_t xigua_ai_resume_backend(const char *command_id);
esp_err_t xigua_ai_stop_backend(const char *command_id);
