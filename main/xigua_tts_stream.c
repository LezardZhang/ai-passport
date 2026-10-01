#include "xigua_tts_stream.h"

#include <string.h>

enum { PREFIX, JSON, DONE_TOKEN, IGNORE };
enum { OBJECT = 1, ARRAY };
enum { KEY_OR_END, KEY, COLON, VALUE, VALUE_OR_END, COMMA_OR_END };
enum { OTHER, ROOT, CHOICES, CHOICE, DELTA, AUDIO, AUDIO_DATA };
enum { NO_TOKEN, STRING, LITERAL };

static bool whitespace(char c) { return c == ' ' || c == '\t' || c == '\r'; }
static bool hex(char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F'); }

static bool flush(xigua_tts_stream_t *s)
{
    if (!s->pcm_length) return true;
    if (!s->sink(s->context, s->pcm, s->pcm_length)) return false;
    s->bytes += s->pcm_length;
    s->pcm_length = 0;
    return true;
}

static int base64(char c)
{
    if (c >= 'A' && c <= 'Z') return c - 'A';
    if (c >= 'a' && c <= 'z') return c - 'a' + 26;
    if (c >= '0' && c <= '9') return c - '0' + 52;
    if (c == '+') return 62;
    if (c == '/') return 63;
    if (c == '=') return 64;
    return -1;
}

static bool audio_char(xigua_tts_stream_t *s, char c)
{
    int n = base64(c);
    if (n < 0 || s->padded) return false;
    s->quartet[s->quartet_length++] = (uint8_t)n;
    if (s->quartet_length < 4) return true;
    const uint8_t *q = s->quartet;
    if (q[0] == 64 || q[1] == 64 || (q[2] == 64 && q[3] != 64)) return false;
    unsigned count = q[2] == 64 ? 1 : q[3] == 64 ? 2 : 3;
    if ((count == 1 && (q[1] & 15)) || (count == 2 && (q[2] & 3))) return false;
    uint8_t decoded[3] = { (uint8_t)((q[0] << 2) | (q[1] >> 4)),
                           (uint8_t)((q[1] << 4) | (q[2] >> 2)),
                           (uint8_t)((q[2] << 6) | q[3]) };
    for (unsigned i = 0; i < count; ++i) {
        s->pcm[s->pcm_length++] = decoded[i];
        ++s->audio_bytes;
        if (s->pcm_length == sizeof(s->pcm) && !flush(s)) return false;
    }
    s->padded = count != 3;
    s->quartet_length = 0;
    return true;
}

static unsigned value_path(xigua_tts_stream_t *s)
{
    if (!s->depth) return ROOT;
    xigua_tts_frame_t *f = &s->frames[s->depth - 1];
    if (f->kind == ARRAY) return f->path == CHOICES && f->index == 0 ? CHOICE : OTHER;
    if (f->path == ROOT && !strcmp(s->name, "choices")) return CHOICES;
    if (f->path == CHOICE && !strcmp(s->name, "delta")) return DELTA;
    if (f->path == DELTA && !strcmp(s->name, "audio")) return AUDIO;
    if (f->path == AUDIO && !strcmp(s->name, "data")) return AUDIO_DATA;
    return OTHER;
}

static void complete_value(xigua_tts_stream_t *s)
{
    if (!s->depth) s->root_complete = true;
    else {
        xigua_tts_frame_t *f = &s->frames[s->depth - 1];
        f->state = COMMA_OR_END;
        if (f->kind == ARRAY) ++f->index;
    }
}

static bool literal_valid(const char *p)
{
    if (!strcmp(p, "null") || !strcmp(p, "true") || !strcmp(p, "false")) return true;
    if (*p == '-') ++p;
    if (*p == '0') ++p;
    else {
        if (*p < '1' || *p > '9') return false;
        do { ++p; } while (*p >= '0' && *p <= '9');
    }
    if (*p == '.') {
        ++p;
        if (*p < '0' || *p > '9') return false;
        do { ++p; } while (*p >= '0' && *p <= '9');
    }
    if (*p == 'e' || *p == 'E') {
        ++p;
        if (*p == '+' || *p == '-') ++p;
        if (*p < '0' || *p > '9') return false;
        do { ++p; } while (*p >= '0' && *p <= '9');
    }
    return !*p;
}

static bool json_char(xigua_tts_stream_t *s, char c)
{
    if (s->token == STRING) {
        if (s->unicode_left) {
            if (!hex(c)) return false;
            --s->unicode_left;
            /* Escaped property names cannot select the audio path. */
            if (s->key && s->name_length + 1 < sizeof(s->name)) s->name[s->name_length++] = '?';
            return true;
        }
        if (s->escape) {
            s->escape = 0;
            if (c == 'u') { if (s->audio) return false; s->unicode_left = 4; return true; }
            if (!strchr("\"\\/bfnrt", c)) return false;
            if (s->audio) return c == '/' && audio_char(s, c);
            if (s->key && s->name_length + 1 < sizeof(s->name)) s->name[s->name_length++] = c;
            return true;
        }
        if (c == '\\') { s->escape = 1; return true; }
        if (c == '"') {
            s->token = NO_TOKEN;
            if (s->key) {
                s->name[s->name_length] = 0;
                s->frames[s->depth - 1].state = COLON;
            } else {
                if (s->audio) {
                    if (s->quartet_length || (s->audio_bytes & 1) || !flush(s)) return false;
                    if (s->audio_bytes) ++s->chunks;
                }
                complete_value(s);
            }
            return true;
        }
        if ((unsigned char)c < 32) return false;
        if (s->audio) return audio_char(s, c);
        if (s->key) {
            if (s->name_length + 1 < sizeof(s->name)) s->name[s->name_length++] = c;
            else s->name[0] = '?';
        }
        return true;
    }
    if (s->token == LITERAL) {
        if (!whitespace(c) && c != ',' && c != ']' && c != '}') {
            if (s->literal_length + 1 >= sizeof(s->literal)) return false;
            s->literal[s->literal_length++] = c;
            return true;
        }
        s->literal[s->literal_length] = 0;
        if (!literal_valid(s->literal)) return false;
        s->token = NO_TOKEN;
        complete_value(s);
    }
    if (whitespace(c)) return true;
    if (s->root_complete) return false;
    xigua_tts_frame_t *f = s->depth ? &s->frames[s->depth - 1] : NULL;
    if (f && f->state == COLON) { if (c != ':') return false; f->state = VALUE; return true; }
    if (f && f->state == COMMA_OR_END && c == ',') {
        f->state = f->kind == OBJECT ? KEY : VALUE;
        return true;
    }
    if (c == '}' || c == ']') {
        if (!f || (c == '}' ? f->kind != OBJECT : f->kind != ARRAY) ||
            (f->state != COMMA_OR_END && f->state != KEY_OR_END && f->state != VALUE_OR_END)) return false;
        --s->depth;
        complete_value(s);
        return true;
    }
    if (f && (f->state == KEY || f->state == KEY_OR_END)) {
        if (c != '"') return false;
        s->key = true; s->audio = false; s->name_length = 0; s->token = STRING;
        return true;
    }
    if (f && f->state != VALUE && f->state != VALUE_OR_END) return false;
    unsigned path = value_path(s);
    if (c == '{' || c == '[') {
        if (s->depth == sizeof(s->frames) / sizeof(s->frames[0])) return false;
        if (!f && c != '{') return false;
        s->frames[s->depth++] = (xigua_tts_frame_t){
            .kind = c == '{' ? OBJECT : ARRAY,
            .state = c == '{' ? KEY_OR_END : VALUE_OR_END,
            .path = (uint8_t)path,
        };
        return true;
    }
    if (!f) return false;
    if (c == '"') {
        s->key = false; s->audio = path == AUDIO_DATA; s->token = STRING;
        s->padded = false; s->quartet_length = 0; s->audio_bytes = 0;
        return true;
    }
    s->token = LITERAL; s->literal_length = 1; s->literal[0] = c;
    return true;
}

static bool line_end(xigua_tts_stream_t *s)
{
    if (s->mode == JSON && (!s->root_complete || s->depth || s->token)) return false;
    if (s->mode == DONE_TOKEN) {
        s->prefix[s->prefix_length] = 0;
        if (strcmp(s->prefix, "[DONE]")) return false;
        s->done = true;
    }
    s->mode = PREFIX; s->prefix_length = 0; s->root_complete = false;
    return true;
}

void xigua_tts_stream_init(xigua_tts_stream_t *s, xigua_tts_sink_t sink, void *context)
{
    memset(s, 0, sizeof(*s));
    s->sink = sink; s->context = context;
}

bool xigua_tts_stream_feed(xigua_tts_stream_t *s, const char *data, size_t bytes)
{
    if (s->failed || !s->sink) return false;
    for (size_t i = 0; i < bytes && !s->done; ++i) {
        char c = data[i];
        bool ok = true;
        if (c == '\n') ok = line_end(s);
        else if (s->mode == PREFIX) {
            if (!s->prefix_length && whitespace(c)) continue;
            const char *prefix = "data:";
            if (c != prefix[s->prefix_length]) s->mode = IGNORE;
            else if (++s->prefix_length == 5) { s->mode = JSON; s->prefix_length = 0; }
        } else if (s->mode == JSON) {
            if (!s->depth && !s->root_complete && s->token == NO_TOKEN && c == '[') {
                s->mode = DONE_TOKEN; s->prefix[0] = c; s->prefix_length = 1;
            } else ok = json_char(s, c);
        } else if (s->mode == DONE_TOKEN && !whitespace(c)) {
            if (s->prefix_length + 1 >= sizeof(s->prefix)) ok = false;
            else s->prefix[s->prefix_length++] = c;
        }
        if (!ok) { s->failed = true; return false; }
    }
    return true;
}

bool xigua_tts_stream_finish(xigua_tts_stream_t *s)
{
    if (!s->failed && !s->done && !line_end(s)) s->failed = true;
    return !s->failed && s->done && s->bytes > 0;
}
