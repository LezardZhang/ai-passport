#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* Five rows by five columns. Four rows show characters; the final row switches
 * character sets, deletes, or accepts the edit. */
#define XIGUA_KEYBOARD_CHAR_KEY_COUNT 20u
#define XIGUA_KEYBOARD_KEY_COUNT 25u
#define XIGUA_KEYBOARD_MODE_COUNT 3u
#define XIGUA_KEYBOARD_MAX_PAGE_COUNT 3u
#define XIGUA_KEYBOARD_TEXT_CAPACITY 65u

typedef enum {
    XIGUA_KEYBOARD_KEY_LOWER = XIGUA_KEYBOARD_CHAR_KEY_COUNT,
    XIGUA_KEYBOARD_KEY_UPPER,
    XIGUA_KEYBOARD_KEY_SYMBOLS,
    XIGUA_KEYBOARD_KEY_DEL,
    XIGUA_KEYBOARD_KEY_DONE,
} xigua_keyboard_key_t;

typedef enum {
    XIGUA_KEYBOARD_MODE_LOWER = 0,
    XIGUA_KEYBOARD_MODE_UPPER,
    XIGUA_KEYBOARD_MODE_SYMBOLS,
} xigua_keyboard_mode_t;

typedef enum {
    XIGUA_KEYBOARD_NOOP = 0,
    XIGUA_KEYBOARD_EDITED,
    XIGUA_KEYBOARD_MODE_CHANGED,
    XIGUA_KEYBOARD_PAGE_CHANGED,
    XIGUA_KEYBOARD_DONE,
    XIGUA_KEYBOARD_CANCEL,
    XIGUA_KEYBOARD_FULL,
} xigua_keyboard_result_t;

typedef struct {
    /* max_bytes includes the trailing NUL; supported range is 1..65. */
    char text[XIGUA_KEYBOARD_TEXT_CAPACITY];
    size_t max_bytes;
    uint8_t mode;
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

/* Switches character set and returns to its first page. */
bool xigua_keyboard_set_mode(xigua_keyboard_t *keyboard,
                             xigua_keyboard_mode_t mode);

/* Advances cyclically through the current character set's pages. */
void xigua_keyboard_next_page(xigua_keyboard_t *keyboard);

/* Cancels the edit. Long DOWN uses this without occupying a visible key. */
bool xigua_keyboard_cancel(xigua_keyboard_t *keyboard);

/* Applies the selected key and returns the resulting state change. */
xigua_keyboard_result_t xigua_keyboard_press(xigua_keyboard_t *keyboard);

/* Returns a static character/control label or "" for an unused cell. */
const char *xigua_keyboard_label(const xigua_keyboard_t *keyboard, uint8_t index);

/* False only for unused character cells on a short final page. */
bool xigua_keyboard_key_enabled(const xigua_keyboard_t *keyboard, uint8_t index);

/* Number of character pages in the active set. */
uint8_t xigua_keyboard_page_count(const xigua_keyboard_t *keyboard);

/* Returns the active set's display name. */
const char *xigua_keyboard_mode_name(const xigua_keyboard_t *keyboard);

