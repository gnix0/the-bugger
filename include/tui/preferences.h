#ifndef THE_BUGGER_PREFERENCES_H
#define THE_BUGGER_PREFERENCES_H

#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>
#include "tui/keybinds.h"

typedef struct {
    char        directory[4000];
    char        path[4096];
    char        error[192];
    struct stat stamp;
    size_t      palette;
    int         keys[TB_BIND_COUNT];
    bool        existed;
    bool        blocked;
    bool        vim;
} TbPreferences_t;

/* Missing files use Catppuccin Macchiato. Invalid files are preserved. */
bool tb_preferences_load(TbPreferences_t *preferences);
bool tb_preferences_save(TbPreferences_t *preferences, size_t palette);
bool tb_preferences_save_keys(TbPreferences_t *preferences, const int keys[TB_BIND_COUNT]);
bool tb_preferences_save_input(TbPreferences_t *preferences, const int keys[TB_BIND_COUNT], bool vim);

#endif // THE_BUGGER_PREFERENCES_H
