#pragma once
#include <stdbool.h>
#include <stdint.h>

/* Pure gesture reducer. Hardware click/double/long notifications are ignored. */
typedef struct {
    int active_key;
    uint64_t pressed_ms;
    bool consumed;
    bool long_sent;
} xigua_input_t;
typedef enum { XG_INPUT_NONE, XG_INPUT_WAKE, XG_INPUT_CLICK, XG_INPUT_LONG } xigua_input_action_t;
void xigua_input_init(xigua_input_t *input);
xigua_input_action_t xigua_input_press(xigua_input_t *input, int key,
                                      uint64_t now_ms, bool screen_off);
xigua_input_action_t xigua_input_release(xigua_input_t *input, int key, uint64_t now_ms);
xigua_input_action_t xigua_input_tick(xigua_input_t *input, uint64_t now_ms, int *key);
