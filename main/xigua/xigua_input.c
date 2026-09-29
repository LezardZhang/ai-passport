#include "xigua_input.h"
#include <stddef.h>

#define HOLD_MS 600u
#define STUCK_MS 15000u

void xigua_input_init(xigua_input_t *input)
{
    *input = (xigua_input_t){ .active_key = -1 };
}

xigua_input_action_t xigua_input_press(xigua_input_t *input, int key,
                                      uint64_t now_ms, bool screen_off)
{
    if (key < 0 || key > 2) return XG_INPUT_NONE;
    if (input->active_key >= 0) {
        /* Shared ADC ladder has no multi-key contract: cancel an overlap. */
        input->consumed = true;
        return XG_INPUT_NONE;
    }
    *input = (xigua_input_t){ key, now_ms, screen_off, false };
    return screen_off ? XG_INPUT_WAKE : XG_INPUT_NONE;
}

xigua_input_action_t xigua_input_release(xigua_input_t *input, int key, uint64_t now_ms)
{
    if (key != input->active_key) return XG_INPUT_NONE;
    xigua_input_action_t action = XG_INPUT_NONE;
    if (!input->consumed && !input->long_sent && now_ms >= input->pressed_ms) {
        action = now_ms - input->pressed_ms >= HOLD_MS ? XG_INPUT_LONG : XG_INPUT_CLICK;
    }
    xigua_input_init(input);
    return action;
}

xigua_input_action_t xigua_input_tick(xigua_input_t *input, uint64_t now_ms, int *key)
{
    if (!key || input->active_key < 0 || now_ms < input->pressed_ms) return XG_INPUT_NONE;
    const uint64_t elapsed = now_ms - input->pressed_ms;
    if (elapsed >= STUCK_MS) {
        /* No new gesture can begin until a fresh physical PRESS. */
        xigua_input_init(input);
        return XG_INPUT_NONE;
    }
    if (!input->consumed && !input->long_sent && elapsed >= HOLD_MS) {
        input->long_sent = true;
        *key = input->active_key;
        return XG_INPUT_LONG;
    }
    return XG_INPUT_NONE;
}
