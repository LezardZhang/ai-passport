#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

/* Input is valid UTF-8. Truncation never leaves a partial codepoint. */
static inline bool xigua_text_copy(char *dest, size_t capacity, const char *source)
{
    size_t length = strlen(source);
    if (!capacity) return length != 0;
    size_t copied = length < capacity ? length : capacity - 1;
    if (copied < length) {
        while (copied && ((unsigned char)source[copied] & 0xc0) == 0x80) --copied;
    }
    memcpy(dest, source, copied);
    dest[copied] = '\0';
    return copied < length;
}
