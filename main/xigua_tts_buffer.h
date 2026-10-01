#pragma once

#include <stdbool.h>
#include <stddef.h>

#define XIGUA_TTS_PLAYBACK_HZ 12000U
#define XIGUA_TTS_PREFILL_BYTES (3U * XIGUA_TTS_PLAYBACK_HZ * 2U)

typedef struct {
    bool playing;
    unsigned underruns;
} xigua_tts_buffer_t;

/* Wait for a useful reserve before starting/restarting. A completed short
 * stream must drain even when it never reaches the prefill threshold. */
static inline bool xigua_tts_buffer_ready(xigua_tts_buffer_t *state, size_t queued,
                                         bool producer_done)
{
    if (state->playing && !queued && !producer_done) {
        state->playing = false;
        ++state->underruns;
    }
    if (!state->playing && queued &&
        (queued >= XIGUA_TTS_PREFILL_BYTES || producer_done)) state->playing = true;
    return state->playing && queued > 0;
}
