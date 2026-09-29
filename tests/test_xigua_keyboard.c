#include "../main/xigua/xigua_keyboard.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void select_key(xigua_keyboard_t *keyboard, uint8_t key)
{
    keyboard->selection = key;
}

static void test_layout_and_character_coverage(void)
{
    xigua_keyboard_t keyboard;
    assert(xigua_keyboard_init(&keyboard, "", 33));
    assert(XIGUA_KEYBOARD_KEY_COUNT == 25);
    assert(XIGUA_KEYBOARD_MODE_COUNT == 3);
    assert(strcmp(xigua_keyboard_label(&keyboard, 0), "a") == 0);
    assert(strcmp(xigua_keyboard_label(&keyboard, 19), "t") == 0);
    assert(strcmp(xigua_keyboard_label(&keyboard, XIGUA_KEYBOARD_KEY_LOWER), "小写") == 0);
    assert(strcmp(xigua_keyboard_label(&keyboard, XIGUA_KEYBOARD_KEY_UPPER), "大写") == 0);
    assert(strcmp(xigua_keyboard_label(&keyboard, XIGUA_KEYBOARD_KEY_SYMBOLS), "数符") == 0);
    assert(strcmp(xigua_keyboard_label(&keyboard, XIGUA_KEYBOARD_KEY_DEL), "退格") == 0);
    assert(strcmp(xigua_keyboard_label(&keyboard, XIGUA_KEYBOARD_KEY_DONE), "确认") == 0);

    bool seen[127] = { false };
    size_t total = 0;
    for (uint8_t mode = 0; mode < XIGUA_KEYBOARD_MODE_COUNT; ++mode) {
        assert(xigua_keyboard_set_mode(&keyboard, (xigua_keyboard_mode_t)mode));
        assert(keyboard.page == 0);
        for (uint8_t page = 0; page < xigua_keyboard_page_count(&keyboard); ++page) {
            keyboard.page = page;
            for (uint8_t key = 0; key < XIGUA_KEYBOARD_CHAR_KEY_COUNT; ++key) {
                if (!xigua_keyboard_key_enabled(&keyboard, key)) continue;
                const char *label = xigua_keyboard_label(&keyboard, key);
                char expected = strcmp(label, "SPC") == 0 ? ' ' : label[0];
                assert((expected >= 33 && expected <= 126) || expected == ' ');
                assert(label[1] == '\0' || strcmp(label, "SPC") == 0);
                assert(expected == ' ' || !seen[(unsigned char)expected]);
                seen[(unsigned char)expected] = true;
                ++total;
                keyboard.text[0] = '\0';
                select_key(&keyboard, key);
                assert(xigua_keyboard_press(&keyboard) == XIGUA_KEYBOARD_EDITED);
                assert(strlen(keyboard.text) == 1 && keyboard.text[0] == expected);
            }
        }
    }
    assert(total == 97); /* Space is repeated in all three sets. */
    assert(seen[' ']);
    for (unsigned char c = 33; c <= 126; ++c) assert(seen[c]);
    assert(!xigua_keyboard_key_enabled(&keyboard, 3)); /* Symbol page three has 3 chars. */
    assert(strcmp(xigua_keyboard_label(&keyboard, 14), "") == 0);
}

static void test_movement_modes_and_paging(void)
{
    xigua_keyboard_t keyboard;
    assert(xigua_keyboard_init(&keyboard, "", 65));
    keyboard.selection = 24;
    xigua_keyboard_move(&keyboard, 1);
    assert(keyboard.selection == 0);
    xigua_keyboard_move(&keyboard, -1);
    assert(keyboard.selection == 24);
    assert(xigua_keyboard_page_count(&keyboard) == 2);
    xigua_keyboard_next_page(&keyboard);
    assert(keyboard.mode == XIGUA_KEYBOARD_MODE_LOWER && keyboard.page == 1);
    keyboard.selection = 6; /* Lowercase page two ends with SPC. */
    xigua_keyboard_move(&keyboard, 1);
    assert(keyboard.selection == XIGUA_KEYBOARD_KEY_DEL);
    keyboard.selection = 0;
    xigua_keyboard_move(&keyboard, -1);
    assert(keyboard.selection == XIGUA_KEYBOARD_KEY_DONE);

    select_key(&keyboard, XIGUA_KEYBOARD_KEY_UPPER);
    assert(xigua_keyboard_press(&keyboard) == XIGUA_KEYBOARD_MODE_CHANGED);
    assert(keyboard.mode == XIGUA_KEYBOARD_MODE_UPPER && keyboard.page == 0);
    assert(strcmp(xigua_keyboard_label(&keyboard, 0), "A") == 0);
    select_key(&keyboard, XIGUA_KEYBOARD_KEY_SYMBOLS);
    assert(xigua_keyboard_press(&keyboard) == XIGUA_KEYBOARD_MODE_CHANGED);
    assert(keyboard.mode == XIGUA_KEYBOARD_MODE_SYMBOLS);
    assert(xigua_keyboard_page_count(&keyboard) == 3);
    assert(strcmp(xigua_keyboard_label(&keyboard, 0), "0") == 0);
    xigua_keyboard_next_page(&keyboard);
    assert(keyboard.page == 1);
    xigua_keyboard_next_page(&keyboard);
    xigua_keyboard_next_page(&keyboard);
    assert(keyboard.page == 0);
}

