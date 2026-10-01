#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "xigua_adpcm.h"

/* Append-only temporary audio storage. Publish only after a successful Flash
 * write; advance the reader only after the corresponding PCM write succeeds.
 * The application protects shared snapshots/publications with its task lock. */
typedef struct {
    size_t capacity_blocks;
    size_t written_blocks, read_blocks;
    size_t written_pcm, read_pcm;
} xigua_tts_cache_t;

static inline void xigua_tts_cache_init(xigua_tts_cache_t *cache, size_t capacity)
{
    *cache = (xigua_tts_cache_t){ .capacity_blocks = capacity / sizeof(xigua_adpcm_block_t) };
}

static inline size_t xigua_tts_cache_write_offset(const xigua_tts_cache_t *cache)
{
    return cache->written_blocks < cache->capacity_blocks ?
           cache->written_blocks * sizeof(xigua_adpcm_block_t) : SIZE_MAX;
}

static inline bool xigua_tts_cache_publish(xigua_tts_cache_t *cache, size_t pcm_bytes)
{
    if (!pcm_bytes || (pcm_bytes & 1) || pcm_bytes > XIGUA_ADPCM_PCM_BYTES ||
        cache->written_blocks >= cache->capacity_blocks) return false;
    ++cache->written_blocks;
    cache->written_pcm += pcm_bytes;
    return true;
}

static inline size_t xigua_tts_cache_read_offset(const xigua_tts_cache_t *cache)
{
    return cache->read_blocks < cache->written_blocks ?
           cache->read_blocks * sizeof(xigua_adpcm_block_t) : SIZE_MAX;
}

static inline bool xigua_tts_cache_advance(xigua_tts_cache_t *cache, size_t pcm_bytes)
{
    if (!pcm_bytes || (pcm_bytes & 1) || pcm_bytes > XIGUA_ADPCM_PCM_BYTES ||
        cache->read_blocks >= cache->written_blocks ||
        pcm_bytes > cache->written_pcm - cache->read_pcm) return false;
    ++cache->read_blocks;
    cache->read_pcm += pcm_bytes;
    return true;
}

static inline void xigua_tts_cache_restart(xigua_tts_cache_t *cache)
{
    cache->read_blocks = cache->read_pcm = 0;
}

static inline size_t xigua_tts_cache_available(const xigua_tts_cache_t *cache)
{
    return cache->written_pcm - cache->read_pcm;
}
