#include "xigua_tts_downsample.h"

/* 31-tap Hamming-windowed sinc, cutoff 4.8 kHz, normalized Q15 DC gain.
 * Suppress frequencies above the new 6 kHz Nyquist limit before decimation.
 * Sum of absolute coefficients is 52420, so full-scale PCM fits int32_t. */
static const int16_t coefficients[31] = {
    0, -64, -57, 86, 210, 0, -439, -378, 516, 1130, 0, -2107, -1868,
    2949, 9839, 13134, 9839, 2949, -1868, -2107, 0, 1130, 516, -378,
    -439, 0, 210, 86, -57, -64, 0
};

bool xigua_tts_downsample_push(xigua_tts_downsample_t *state, int16_t sample,
                               int16_t *output)
{
    state->history[state->next] = sample;
    state->next = (state->next + 1) % 31;
    state->phase = !state->phase;
    if (state->phase) return false;
    int32_t sum = 0;
    unsigned at = state->next;
    for (unsigned i = 0; i < 31; ++i) {
        at = at ? at - 1 : 30;
        sum += (int32_t)state->history[at] * coefficients[i];
    }
    int32_t value = sum / 32768;
    if (value > 32767) value = 32767;
    if (value < -32768) value = -32768;
    *output = (int16_t)value;
    return true;
}
