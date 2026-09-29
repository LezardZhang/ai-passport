#ifndef XIGUA_JSON_H
#define XIGUA_JSON_H
#include <stdbool.h>
#include <stddef.h>

/* Strict syntax before cJSON allocation: depth <= 12, values <= 256,
 * valid UTF-8, no embedded NUL, no trailing garbage or permissive numbers.
 * The caller bounds total bytes and checks duplicate keys/schema afterward.
 */
bool xigua_json_valid(const char *s, size_t length);
bool xigua_utf8_valid(const char *s, size_t length);
size_t xigua_utf8_width(const unsigned char *s, size_t remaining);
#endif
