#ifndef XIGUA_SOUND_H
#define XIGUA_SOUND_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XIGUA_SOUND_SAMPLE_RATE 16000u
#define XIGUA_SOUND_PEAK_LIMIT 24000
#define XIGUA_SOUND_ALERT_SAMPLES 16000u

typedef enum {
    XIGUA_SOUND_WHITE = 0,
    XIGUA_SOUND_RAIN,
    XIGUA_SOUND_WAVES,
    XIGUA_SOUND_ALERT,
    XIGUA_SOUND_TRACK_COUNT
} xigua_sound_track_t;

/* Private streaming state, exposed only for allocation by its single owner.
 * Synthetic textures (not environmental recordings), signed 16-bit mono PCM.
 * No heap, ESP-IDF, floating point, or full-track buffer is needed. */
typedef struct {
    xigua_sound_track_t track;
    uint32_t random, position, phase;
    int32_t lowpass, dc;
    uint16_t fade;
    uint16_t drop_age, drop_length, drop_wait, drop_step;
} xigua_sound_t;

/* Deterministically restarts the selected track, including a 20 ms ambient
 * fade-in. Invalid tracks return false and leave a silent, invalid state. */
bool xigua_sound_init(xigua_sound_t *state, xigua_sound_track_t track);

/* Ambient tracks fill samples indefinitely. ALERT emits exactly one second
 * (three enveloped tones with gaps) and then returns zero. Returns the number
 * of generated frames; any unused output tail is zeroed. Null state generates
 * silence/zero frames; null output or zero count does not advance the state.
 * Output is bounded by +/-XIGUA_SOUND_PEAK_LIMIT, before caller volume control.
 * Calls must be serialized by the owner; state must first be initialized. */
size_t xigua_sound_render(xigua_sound_t *state, int16_t *output, size_t samples);

#endif
