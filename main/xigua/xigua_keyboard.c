#include "xigua_keyboard.h"

#include <string.h>

/* Common password characters come first; all remaining printable non-space
 * ASCII characters follow, so the five pages cover the complete set. */
static const char *const s_char_labels[94] = {
    "a", "b", "c", "d", "e", "f", "g", "h", "i", "j",
    "k", "l", "m", "n", "o", "p", "q", "r", "s", "t",
    "u", "v", "w", "x", "y", "z",
    "A", "B", "C", "D", "E", "F", "G", "H", "I", "J",
    "K", "L", "M", "N", "O", "P", "Q", "R", "S", "T",
    "U", "V", "W", "X", "Y", "Z",
    "0", "1", "2", "3", "4", "5", "6", "7", "8", "9",
    "!", "\"", "#", "$", "%", "&", "'", "(", ")", "*",
    "+", ",", "-", ".", "/", ":", ";", "<", "=", ">",
    "?", "@", "[", "\\", "]", "^", "_", "`", "{", "|",
    "}", "~",
};

static const char *const s_control_labels[5] = { "DEL", "SPC", "PG", "DON", "ESC" };

static size_t utf8_codepoint_bytes(unsigned char lead)
{
    if (lead < 0x80) return 1;
    if ((lead & 0xE0) == 0xC0) return 2;
    if ((lead & 0xF0) == 0xE0) return 3;
    if ((lead & 0xF8) == 0xF0) return 4;
    return 1;
}

static size_t truncate_utf8(const char *text, size_t limit)
{
    size_t used = 0;
    while (text[used] != '\0' && used < limit) {
        size_t count = utf8_codepoint_bytes((unsigned char)text[used]);
        if (used + count > limit) break;
        bool valid = true;
        for (size_t i = 1; i < count; ++i) {
            unsigned char byte = (unsigned char)text[used + i];
            if (byte == '\0' || (byte & 0xC0) != 0x80) {
                valid = false;
                break;
            }
        }
        if (!valid) count = 1; /* Preserve malformed input without overrunning. */
        used += count;
    }
    return used;
}

bool xigua_keyboard_init(xigua_keyboard_t *keyboard, const char *initial,
                        size_t max_bytes)
{
    if (!keyboard || max_bytes == 0 || max_bytes > XIGUA_KEYBOARD_TEXT_CAPACITY) return false;
    memset(keyboard, 0, sizeof(*keyboard));
    keyboard->max_bytes = max_bytes;
    if (initial) {
        size_t bytes = truncate_utf8(initial, max_bytes - 1);
        memcpy(keyboard->text, initial, bytes);
        keyboard->text[bytes] = '\0';
    }
    keyboard->selection = 0;
    return true;
}

bool xigua_keyboard_key_enabled(const xigua_keyboard_t *keyboard, uint8_t index)
{
    if (!keyboard || index >= XIGUA_KEYBOARD_KEY_COUNT) return false;
    if (index >= XIGUA_KEYBOARD_CHAR_KEY_COUNT) return true;
    const size_t character_index = (size_t)keyboard->page * XIGUA_KEYBOARD_CHAR_KEY_COUNT + index;
    return character_index < 94;
}

static uint8_t enabled_at_or_after(const xigua_keyboard_t *keyboard, uint8_t start)
{
    for (uint8_t i = start; i < XIGUA_KEYBOARD_KEY_COUNT; ++i) {
        if (xigua_keyboard_key_enabled(keyboard, i)) return i;
    }
    for (uint8_t i = 0; i < start; ++i) {
        if (xigua_keyboard_key_enabled(keyboard, i)) return i;
    }
    return 0;
}

