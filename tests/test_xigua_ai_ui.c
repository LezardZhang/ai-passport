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
    /* Pause/resume and restart are separate actions and preserve text paging. */
    ui = (x_ai_ui_t){ .view = X_AI_READING, .pages = 4, .page = 2,
                     .has_reply = true, .story_reply = true, .speaking = true };
    assert(x_ai_input(&ui, X_AI_OK) == X_AI_NONE && ui.view == X_AI_ACTIONS);
    assert(x_ai_input(&ui, X_AI_OK) == X_AI_PAUSE_VOICE);
    ui.paused = true;
    assert(x_ai_input(&ui, X_AI_OK) == X_AI_RESUME_VOICE && ui.page == 2);
    x_ai_input(&ui, X_AI_DOWN);
    assert(x_ai_input(&ui, X_AI_OK) == X_AI_RESTART_VOICE);
    x_ai_input(&ui, X_AI_DOWN);
    assert(x_ai_input(&ui, X_AI_OK) == X_AI_NONE && ui.view == X_AI_READING && ui.page == 2);
    x_ai_input(&ui, X_AI_OK);
    x_ai_input(&ui, X_AI_UP); /* Four actions wrap to Another story. */
    assert(ui.focus == 3);
    assert(x_ai_input(&ui, X_AI_OK) == X_AI_STOP_VOICE && ui.view == X_AI_READY);
    assert(ui.has_reply);
    assert(x_ai_input(&ui, X_AI_BACK) == X_AI_HOME);
    ui = (x_ai_ui_t){ .view = X_AI_ACTIONS, .story_reply = true, .has_reply = true };
    assert(x_ai_input(&ui, X_AI_OK) == X_AI_READ_REPLY);
    x_ai_input(&ui, X_AI_DOWN);
    assert(x_ai_input(&ui, X_AI_OK) == X_AI_READ_REPLY);
    assert(x_ai_page_count(190, 190) == 1);
    ui = (x_ai_ui_t){ .view = X_AI_ACTIONS, .has_reply = true, .page = 2 };
    x_ai_input(&ui, X_AI_UP);
    assert(ui.focus == 3 && x_ai_input(&ui, X_AI_OK) == X_AI_READ_REPLY);
    ui.speaking = true;
    assert(x_ai_input(&ui, X_AI_OK) == X_AI_PAUSE_VOICE);
    ui.paused = true;
    assert(x_ai_input(&ui, X_AI_OK) == X_AI_RESUME_VOICE && ui.page == 2);
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
    assert(xigua_text_reply_copy(text, sizeof(text), "OK", "length"));
    assert(!xigua_text_reply_copy(text, sizeof(text), "OK", "stop"));
    assert(!xigua_text_reply_copy(text, sizeof(text), "OK", NULL));
    assert(xigua_text_reply_copy(text, 3, utf8, "stop"));
    /* A multi-page Chinese reply must survive beyond the old 1024-byte limit. */
    char long_reply[2404], displayed[4096];
    for (size_t i = 0; i < 800; ++i) memcpy(long_reply + i * 3, "\xe4\xb8\xad", 3);
    memcpy(long_reply + 2400, "\xe3\x80\x82", 4);
    assert(!xigua_text_reply_copy(displayed, sizeof(displayed), long_reply, "stop"));
    assert(strlen(displayed) == 2403 && strcmp(displayed + 2400, "\xe3\x80\x82") == 0);
    char formatted[] = "\n# Title\r\n\n**文字** 😀\n\n1. 第一段\n- 第二段 ✨\n\n";
    xigua_text_plain_reply(formatted);
    assert(strcmp(formatted, "Title\n文字\n第一段\n第二段") == 0);
    char plain[] = "2026 年睡眠记录。\n\n\n网络 GUANTANG_2.4G。";
    xigua_text_plain_reply(plain);
    assert(strcmp(plain, "2026 年睡眠记录。\n网络 GUANTANG_2.4G。") == 0);
    puts("AI reply protection, explicit recording, paging and UTF-8 bounds: PASS");
}
