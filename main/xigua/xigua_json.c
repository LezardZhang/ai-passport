#include "xigua_json.h"
#include <string.h>

/* Reject overlong UTF-8, surrogates, and code points beyond U+10FFFF. */
size_t xigua_utf8_width(const unsigned char *s, size_t left)
{
    if (!left) return 0;
    unsigned c = s[0];
    if (c < 0x80) return 1;
    size_t n = c >= 0xc2 && c <= 0xdf ? 2 :
               c >= 0xe0 && c <= 0xef ? 3 :
               c >= 0xf0 && c <= 0xf4 ? 4 : 0;
    if (!n || left < n) return 0;
    for (size_t i = 1; i < n; ++i)
        if ((s[i] & 0xc0) != 0x80) return 0;
    if ((c == 0xe0 && s[1] < 0xa0) || (c == 0xed && s[1] >= 0xa0) ||
        (c == 0xf0 && s[1] < 0x90) || (c == 0xf4 && s[1] >= 0x90)) return 0;
    return n;
}

bool xigua_utf8_valid(const char *s, size_t n)
{
    for (size_t i = 0; i < n;) {
        size_t width = xigua_utf8_width((const unsigned char *)s + i, n - i);
        if (!width || !s[i]) return false;
        i += width;
    }
    return true;
}

/* cJSON accepts some non-JSON number forms. Validate syntax before allocating
 * its tree and cap recursion/nodes, keeping untrusted JSON within the RAM budget.
 * Embedded NUL escapes are rejected because cJSON strings are NUL terminated.
 */
typedef struct { const char *p, *end; unsigned nodes; } scan_t;
static void whitespace(scan_t *s)
{
    while (s->p < s->end && (*s->p == ' ' || *s->p == '\t' ||
           *s->p == '\r' || *s->p == '\n')) ++s->p;
}
static bool hex_digit(char c)
{
    return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') ||
           (c >= 'A' && c <= 'F');
}
static bool string_token(scan_t *s)
{
    if (s->p == s->end || *s->p++ != '"') return false;
    while (s->p < s->end) {
        unsigned char c = (unsigned char)*s->p++;
        if (c == '"') return true;
        if (c < 0x20) return false;
        if (c == '\\') {
            if (s->p == s->end) return false;
            char escape = *s->p++;
            if (escape == 'u') {
                if (s->end - s->p < 4) return false;
                for (int i = 0; i < 4; ++i) if (!hex_digit(s->p[i])) return false;
                if (memcmp(s->p, "0000", 4) == 0) return false;
                s->p += 4;
            } else if (!strchr("\"\\/bfnrt", escape)) return false;
        }
    }
    return false;
}
static bool digit(char c) { return c >= '0' && c <= '9'; }
static bool value_token(scan_t *s, unsigned depth)
{
    whitespace(s);
    if (s->p == s->end || depth > 12 || ++s->nodes > 256) return false;
    char c = *s->p;
    if (c == '"') return string_token(s);
    if (c == '{' || c == '[') {
        char closing = c == '{' ? '}' : ']';
        ++s->p;
        whitespace(s);
        if (s->p < s->end && *s->p == closing) { ++s->p; return true; }
        for (;;) {
            if (c == '{') {
                if (!string_token(s)) return false;
                whitespace(s);
                if (s->p == s->end || *s->p++ != ':') return false;
            }
            if (!value_token(s, depth + 1)) return false;
            whitespace(s);
            if (s->p == s->end) return false;
            if (*s->p == closing) { ++s->p; return true; }
            if (*s->p++ != ',') return false;
            whitespace(s);
        }
    }
    const char *literal = c == 't' ? "true" : c == 'f' ? "false" : c == 'n' ? "null" : NULL;
    if (literal) {
        size_t n = strlen(literal);
        if ((size_t)(s->end - s->p) < n || memcmp(s->p, literal, n)) return false;
        s->p += n;
        return true;
    }
    if (c == '-') ++s->p;
    if (s->p == s->end || !digit(*s->p)) return false;
    if (*s->p == '0') ++s->p;
    else while (s->p < s->end && digit(*s->p)) ++s->p;
    if (s->p < s->end && *s->p == '.') {
        ++s->p;
        if (s->p == s->end || !digit(*s->p)) return false;
        while (s->p < s->end && digit(*s->p)) ++s->p;
    }
    if (s->p < s->end && (*s->p == 'e' || *s->p == 'E')) {
        ++s->p;
        if (s->p < s->end && (*s->p == '+' || *s->p == '-')) ++s->p;
        if (s->p == s->end || !digit(*s->p)) return false;
        while (s->p < s->end && digit(*s->p)) ++s->p;
    }
    return true;
}

bool xigua_json_valid(const char *s, size_t n)
{
    if (!s || !n || !xigua_utf8_valid(s, n)) return false;
    scan_t scan = {s, s + n, 0};
    if (!value_token(&scan, 0)) return false;
    whitespace(&scan);
    return scan.p == scan.end;
}