static void test_capacity_space_and_password_length(void)
{
    xigua_keyboard_t keyboard;
    assert(xigua_keyboard_init(&keyboard, "", 33)); /* 32-byte SSID payload. */
    for (unsigned i = 0; i < 32; ++i) {
        keyboard.page = 0;
        keyboard.selection = (uint8_t)(i % 20);
        assert(xigua_keyboard_press(&keyboard) == XIGUA_KEYBOARD_EDITED);
    }
    assert(strlen(keyboard.text) == 32);
    keyboard.selection = 0;
    assert(xigua_keyboard_press(&keyboard) == XIGUA_KEYBOARD_FULL);
    assert(strlen(keyboard.text) == 32);
    keyboard.selection = XIGUA_KEYBOARD_KEY_DEL;
    assert(xigua_keyboard_press(&keyboard) == XIGUA_KEYBOARD_EDITED);
    keyboard.selection = 6; /* Lowercase page two ends with SPC. */
    keyboard.page = 1;
    assert(xigua_keyboard_press(&keyboard) == XIGUA_KEYBOARD_EDITED);
    assert(keyboard.text[31] == ' ');

    assert(xigua_keyboard_init(&keyboard, "", 65)); /* 64-hex-character password. */
    keyboard.page = 1;
    keyboard.selection = 6;
    for (unsigned i = 0; i < 64; ++i) {
        assert(xigua_keyboard_press(&keyboard) == XIGUA_KEYBOARD_EDITED);
    }
    assert(strlen(keyboard.text) == 64);
    assert(xigua_keyboard_press(&keyboard) == XIGUA_KEYBOARD_FULL);
}

static void test_utf8_delete_and_boundary_copy(void)
{
    xigua_keyboard_t keyboard;
    const char initial[] = "A\xE4\xB8\xAD"; /* A + 中 */
    assert(xigua_keyboard_init(&keyboard, initial, 5));
    assert(strcmp(keyboard.text, initial) == 0);
    keyboard.selection = XIGUA_KEYBOARD_KEY_DEL;
    assert(xigua_keyboard_press(&keyboard) == XIGUA_KEYBOARD_EDITED);
    assert(strcmp(keyboard.text, "A") == 0);
    assert(xigua_keyboard_press(&keyboard) == XIGUA_KEYBOARD_EDITED);
    assert(keyboard.text[0] == '\0');
    assert(xigua_keyboard_press(&keyboard) == XIGUA_KEYBOARD_NOOP);

    assert(xigua_keyboard_init(&keyboard, "A\xE4\xB8\xAD", 4));
    assert(strcmp(keyboard.text, "A") == 0); /* Incomplete code point omitted. */
    assert(!xigua_keyboard_init(&keyboard, "", 0));
    assert(!xigua_keyboard_init(&keyboard, "", 66));
}

static void test_done_cancel_are_terminal(void)
{
    xigua_keyboard_t keyboard;
    assert(xigua_keyboard_init(&keyboard, "abc", 65));
    keyboard.selection = XIGUA_KEYBOARD_KEY_DONE;
    assert(xigua_keyboard_press(&keyboard) == XIGUA_KEYBOARD_DONE);
    assert(keyboard.finished && !keyboard.cancelled);
    assert(strcmp(keyboard.text, "abc") == 0);
    keyboard.selection = 0;
    assert(xigua_keyboard_press(&keyboard) == XIGUA_KEYBOARD_NOOP);
    assert(strcmp(keyboard.text, "abc") == 0);

    assert(xigua_keyboard_init(&keyboard, "secret", 65));
    assert(xigua_keyboard_cancel(&keyboard));
    assert(keyboard.cancelled && !keyboard.finished);
    assert(strcmp(keyboard.text, "secret") == 0);
    keyboard.selection = 0;
    assert(xigua_keyboard_press(&keyboard) == XIGUA_KEYBOARD_NOOP);
    assert(strcmp(keyboard.text, "secret") == 0);
    assert(!xigua_keyboard_cancel(&keyboard));
}

int main(void)
{
    test_layout_and_character_coverage();
    test_movement_modes_and_paging();
    test_capacity_space_and_password_length();
    test_utf8_delete_and_boundary_copy();
    test_done_cancel_are_terminal();
    puts("xigua keyboard tests passed");
    return 0;
}
