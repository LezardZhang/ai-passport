#include "xigua_adpcm.h"

#include <string.h>

/* Standard IMA quantizer step sizes and index adjustments. */
static const int steps[89] = {
    7,8,9,10,11,12,13,14,16,17,19,21,23,25,28,31,34,37,41,45,50,55,60,66,
    73,80,88,97,107,118,130,143,157,173,190,209,230,253,279,307,337,371,
    408,449,494,544,598,658,724,796,876,963,1060,1166,1282,1411,1552,1707,
    1878,2066,2272,2499,2749,3024,3327,3660,4026,4428,4871,5358,5894,6484,
    7132,7845,8630,9493,10442,11487,12635,13899,15289,16818,18500,20350,
    22385,24623,27086,29794,32767
};
static const int changes[8] = { -1,-1,-1,-1,2,4,6,8 };

static int clamp(int value, int low, int high)
{
    return value < low ? low : value > high ? high : value;
}

static int apply(xigua_adpcm_state_t *state, unsigned code)
{
    int step = steps[state->index];
    int difference = step >> 3;
    if (code & 4) difference += step;
    if (code & 2) difference += step >> 1;
    if (code & 1) difference += step >> 2;
    state->predictor = clamp(state->predictor + ((code & 8) ? -difference : difference),
                              -32768, 32767);
    state->index = clamp(state->index + changes[code & 7], 0, 88);
    return state->predictor;
}

bool xigua_adpcm_encode(xigua_adpcm_state_t *state, const uint8_t *pcm, size_t bytes,
                         xigua_adpcm_block_t *block)
{
    if (!state || !pcm || !block || !bytes || bytes > XIGUA_ADPCM_PCM_BYTES ||
        (bytes & 1) || state->index < 0 || state->index > 88 ||
        state->predictor < -32768 || state->predictor > 32767) return false;
    memset(block, 0, sizeof(*block));
    block->predictor = (int16_t)state->predictor;
    block->index = (uint8_t)state->index;
    block->pcm_bytes = (uint16_t)bytes;
    for (size_t sample = 0; sample < bytes / 2; ++sample) {
        unsigned word = pcm[sample * 2] | ((unsigned)pcm[sample * 2 + 1] << 8);
        int value = word > 32767 ? (int)word - 65536 : (int)word;
        int difference = value - state->predictor;
        unsigned code = difference < 0 ? 8 : 0;
        if (difference < 0) difference = -difference;
        int step = steps[state->index];
        if (difference >= step) { code |= 4; difference -= step; }
        step >>= 1;
        if (difference >= step) { code |= 2; difference -= step; }
        step >>= 1;
        if (difference >= step) code |= 1;
        apply(state, code);
        block->data[sample / 2] |= (uint8_t)(code << ((sample & 1) ? 0 : 4));
    }
    return true;
}

bool xigua_adpcm_decode(const xigua_adpcm_block_t *block, uint8_t *pcm, size_t capacity)
{
    if (!block || !pcm || !block->pcm_bytes || block->pcm_bytes > XIGUA_ADPCM_PCM_BYTES ||
        block->pcm_bytes > capacity || (block->pcm_bytes & 1) || block->index > 88) return false;
    xigua_adpcm_state_t state = { .predictor = block->predictor, .index = block->index };
    for (size_t sample = 0; sample < block->pcm_bytes / 2; ++sample) {
        unsigned code = (block->data[sample / 2] >> ((sample & 1) ? 0 : 4)) & 15;
        unsigned word = (uint16_t)apply(&state, code);
        pcm[sample * 2] = (uint8_t)word;
        pcm[sample * 2 + 1] = (uint8_t)(word >> 8);
    }
    return true;
}
