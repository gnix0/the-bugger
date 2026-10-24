#ifndef THE_BUGGER_PREFERENCES_H
#define THE_BUGGER_PREFERENCES_H
#include <stdbool.h>
#include <stddef.h>
#include <sys/stat.h>

typedef struct {
    char directory[4000], path[4096], error[192];
    struct stat stamp;
    size_t palette;
    bool existed, blocked;
} TbPreferences;
/* Missing files use Catppuccin Macchiato. Invalid files are preserved. */
bool tb_preferences_load(TbPreferences *preferences);
bool tb_preferences_save(TbPreferences *preferences, size_t palette);
#endif
