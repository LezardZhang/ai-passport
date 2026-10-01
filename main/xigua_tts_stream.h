#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Incremental MiMo SSE/JSON/base64 decoder. No allocation or ESP-IDF dependency.
 * Only choices[0].delta.audio.data is sent to the PCM16 sink. */
typedef bool (*xigua_tts_sink_t)(void *context, const uint8_t *pcm, size_t bytes);
typedef struct {
    uint8_t kind, state, path;
    unsigned index;
} xigua_tts_frame_t;
typedef struct {
    xigua_tts_sink_t sink;
    void *context;
    xigua_tts_frame_t frames[16];
    unsigned depth;
    unsigned mode, prefix_length, token, escape, unicode_left;
    bool root_complete, key, audio, padded, failed, done;
    char name[32], prefix[8], literal[32];
    size_t name_length, literal_length;
    uint8_t quartet[4], pcm[1024];
    size_t quartet_length, pcm_length, audio_bytes, bytes, chunks;
} xigua_tts_stream_t;

void xigua_tts_stream_init(xigua_tts_stream_t *stream, xigua_tts_sink_t sink, void *context);
bool xigua_tts_stream_feed(xigua_tts_stream_t *stream, const char *data, size_t bytes);
/* Requires a complete JSON event, at least one PCM sample, and [DONE]. */
bool xigua_tts_stream_finish(xigua_tts_stream_t *stream);
