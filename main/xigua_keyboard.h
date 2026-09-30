#pragma once

#include <stddef.h>

#define XIGUA_KEYBOARD_COLUMNS 6u
#define XIGUA_KEYBOARD_PAGE_KEYS 30u
#define XIGUA_KEYBOARD_ACTIONS 6u

typedef struct {
    size_t anchor, previous_anchor;
    size_t page, previous_page;
    int direction, previous_direction;
} xigua_keyboard_input_t;

static inline size_t xigua_keyboard_step(size_t index, size_t count, int direction)
{
    return direction < 0 ? (index + count - 1) % count : (index + 1) % count;
}

static inline size_t xigua_keyboard_page_count(size_t characters)
{
    return (characters + XIGUA_KEYBOARD_PAGE_KEYS - 1) / XIGUA_KEYBOARD_PAGE_KEYS;
}

/* Move between populated character rows and the two permanently visible action
 * rows. A partial last row clamps the column; empty rows are skipped. */
static inline size_t xigua_keyboard_row(size_t index, size_t characters,
                                       size_t page, int direction)
{
    size_t start = page * XIGUA_KEYBOARD_PAGE_KEYS;
    size_t visible = characters - start;
    if (visible > XIGUA_KEYBOARD_PAGE_KEYS) visible = XIGUA_KEYBOARD_PAGE_KEYS;
    size_t rows = (visible + XIGUA_KEYBOARD_COLUMNS - 1) / XIGUA_KEYBOARD_COLUMNS;
    size_t row, column;
    if (index < characters) {
        row = (index - start) / XIGUA_KEYBOARD_COLUMNS;
        column = (index - start) % XIGUA_KEYBOARD_COLUMNS;
    } else {
        row = rows + (index - characters) / 3;
        column = (index - characters) % 3;
    }
    size_t next = xigua_keyboard_step(row, rows + 2, direction);
    if (row < rows && next >= rows) column /= 2;
    else if (row >= rows && next < rows) column *= 2;
    if (next >= rows) return characters + (next - rows) * 3 + column;
    size_t offset = next * XIGUA_KEYBOARD_COLUMNS;
    size_t width = visible - offset;
    if (width > XIGUA_KEYBOARD_COLUMNS) width = XIGUA_KEYBOARD_COLUMNS;
    if (column >= width) column = width - 1;
    return start + offset + column;
}

/* Every PRESS moves immediately, including a third or fourth rapid press for
 * which the button driver emits no single/double-click callback. DOUBLE replaces
 * the two preliminary steps with one row move from the gesture's starting key. */
static inline size_t xigua_keyboard_press(xigua_keyboard_input_t *input,
                                         size_t index, size_t characters,
                                         size_t *page, int direction)
{
    input->previous_anchor = input->anchor;
    input->previous_page = input->page;
    input->previous_direction = input->direction;
    input->anchor = index;
    input->page = *page;
    input->direction = direction;
    index = xigua_keyboard_step(index, characters + XIGUA_KEYBOARD_ACTIONS, direction);
    if (index < characters) *page = index / XIGUA_KEYBOARD_PAGE_KEYS;
    return index;
}

static inline size_t xigua_keyboard_double(xigua_keyboard_input_t *input,
                                          size_t index, size_t characters,
                                          size_t *page, int direction)
{
    if (input->direction == direction && input->previous_direction == direction) {
        *page = input->previous_page;
        index = xigua_keyboard_row(input->previous_anchor, characters, *page, direction);
    }
    input->direction = input->previous_direction = 0;
    return index;
}
