#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Five rows by five columns. The final page has disabled character slots. */
#define XIGUA_KEYBOARD_CHAR_KEY_COUNT 20u
#define XIGUA_KEYBOARD_KEY_COUNT 25u
#define XIGUA_KEYBOARD_PAGE_COUNT 5u
#define XIGUA_KEYBOARD_TEXT_CAPACITY 65u

typedef enum {
    XIGUA_KEYBOARD_KEY_DEL = XIGUA_KEYBOARD_CHAR_KEY_COUNT,
    XIGUA_KEYBOARD_KEY_SPACE,
    XIGUA_KEYBOARD_KEY_PAGE,
    XIGUA_KEYBOARD_KEY_DONE,
    XIGUA_KEYBOARD_KEY_CANCEL,
} xigua_keyboard_key_t;

typedef enum {
    XIGUA_KEYBOARD_NOOP = 0,
    XIGUA_KEYBOARD_EDITED,
    XIGUA_KEYBOARD_PAGE_CHANGED,
    XIGUA_KEYBOARD_DONE,
    XIGUA_KEYBOARD_CANCEL,
    XIGUA_KEYBOARD_FULL,
} xigua_keyboard_result_t;

typedef struct {
    /* max_bytes includes the trailing NUL; supported range is 1..65. */
    char text[XIGUA_KEYBOARD_TEXT_CAPACITY];
    size_t max_bytes;
    uint8_t page;
    uint8_t selection;
    bool finished;
    bool cancelled;
} xigua_keyboard_t;

/* Copies initial text at complete UTF-8 code-point boundaries. Returns false
 * for a null state or a max_bytes value outside 1..65. */
bool xigua_keyboard_init(xigua_keyboard_t *keyboard, const char *initial,
                        size_t max_bytes);

/* Moves cyclically through enabled cells; negative delta moves backward. */
void xigua_keyboard_move(xigua_keyboard_t *keyboard, int delta);

/* Advances cyclically through the five ASCII character pages. */
void xigua_keyboard_next_page(xigua_keyboard_t *keyboard);

/* Applies the selected key and returns the resulting state change. */
xigua_keyboard_result_t xigua_keyboard_press(xigua_keyboard_t *keyboard);

/* Returns a static one-character label, a short control label, or "". */
const char *xigua_keyboard_label(const xigua_keyboard_t *keyboard, uint8_t index);

/* False only for the unused cells after ASCII '~' on the last page. */
bool xigua_keyboard_key_enabled(const xigua_keyboard_t *keyboard, uint8_t index);

