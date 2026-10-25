#ifndef THE_BUGGER_PREFERENCES_H
#define THE_BUGGER_PREFERENCES_H
#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>
#include "keybinds.h"

typedef struct {
    char directory[4000], path[4096], error[192];
    struct stat stamp;
    size_t palette;
    int keys[TB_BIND_COUNT];
    bool existed, blocked, vim;
} TbPreferences;
/* Missing files use Catppuccin Macchiato. Invalid files are preserved. */
bool tb_preferences_load(TbPreferences *preferences);
bool tb_preferences_save(TbPreferences *preferences, size_t palette);
bool tb_preferences_save_keys(TbPreferences *preferences, const int keys[TB_BIND_COUNT]);
bool tb_preferences_save_input(TbPreferences *preferences, const int keys[TB_BIND_COUNT], bool vim);
#endif
