#include "xigua_keyboard.h"

#include <string.h>

/* Printable non-space ASCII grouped as letters, digits, then punctuation. */
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

static const char s_space_label[] = "SPC";
static const char *const s_control_labels[5] = {
    "小写", "大写", "数符", "退格", "确认"
};
static const char *const s_mode_names[XIGUA_KEYBOARD_MODE_COUNT] = {
    "小写", "大写", "数字符号"
};

static uint8_t character_count(xigua_keyboard_mode_t mode)
{
    switch (mode) {
    case XIGUA_KEYBOARD_MODE_LOWER:
    case XIGUA_KEYBOARD_MODE_UPPER:
        return 27u; /* 26 letters plus space. */
    case XIGUA_KEYBOARD_MODE_SYMBOLS:
        return 43u; /* Digits, punctuation, then space. */
    default:
        return 0u;
    }
}

static const char *character_label(xigua_keyboard_mode_t mode, uint8_t index)
{
    uint8_t count = character_count(mode);
    if (index >= count) return "";
    if (index == count - 1u) return s_space_label;
    uint8_t base = 0u;
    if (mode == XIGUA_KEYBOARD_MODE_UPPER) base = 26u;
    else if (mode == XIGUA_KEYBOARD_MODE_SYMBOLS) base = 52u;
    return s_char_labels[(size_t)base + index];
}

static char character_value(xigua_keyboard_mode_t mode, uint8_t index)
{
    const char *label = character_label(mode, index);
    return label == s_space_label ? ' ' : label[0];
}

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
    const size_t character_index =
        (size_t)keyboard->page * XIGUA_KEYBOARD_CHAR_KEY_COUNT + index;
    return character_index < character_count((xigua_keyboard_mode_t)keyboard->mode);
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

bool xigua_keyboard_set_mode(xigua_keyboard_t *keyboard,
                             xigua_keyboard_mode_t mode)
{
    if (!keyboard || keyboard->finished || keyboard->cancelled ||
        mode >= XIGUA_KEYBOARD_MODE_COUNT) return false;
    keyboard->mode = (uint8_t)mode;
    keyboard->page = 0;
    return true;
}

void xigua_keyboard_next_page(xigua_keyboard_t *keyboard)
{
    if (!keyboard || keyboard->finished || keyboard->cancelled) return;
    uint8_t pages = xigua_keyboard_page_count(keyboard);
    if (pages == 0) return;
    keyboard->page = (uint8_t)((keyboard->page + 1u) % pages);
    if (!xigua_keyboard_key_enabled(keyboard, keyboard->selection)) {
        keyboard->selection = enabled_at_or_after(keyboard, 0);
    }
}

bool xigua_keyboard_cancel(xigua_keyboard_t *keyboard)
{
    if (!keyboard || keyboard->finished || keyboard->cancelled) return false;
    keyboard->cancelled = true;
    return true;
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
        uint8_t index =
            (uint8_t)(keyboard->page * XIGUA_KEYBOARD_CHAR_KEY_COUNT + selected);
        return insert_byte(keyboard,
                           character_value((xigua_keyboard_mode_t)keyboard->mode, index));
    }
    switch (selected) {
    case XIGUA_KEYBOARD_KEY_LOWER:
        xigua_keyboard_set_mode(keyboard, XIGUA_KEYBOARD_MODE_LOWER);
        return XIGUA_KEYBOARD_MODE_CHANGED;
    case XIGUA_KEYBOARD_KEY_UPPER:
        xigua_keyboard_set_mode(keyboard, XIGUA_KEYBOARD_MODE_UPPER);
        return XIGUA_KEYBOARD_MODE_CHANGED;
    case XIGUA_KEYBOARD_KEY_SYMBOLS:
        xigua_keyboard_set_mode(keyboard, XIGUA_KEYBOARD_MODE_SYMBOLS);
        return XIGUA_KEYBOARD_MODE_CHANGED;
    case XIGUA_KEYBOARD_KEY_DEL:
        return delete_codepoint(keyboard);
    case XIGUA_KEYBOARD_KEY_DONE:
        keyboard->finished = true;
        return XIGUA_KEYBOARD_DONE;
    default:
        return XIGUA_KEYBOARD_NOOP;
    }
}

const char *xigua_keyboard_label(const xigua_keyboard_t *keyboard, uint8_t index)
{
    if (!xigua_keyboard_key_enabled(keyboard, index)) return "";
    if (index < XIGUA_KEYBOARD_CHAR_KEY_COUNT) {
        uint8_t character_index =
            (uint8_t)(keyboard->page * XIGUA_KEYBOARD_CHAR_KEY_COUNT + index);
        return character_label((xigua_keyboard_mode_t)keyboard->mode, character_index);
    }
    return s_control_labels[index - XIGUA_KEYBOARD_CHAR_KEY_COUNT];
}

uint8_t xigua_keyboard_page_count(const xigua_keyboard_t *keyboard)
{
    if (!keyboard || keyboard->mode >= XIGUA_KEYBOARD_MODE_COUNT) return 0;
    uint8_t count = character_count((xigua_keyboard_mode_t)keyboard->mode);
    return (uint8_t)((count + XIGUA_KEYBOARD_CHAR_KEY_COUNT - 1u) /
                     XIGUA_KEYBOARD_CHAR_KEY_COUNT);
}

const char *xigua_keyboard_mode_name(const xigua_keyboard_t *keyboard)
{
    if (!keyboard || keyboard->mode >= XIGUA_KEYBOARD_MODE_COUNT) return "";
    return s_mode_names[keyboard->mode];
}