void xigua_keyboard_move(xigua_keyboard_t *keyboard, int delta)
{
    if (!keyboard || delta == 0) return;
    int step = delta > 0 ? 1 : -1;
    unsigned remaining = (unsigned)(delta > 0 ? delta : -((int64_t)delta));
    uint8_t cursor = keyboard->selection < XIGUA_KEYBOARD_KEY_COUNT ? keyboard->selection : 0;
    while (remaining-- > 0) {
        for (uint8_t attempts = 0; attempts < XIGUA_KEYBOARD_KEY_COUNT; ++attempts) {
            int candidate = (int)cursor + step;
            if (candidate < 0) candidate = XIGUA_KEYBOARD_KEY_COUNT - 1;
            if (candidate >= (int)XIGUA_KEYBOARD_KEY_COUNT) candidate = 0;
            cursor = (uint8_t)candidate;
            if (xigua_keyboard_key_enabled(keyboard, cursor)) break;
        }
    }
    keyboard->selection = cursor;
}

void xigua_keyboard_next_page(xigua_keyboard_t *keyboard)
{
    if (!keyboard) return;
    keyboard->page = (uint8_t)((keyboard->page + 1u) % XIGUA_KEYBOARD_PAGE_COUNT);
    if (!xigua_keyboard_key_enabled(keyboard, keyboard->selection)) {
        keyboard->selection = enabled_at_or_after(keyboard, 0);
    }
}

static xigua_keyboard_result_t insert_byte(xigua_keyboard_t *keyboard, char value)
{
    size_t length = strlen(keyboard->text);
    if (length + 1 >= keyboard->max_bytes) return XIGUA_KEYBOARD_FULL;
    keyboard->text[length] = value;
    keyboard->text[length + 1] = '\0';
    return XIGUA_KEYBOARD_EDITED;
}

static xigua_keyboard_result_t delete_codepoint(xigua_keyboard_t *keyboard)
{
    size_t length = strlen(keyboard->text);
    if (length == 0) return XIGUA_KEYBOARD_NOOP;
    size_t start = length - 1;
    while (start > 0 && (((unsigned char)keyboard->text[start] & 0xC0) == 0x80)) --start;
    keyboard->text[start] = '\0';
    return XIGUA_KEYBOARD_EDITED;
}

xigua_keyboard_result_t xigua_keyboard_press(xigua_keyboard_t *keyboard)
{
    if (!keyboard || keyboard->finished || keyboard->cancelled ||
        keyboard->selection >= XIGUA_KEYBOARD_KEY_COUNT) return XIGUA_KEYBOARD_NOOP;
    const uint8_t selected = keyboard->selection;
    if (!xigua_keyboard_key_enabled(keyboard, selected)) return XIGUA_KEYBOARD_NOOP;
    if (selected < XIGUA_KEYBOARD_CHAR_KEY_COUNT) {
        size_t index = (size_t)keyboard->page * XIGUA_KEYBOARD_CHAR_KEY_COUNT + selected;
        return insert_byte(keyboard, s_char_labels[index][0]);
    }
    switch (selected) {
    case XIGUA_KEYBOARD_KEY_DEL:
        return delete_codepoint(keyboard);
    case XIGUA_KEYBOARD_KEY_SPACE:
        return insert_byte(keyboard, ' ');
    case XIGUA_KEYBOARD_KEY_PAGE:
        xigua_keyboard_next_page(keyboard);
        return XIGUA_KEYBOARD_PAGE_CHANGED;
    case XIGUA_KEYBOARD_KEY_DONE:
        keyboard->finished = true;
        return XIGUA_KEYBOARD_DONE;
    case XIGUA_KEYBOARD_KEY_CANCEL:
        keyboard->cancelled = true;
        return XIGUA_KEYBOARD_CANCEL;
    default:
        return XIGUA_KEYBOARD_NOOP;
    }
}

const char *xigua_keyboard_label(const xigua_keyboard_t *keyboard, uint8_t index)
{
    if (!xigua_keyboard_key_enabled(keyboard, index)) return "";
    if (index < XIGUA_KEYBOARD_CHAR_KEY_COUNT) {
        size_t character_index = (size_t)keyboard->page * XIGUA_KEYBOARD_CHAR_KEY_COUNT + index;
        return s_char_labels[character_index];
    }
    return s_control_labels[index - XIGUA_KEYBOARD_CHAR_KEY_COUNT];
}
