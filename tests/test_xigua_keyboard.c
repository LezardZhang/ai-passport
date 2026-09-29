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
    assert(XIGUA_KEYBOARD_PAGE_COUNT == 5);
    assert(strcmp(xigua_keyboard_label(&keyboard, 0), "a") == 0);
    assert(strcmp(xigua_keyboard_label(&keyboard, 19), "t") == 0);
    assert(strcmp(xigua_keyboard_label(&keyboard, XIGUA_KEYBOARD_KEY_DEL), "DEL") == 0);
    assert(strcmp(xigua_keyboard_label(&keyboard, XIGUA_KEYBOARD_KEY_SPACE), "SPC") == 0);
    assert(strcmp(xigua_keyboard_label(&keyboard, XIGUA_KEYBOARD_KEY_PAGE), "PG") == 0);
    assert(strcmp(xigua_keyboard_label(&keyboard, XIGUA_KEYBOARD_KEY_DONE), "DON") == 0);
    assert(strcmp(xigua_keyboard_label(&keyboard, XIGUA_KEYBOARD_KEY_CANCEL), "ESC") == 0);

    bool seen[127] = { false };
    size_t total = 0;
    for (uint8_t page = 0; page < XIGUA_KEYBOARD_PAGE_COUNT; ++page) {
        keyboard.page = page;
        for (uint8_t key = 0; key < XIGUA_KEYBOARD_CHAR_KEY_COUNT; ++key) {
            if (!xigua_keyboard_key_enabled(&keyboard, key)) continue;
            const char *label = xigua_keyboard_label(&keyboard, key);
            assert(label[0] >= 33 && label[0] <= 126 && label[1] == '\0');
            assert(!seen[(unsigned char)label[0]]);
            seen[(unsigned char)label[0]] = true;
            ++total;
            keyboard.text[0] = '\0'; /* Exercise each key without exceeding the SSID limit. */
            select_key(&keyboard, key);
            assert(xigua_keyboard_press(&keyboard) == XIGUA_KEYBOARD_EDITED);
            assert(!strcmp(keyboard.text, label));
        }
    }
    assert(total == 94);
    for (unsigned char c = 33; c <= 126; ++c) assert(seen[c]);
    assert(!xigua_keyboard_key_enabled(&keyboard, 14)); /* Page five has 14 chars. */
    assert(strcmp(xigua_keyboard_label(&keyboard, 14), "") == 0);
}

static void test_movement_and_paging(void)
{
    xigua_keyboard_t keyboard;
    assert(xigua_keyboard_init(&keyboard, "", 65));
    keyboard.selection = 24;
    xigua_keyboard_move(&keyboard, 1);
    assert(keyboard.selection == 0);
    xigua_keyboard_move(&keyboard, -1);
    assert(keyboard.selection == 24);
    for (unsigned i = 0; i < 12; ++i) xigua_keyboard_next_page(&keyboard);
    assert(keyboard.page == 2);
    keyboard.page = 4;
    keyboard.selection = 19;
    xigua_keyboard_move(&keyboard, 1);
    assert(keyboard.selection == XIGUA_KEYBOARD_KEY_DEL);
    keyboard.selection = 0;
    xigua_keyboard_move(&keyboard, -1);
    assert(keyboard.selection == XIGUA_KEYBOARD_KEY_CANCEL);
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
    keyboard.selection = XIGUA_KEYBOARD_KEY_SPACE;
    assert(xigua_keyboard_press(&keyboard) == XIGUA_KEYBOARD_EDITED);
    assert(keyboard.text[31] == ' ');

    assert(xigua_keyboard_init(&keyboard, "", 65)); /* 64-hex-character password. */
    for (unsigned i = 0; i < 64; ++i) {
        keyboard.selection = XIGUA_KEYBOARD_KEY_SPACE;
        assert(xigua_keyboard_press(&keyboard) == XIGUA_KEYBOARD_EDITED);
    }
    assert(strlen(keyboard.text) == 64);
    keyboard.selection = XIGUA_KEYBOARD_KEY_SPACE;
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
    keyboard.selection = XIGUA_KEYBOARD_KEY_CANCEL;
    assert(xigua_keyboard_press(&keyboard) == XIGUA_KEYBOARD_CANCEL);
    assert(keyboard.cancelled && !keyboard.finished);
    assert(strcmp(keyboard.text, "secret") == 0);
    keyboard.selection = 0;
    assert(xigua_keyboard_press(&keyboard) == XIGUA_KEYBOARD_NOOP);
    assert(strcmp(keyboard.text, "secret") == 0);
}

int main(void)
{
    test_layout_and_character_coverage();
    test_movement_and_paging();
    test_capacity_space_and_password_length();
    test_utf8_delete_and_boundary_copy();
    test_done_cancel_are_terminal();
    puts("xigua keyboard tests passed");
    return 0;
}
