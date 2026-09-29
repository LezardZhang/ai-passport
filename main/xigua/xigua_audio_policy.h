#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XIGUA_AUDIO_VOLUME_MAX 100u
#define XIGUA_AUDIO_VOLUME_DEFAULT 40u
#define XIGUA_AUDIO_VOLUME_STEP 5u
#define XIGUA_AUDIO_TRACK_COUNT 3u
#define XIGUA_AUDIO_ALERT_SAMPLES 16000u

typedef enum {
    XIGUA_AUDIO_DESIRED_STOP = 0,
    XIGUA_AUDIO_DESIRED_PAUSE,
    XIGUA_AUDIO_DESIRED_PLAY,
} xigua_audio_desired_t;

typedef enum {
    XIGUA_AUDIO_SEGMENT_IDLE = 0,
    XIGUA_AUDIO_SEGMENT_BACKGROUND,
    XIGUA_AUDIO_SEGMENT_ALERT,
} xigua_audio_segment_t;

typedef struct {
    xigua_audio_desired_t desired;
    uint8_t track;
    uint8_t volume;
    uint32_t generation;
    uint32_t alert_serial;
    size_t alert_samples_remaining;
    bool fault;
} xigua_audio_policy_t;

typedef struct {
    xigua_audio_segment_t segment;
    uint8_t track;
    uint8_t volume;
    uint32_t generation;
    uint32_t alert_serial;
    size_t samples;
} xigua_audio_selection_t;

void xigua_audio_policy_init(xigua_audio_policy_t *policy);
bool xigua_audio_policy_play(xigua_audio_policy_t *policy, uint8_t track);
void xigua_audio_policy_pause(xigua_audio_policy_t *policy);
void xigua_audio_policy_stop(xigua_audio_policy_t *policy);
void xigua_audio_policy_alert(xigua_audio_policy_t *policy);
void xigua_audio_policy_dismiss_alert(xigua_audio_policy_t *policy);
void xigua_audio_policy_volume(xigua_audio_policy_t *policy, uint8_t volume);
xigua_audio_selection_t xigua_audio_policy_select(const xigua_audio_policy_t *policy,
                                                   size_t chunk_samples);
bool xigua_audio_policy_is_current(const xigua_audio_policy_t *policy,
                                   const xigua_audio_selection_t *selection);
void xigua_audio_policy_rendered(xigua_audio_policy_t *policy,
                                 const xigua_audio_selection_t *selection,
                                 size_t samples);
void xigua_audio_policy_fail_if_current(xigua_audio_policy_t *policy,
                                        const xigua_audio_selection_t *selection);
