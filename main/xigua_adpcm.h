#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define XIGUA_ADPCM_PCM_BYTES 1024U

/* Independent mono IMA ADPCM blocks; high nibble first, with the starting
 * predictor/index carried explicitly. PCM input/output is signed 16-bit LE. */
typedef struct { int predictor, index; } xigua_adpcm_state_t;
typedef struct {
    int16_t predictor;
    uint16_t pcm_bytes;
    uint8_t index;
    uint8_t data[XIGUA_ADPCM_PCM_BYTES / 4];
} xigua_adpcm_block_t;

bool xigua_adpcm_encode(xigua_adpcm_state_t *state, const uint8_t *pcm, size_t bytes,
                         xigua_adpcm_block_t *block);
bool xigua_adpcm_decode(const xigua_adpcm_block_t *block, uint8_t *pcm, size_t capacity);
