#pragma once

#include <stdbool.h>
#include <stdint.h>

/* 24 kHz input -> 12 kHz output. State persists across SSE/PCM boundaries. */
typedef struct {
    int16_t history[31];
    unsigned next;
    bool phase;
} xigua_tts_downsample_t;

bool xigua_tts_downsample_push(xigua_tts_downsample_t *state, int16_t sample,
                               int16_t *output);
