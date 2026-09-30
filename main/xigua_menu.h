#pragma once

#include <stdbool.h>
#include <stddef.h>

static inline size_t xigua_menu_move(size_t focus, size_t count, bool next)
{
    if (!count) return 0;
    focus %= count;
    return next ? (focus + 1) % count : (focus + count - 1) % count;
}

static inline size_t xigua_menu_first(size_t focus, size_t visible)
{
    return visible ? focus / visible * visible : 0;
}
