#include "keybinds.h"
#include <curses.h>
#include <stdio.h>
#include <string.h>

void tb_keybind_defaults(int keys[TB_BIND_COUNT])
{
    memset(keys, 0, sizeof(int) * TB_BIND_COUNT);
    keys[TB_BIND_CONTINUE] = 5; keys[TB_BIND_OVER] = 10; keys[TB_BIND_INTO] = 11;
    keys[TB_BIND_OUT] = 23; keys[TB_BIND_BREAKPOINT] = 9;
    keys[TB_BIND_PREVIOUS_PANE] = 256 + '['; keys[TB_BIND_NEXT_PANE] = 256 + ']';
    for (int i = 0; i < 9; ++i) { keys[TB_BIND_PANE_1 + i] = 256 + '1' + i; }
    keys[TB_BIND_BACK] = 256 + 'q';
}
const char *tb_keybind_action(size_t action)
{
    static const char *const names[] = {
        "Continue", "Pause / Halt", "Step over", "Step into", "Step out", "Breakpoint", "Reset",
        "Move left", "Move down", "Move up", "Move right",
        "Content pane left", "Content pane down", "Content pane up", "Content pane right",
        "Previous pane", "Next pane", "Select pane 1", "Select pane 2", "Select pane 3",
        "Select pane 4", "Select pane 5", "Select pane 6", "Select pane 7", "Select pane 8",
        "Select pane 9", "Back / close session", "Quit",
        "Next control", "Previous control"
    };
    return action < TB_BIND_COUNT ? names[action] : "Unknown";
}
void tb_keybind_name(int key, char *text, size_t size)
{
    if (key >= 256 + 33 && key <= 256 + 126) { (void)snprintf(text, size, "%c", key - 256); }
    else if (key < 0 && key >= -26) { (void)snprintf(text, size, "Ctrl+%c", 'A' - key - 1); }
    else if (key == 0) { (void)snprintf(text, size, "None"); }
    else if (key >= 1 && key <= 12) { (void)snprintf(text, size, "F%d", key); }
    else if (key >= 13 && key <= 24) { (void)snprintf(text, size, "Shift+F%d", key - 12); }
    else { (void)snprintf(text, size, "Invalid"); }
}
bool tb_keybind_parse(const char *text, int *key)
{
    if (text[0] && !text[1] && (unsigned char)text[0] >= 33 && (unsigned char)text[0] <= 126) {
        *key = 256 + (unsigned char)text[0]; return true;
    }
    if (strlen(text) == 6 && strncmp(text, "Ctrl+", 5) == 0 && text[5] >= 'A' && text[5] <= 'Z') {
        *key = -(text[5] - 'A' + 1); return true;
    }
    if (strcmp(text, "None") == 0) { *key = 0; return true; }
    bool shift = strncmp(text, "Shift+", 6) == 0;
    const char *p = text + (shift ? 6 : 0);
    if (*p != 'F') { return false; }
    ++p;
    if (!*p || strlen(p) > 2) { return false; }
    int value = 0;
    for (; *p; ++p) { if (*p < '0' || *p > '9') { return false; } value = value * 10 + *p - '0'; }
    if (value < 1 || value > (shift ? 12 : 24)) { return false; }
    *key = value + (shift ? 12 : 0); return true;
}
/* Action index and encoded key are distinct domains validated below. */
bool tb_keybind_allowed(size_t action, int key) // NOLINT(bugprone-easily-swappable-parameters)
{
    if (action >= TB_BIND_COUNT) { return false; }
    if (key >= 256 + 33 && key <= 256 + 126) { return true; }
    if (key == -8 || key == -10 || key == -11 || key == -12) {
        return action >= TB_BIND_CONTENT_LEFT && action <= TB_BIND_CONTENT_RIGHT;
    }
    if (key >= 0 && key <= 24) { return true; }
    return key <= -1 && key >= -26 && key != -3 && key != -9 && key != -13 &&
           key != -17 && key != -19 && key != -26;
}
bool tb_keybinds_valid(const int keys[TB_BIND_COUNT])
{
    for (size_t i = 0; i < TB_BIND_COUNT; ++i) {
        if (!tb_keybind_allowed(i, keys[i])) { return false; }
        for (size_t j = 0; keys[i] && j < i; ++j) { if (keys[i] == keys[j]) { return false; } }
    }
    return true;
}
bool tb_keybind_matches(int binding, wint_t key, bool special)
{
    if (!binding) { return false; }
    if (binding > 0 && binding <= 24) { return special && (int)key == KEY_F(binding); }
    if (binding >= 289 && binding <= 382) { return !special && (int)key == binding - 256; }
    if (binding == -8 && special && key == KEY_BACKSPACE) { return true; }
    return binding < 0 && binding >= -26 && !special && (int)key == -binding;
}

/* Apply aliases atomically: never steal an existing custom assignment. */
bool tb_keybind_vim(int keys[TB_BIND_COUNT], bool enabled)
{
    static const int aliases[] = {256 + 'h', 256 + 'j', 256 + 'k', 256 + 'l', -8, -10, -11, -12};
    int draft[TB_BIND_COUNT]; memcpy(draft, keys, sizeof draft);
    for (size_t i = 0; i < sizeof aliases / sizeof aliases[0]; ++i) {
        size_t action = TB_BIND_LEFT + i;
        if (enabled && !draft[action]) { draft[action] = aliases[i]; }
        else if (!enabled && draft[action] == aliases[i]) { draft[action] = 0; }
    }
    if (!tb_keybinds_valid(draft)) { return false; }
    memcpy(keys, draft, sizeof draft); return true;
}
