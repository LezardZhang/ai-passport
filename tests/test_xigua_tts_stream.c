#include "xigua_tts_stream.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static size_t received, calls;
static bool reject;
static bool sink(void *ctx, const uint8_t *pcm, size_t bytes)
{
    (void)ctx;
    if (reject) return false;
    assert(bytes && bytes <= 1024 && !(bytes & 1));
    for (size_t i = 0; i < bytes; ++i) assert(pcm[i] == 0);
    received += bytes; ++calls;
    return true;
}
static void reset(xigua_tts_stream_t *s)
{
    received = calls = 0; reject = false;
    xigua_tts_stream_init(s, sink, NULL);
}
static void feed(xigua_tts_stream_t *s, const char *text, size_t stride)
{
    size_t len = strlen(text);
    for (size_t i = 0; i < len; i += stride) {
        size_t n = len - i < stride ? len - i : stride;
        assert(xigua_tts_stream_feed(s, text + i, n));
    }
}
static const char prefix[] = "data: {\"id\":\"ignored \\\"data\\\"\",\"choices\":[{\"index\":0,\"delta\":{\"audio\":{\"data\":\"";
static const char suffix[] = "\",\"expires_at\":123,\"transcript\":\"你好\"}}}],\"usage\":null}\r\n\r\ndata: [DONE]\r\n\r\n";
static bool invalid(const char *text)
{
    xigua_tts_stream_t s; reset(&s);
    return !xigua_tts_stream_feed(&s, text, strlen(text)) || !xigua_tts_stream_finish(&s);
}
int main(void)
{
    /* The observed tail chunk is 30,720 PCM bytes / 40,960 base64 bytes.
     * Exercise much larger events too; memory use stays below 2 KiB. */
    assert(sizeof(xigua_tts_stream_t) < 2048);
    for (size_t raw = 7680; raw <= 122880; raw *= 2) {
        size_t encoded = raw / 3 * 4;
        char *event = malloc(sizeof(prefix) + encoded + sizeof(suffix));
        assert(event);
        memcpy(event, prefix, sizeof(prefix) - 1);
        memset(event + sizeof(prefix) - 1, 'A', encoded);
        strcpy(event + sizeof(prefix) - 1 + encoded, suffix);
        for (size_t stride = 1; stride <= 1024; stride *= 4) {
            xigua_tts_stream_t s; reset(&s);
            feed(&s, event, stride);
            assert(xigua_tts_stream_finish(&s));
            assert(received == raw && s.bytes == raw && s.chunks == 1 && calls > 1);
        }
        free(event);
    }
    xigua_tts_stream_t s; reset(&s);
    feed(&s, ": heartbeat\n\nevent: message\ndata: {\"choices\":[{\"delta\":{\"audio\":{\"data\":\"AAA=\"}}}]}\n"
             "data: {\"choices\":[{\"delta\":{\"audio\":{\"data\":\"AAAAAA==\"}}}]}\n"
             "data: {\"choices\":[{\"delta\":{\"audio\":{\"data\":\"\"}}}]}\n"
             "data: [DONE]", 1);
    assert(xigua_tts_stream_finish(&s) && received == 6 && s.chunks == 2);
    assert(invalid("data: [DONE]\n")); /* No audio. */
    assert(invalid("data: {\"audio\":{\"data\":\"AAA=\"}}\ndata: [DONE]\n"));
    assert(invalid("data: {\"choices\":[{}, {\"delta\":{\"audio\":{\"data\":\"AAA=\"}}}]}\ndata: [DONE]\n"));
    assert(invalid("data: {\"choices\":[{\"delta\":{\"audio\":{\"data\":\"AAA=\"}}}]}\n")); /* Incomplete stream. */
    const char *bad[] = { "AA", "A===", "AA==", "AAA?", "AAA=A", "AAB=", "AAAA=", "AA\\nA=", "AAA=\\u0041" };
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        char event[512]; snprintf(event, sizeof(event), "%s%s%s", prefix, bad[i], suffix);
        assert(invalid(event));
    }
    assert(invalid("data: {\"choices\": [}\n"));
    assert(invalid("data: {\"choices\":[,]}\n"));
    assert(invalid("data: {\"choices\":[],}\n"));
    assert(invalid("data: {\"error\":\"bad request\"}\ndata: [DONE]\n"));
    reset(&s); reject = true;
    char event[512]; snprintf(event, sizeof(event), "%sAAA=%s", prefix, suffix);
    assert(!xigua_tts_stream_feed(&s, event, strlen(event)) && s.failed);
    puts("TTS incremental SSE, large audio, PCM boundaries, malformed/cancelled streams: PASS");
}
