#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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

/* The server may stop at its token limit before the local buffer is full. */
static inline bool xigua_text_reply_copy(char *dest, size_t capacity, const char *source,
                                         const char *finish_reason)
{
    bool clipped = xigua_text_copy(dest, capacity, source);
    return clipped || (finish_reason && strcmp(finish_reason, "length") == 0);
}

/* Compact model prose in place after command parsing, never before JSON validation. */
static inline void xigua_text_plain_reply(char *text)
{
    const unsigned char *read = (const unsigned char *)text;
    char *write = text;
    bool line_start = true;
    while (*read) {
        if (line_start) {
            while (*read == ' ' || *read == '\t') ++read;
            if ((*read == '#' || *read == '>' || *read == '-' || *read == '+' ||
                 *read == '*') && (read[1] == ' ' || read[1] == '#' || read[1] == '*')) {
                while (*read == '#' || *read == '>' || *read == '-' || *read == '+' ||
                       *read == '*' || *read == ' ') ++read;
            }
            const unsigned char *number = read;
            while (*number >= '0' && *number <= '9') ++number;
            if (number > read && (*number == '.' || *number == ')') && number[1] == ' ')
                read = number + 2;
        }
        unsigned char first = *read;
        if (!first) break;
        if (first == '\r') { ++read; continue; }
        if (first == '\n') {
            while (write > text && write[-1] == ' ') --write;
            if (write > text && write[-1] != '\n') *write++ = '\n';
            ++read; line_start = true; continue;
        }
        if (first == '*' || first == '`') { ++read; continue; }
        if (first == '_' && read[1] == '_') { read += 2; continue; }
        size_t bytes = first < 0x80 ? 1 : first < 0xe0 ? 2 : first < 0xf0 ? 3 : 4;
        uint32_t point = first & (bytes == 1 ? 0x7f : bytes == 2 ? 0x1f : bytes == 3 ? 0x0f : 0x07);
        for (size_t i = 1; i < bytes; ++i) point = (point << 6) | (read[i] & 0x3f);
        bool decoration = (point >= 0x1f000 && point <= 0x1faff) ||
                          (point >= 0x2600 && point <= 0x27bf) || point == 0xfe0f ||
                          point == 0x200d || point == 0x20e3 || point == 0x2022;
        if (!decoration) {
            if (first == '\t') *write++ = ' ';
            else { memmove(write, read, bytes); write += bytes; }
            line_start = false;
        }
        read += bytes;
    }
    while (write > text && (write[-1] == '\n' || write[-1] == ' ')) --write;
    *write = '\0';
}
