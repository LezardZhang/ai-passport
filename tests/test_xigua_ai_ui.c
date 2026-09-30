#include "xigua_ai_ui.h"
#include "xigua_text.h"
#include <assert.h>
#include <stdio.h>

int main(void)
{
    x_ai_ui_t ui = { .view = X_AI_READY, .pages = 4 };
    /* Repeated short confirmations never issue any network/audio effect. */
    for (int i = 0; i < 20; ++i) assert(x_ai_input(&ui, X_AI_OK) == X_AI_NONE);
    assert(ui.view == X_AI_READY);
    assert(x_ai_input(&ui, X_AI_HOLD_OK) == X_AI_START_VOICE);
    assert(x_ai_input(&ui, X_AI_HOLD_OK) == X_AI_NONE);
    assert(x_ai_input(&ui, X_AI_RELEASE_OK) == X_AI_STOP_VOICE);
    assert(ui.view == X_AI_WAITING);
    assert(x_ai_input(&ui, X_AI_OK) == X_AI_NONE);
    x_ai_complete(&ui, true);
    for (int i = 0; i < 20; ++i) {
        assert(x_ai_input(&ui, X_AI_OK) == X_AI_NONE);
        assert(x_ai_input(&ui, X_AI_HOLD_OK) == X_AI_NONE);
    }
    assert(ui.view == X_AI_READING && ui.has_reply);
    for (int i = 0; i < 20; ++i) x_ai_input(&ui, X_AI_DOWN);
    assert(ui.page == 3);
    for (int i = 0; i < 20; ++i) x_ai_input(&ui, X_AI_UP);
    assert(ui.page == 0);
    x_ai_input(&ui, X_AI_OK); x_ai_input(&ui, X_AI_DOWN);
    assert(x_ai_input(&ui, X_AI_OK) == X_AI_NONE && ui.view == X_AI_READY);
    assert(ui.has_reply);
    x_ai_input(&ui, X_AI_DOWN); x_ai_input(&ui, X_AI_OK);
    assert(ui.view == X_AI_READING); /* Old reply remains accessible. */
    x_ai_complete(&ui, false);
    assert(ui.view == X_AI_ERROR && ui.has_reply);
    x_ai_input(&ui, X_AI_OK);
    assert(ui.view == X_AI_READY);
    assert(x_ai_page_count(0, 190) == 1);
    assert(x_ai_page_count(190, 190) == 1);
    assert(x_ai_page_count(191, 190) == 2);
    assert(x_ai_page_count(380, 190) == 2);
    char text[10];
    const char utf8[] = "A\xe4\xb8\xad\xf0\xa0\x80\x80Z";
    for (size_t cap = 1; cap <= sizeof(text); ++cap) {
        xigua_text_copy(text, cap, utf8);
        size_t n = strlen(text);
        assert(n == 0 || n == 1 || n == 4 || n == 8 || n == 9);
        assert(n < cap);
    }
    assert(!xigua_text_copy(text, sizeof(text), utf8));
    assert(xigua_text_copy(text, 3, utf8) && strcmp(text, "A") == 0);
    puts("AI reply protection, explicit recording, paging and UTF-8 bounds: PASS");
}
