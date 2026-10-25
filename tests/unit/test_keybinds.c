#include "keybinds.h"
#include "../support/check.h"
#include <curses.h>
#include <string.h>
int main(void)
{
    int keys[TB_BIND_COUNT]; tb_keybind_defaults(keys);
    CHECK(tb_keybinds_valid(keys) && keys[TB_BIND_CONTINUE] == 5);
    CHECK(keys[TB_BIND_LEFT] == 0 && keys[TB_BIND_CONTENT_LEFT] == 0);
    CHECK(tb_keybind_vim(keys, true) && keys[TB_BIND_LEFT] == 256 + 'h');
    keys[TB_BIND_UP] = 256 + 'w';
    CHECK(tb_keybind_vim(keys, false) && keys[TB_BIND_UP] == 256 + 'w' && keys[TB_BIND_LEFT] == 0);
    keys[TB_BIND_PAUSE] = 256 + 'h';
    int before[TB_BIND_COUNT]; memcpy(before, keys, sizeof before);
    CHECK(!tb_keybind_vim(keys, true) && memcmp(before, keys, sizeof keys) == 0);
    tb_keybind_defaults(keys);
    for (int action = 0; action < TB_BIND_COUNT; ++action) {
        CHECK(strcmp(tb_keybind_action((size_t)action), "Unknown") != 0);
        for (int key = -26; key <= 382; ++key) {
            if (!tb_keybind_allowed((size_t)action, key)) { continue; }
            char name[24]; int parsed;
            tb_keybind_name(key, name, sizeof name);
            CHECK(tb_keybind_parse(name, &parsed) && parsed == key);
        }
    }
    keys[TB_BIND_INTO] = 5; CHECK(!tb_keybinds_valid(keys));
    tb_keybind_defaults(keys); keys[TB_BIND_CONTINUE] = -17; CHECK(!tb_keybinds_valid(keys));
    const int reserved[] = {-3, -9, -13, -17, -19, -26, 25, 256 + ' '};
    for (size_t i = 0; i < sizeof reserved / sizeof reserved[0]; ++i) {
        CHECK(!tb_keybind_allowed(TB_BIND_CONTINUE, reserved[i]));
        CHECK(!tb_keybind_allowed(TB_BIND_LEFT, reserved[i]));
    }
    CHECK(!tb_keybind_allowed(TB_BIND_CONTINUE, -11));
    CHECK(tb_keybind_allowed(TB_BIND_CONTENT_UP, -11));
    CHECK(tb_keybind_matches(-8, KEY_BACKSPACE, true));
    CHECK(tb_keybind_matches(6, KEY_F(6), true));
    CHECK(tb_keybind_matches(256 + 'w', 'w', false));
    CHECK(!tb_keybind_matches(0, 0, false));
    int parsed;
    CHECK(!tb_keybind_parse("F25", &parsed) && !tb_keybind_parse("Shift+F13", &parsed));
    CHECK(!tb_keybind_parse("Alt+A", &parsed) && !tb_keybind_parse("", &parsed));
    puts("PASS global keybind names, round trips, conflicts, reserved fallbacks and terminal matching");
    return EXIT_SUCCESS;
}
