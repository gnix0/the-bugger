#ifndef THE_BUGGER_KEYBINDS_H
#define THE_BUGGER_KEYBINDS_H
#include <stdbool.h>
#include <stddef.h>
#include <wchar.h>
enum {
    TB_BIND_CONTINUE, TB_BIND_PAUSE, TB_BIND_OVER, TB_BIND_INTO, TB_BIND_OUT,
    TB_BIND_BREAKPOINT, TB_BIND_RESET, TB_BIND_LEFT, TB_BIND_DOWN, TB_BIND_UP, TB_BIND_RIGHT,
    TB_BIND_CONTENT_LEFT, TB_BIND_CONTENT_DOWN, TB_BIND_CONTENT_UP, TB_BIND_CONTENT_RIGHT,
    TB_BIND_PREVIOUS_PANE, TB_BIND_NEXT_PANE, TB_BIND_PANE_1,
    TB_BIND_BACK = TB_BIND_PANE_1 + 9, TB_BIND_QUIT,
    TB_BIND_NEXT_CONTROL, TB_BIND_PREVIOUS_CONTROL, TB_BIND_COUNT
};
/* Existing function/Ctrl encoding; printable ASCII uses 256 + character. */
void tb_keybind_defaults(int keys[TB_BIND_COUNT]);
const char *tb_keybind_action(size_t action);
void tb_keybind_name(int key, char *text, size_t size);
bool tb_keybind_parse(const char *text, int *key);
bool tb_keybind_allowed(size_t action, int key);
bool tb_keybinds_valid(const int keys[TB_BIND_COUNT]);
bool tb_keybind_matches(int binding, wint_t key, bool special);
#endif
