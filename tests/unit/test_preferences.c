#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- Standard POSIX feature selection. */
#include "preferences.h"
#include "theme.h"
#include "../support/check.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <json-c/json.h>

static void write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "w"); CHECK(file);
    CHECK(fputs(text, file) >= 0 && fclose(file) == 0);
}
int main(void)
{
    char root[] = "/tmp/the-bugger-preferences-XXXXXX"; CHECK(mkdtemp(root));
    CHECK(setenv("XDG_CONFIG_HOME", root, 1) == 0);
    TbPreferences first, second;
    CHECK(tb_preferences_load(&first) && !first.existed);
    CHECK(first.palette == tb_theme_find("catppuccin", "macchiato"));
    CHECK(access(first.path, F_OK) != 0); /* Opening/previewing never creates a file. */
    CHECK(tb_preferences_load(&second));
    CHECK(tb_preferences_save(&first, tb_theme_find("rose-pine", "dawn")));
    struct stat info; CHECK(stat(first.path, &info) == 0 && (info.st_mode & 0777) == 0600);
    CHECK(!tb_preferences_save(&second, tb_theme_find("catppuccin", "mocha")));
    CHECK(strstr(second.error, "another instance"));
    CHECK(tb_preferences_load(&second) && second.palette == tb_theme_find("rose-pine", "dawn"));
    CHECK(tb_preferences_save(&second, tb_theme_find("monokai-pro", "classic")));
    CHECK(tb_preferences_load(&first) && first.palette == tb_theme_find("monokai-pro", "classic"));
    for (size_t i = 0; i < tb_theme_count(); ++i) {
        CHECK(tb_preferences_save(&first, i));
        CHECK(tb_preferences_load(&first) && first.palette == i);
    }
    CHECK(tb_preferences_save(&first, tb_theme_find("monokai-pro", "classic")));
    int keys[TB_BIND_COUNT]; tb_keybind_defaults(keys); keys[TB_BIND_PAUSE] = 0; keys[TB_BIND_CONTINUE] = 6;
    CHECK(tb_preferences_save_keys(&first, keys));
    CHECK(tb_preferences_load(&second) && second.keys[TB_BIND_CONTINUE] == 6);
    CHECK(second.keys[TB_BIND_PAUSE] == 0); /* Saved None is never migrated to F6. */
    CHECK(second.palette == tb_theme_find("monokai-pro", "classic"));
    CHECK(tb_preferences_save(&second, tb_theme_find("everforest", "dark-medium")));
    CHECK(tb_preferences_load(&first) && first.keys[TB_BIND_CONTINUE] == 6);
    CHECK(!first.vim && tb_keybind_vim(keys, true));
    CHECK(tb_preferences_save_input(&first, keys, true));
    CHECK(tb_preferences_load(&second) && second.vim && second.keys[TB_BIND_LEFT] == 256 + 'h');
    CHECK(tb_preferences_save(&second, second.palette));
    CHECK(tb_preferences_load(&first) && first.vim);
    CHECK(tb_preferences_save_keys(&first, first.keys));
    CHECK(tb_preferences_load(&second) && second.vim);
    /* An old key map loses stock Vim aliases, preserving custom slots, without a write. */
    json_object *legacy = json_object_from_file(first.path); CHECK(legacy);
    json_object_object_del(legacy, "vim");
    CHECK(json_object_array_put_idx(json_object_object_get(legacy, "keys"), TB_BIND_UP, json_object_new_int(256 + 'w')) == 0);
    write_file(first.path, json_object_to_json_string(legacy)); json_object_put(legacy);
    CHECK(tb_preferences_load(&first) && !first.vim && first.keys[TB_BIND_LEFT] == 0 && first.keys[TB_BIND_UP] == 256 + 'w');
    keys[TB_BIND_INTO] = 6; CHECK(!tb_preferences_save_keys(&first, keys));
    keys[TB_BIND_INTO] = -17; CHECK(!tb_preferences_save_keys(&first, keys));
    CHECK(tb_preferences_load(&second) && second.keys[TB_BIND_INTO] == 11);
    /* Legacy theme-only settings acquire defaults without changing on disk. */
    write_file(first.path, "{\"version\":1,\"theme\":\"rose-pine\",\"variant\":\"dawn\"}");
    CHECK(tb_preferences_load(&first) && first.keys[TB_BIND_CONTINUE] == 5);
    for (int fault = 0; fault < 5; ++fault) {
        json_object *bad = json_tokener_parse("{\"version\":1,\"theme\":\"rose-pine\",\"variant\":\"dawn\"}");
        json_object *array = json_object_new_array(); CHECK(bad && array);
        int defaults[TB_BIND_COUNT]; tb_keybind_defaults(defaults);
        for (int i = 0; i < TB_BIND_COUNT; ++i) {
            json_object *entry = json_object_new_int(defaults[i]);
            if (i == 0) {
                json_object_put(entry);
                entry = fault == 0 ? json_object_new_int(11) : fault == 1 ? json_object_new_int(-17) :
                        fault == 2 ? json_object_new_int64(INT64_C(2147483648)) : fault == 3 ? json_object_new_boolean(1) : json_object_new_string("F6");
            }
            CHECK(entry && json_object_array_add(array, entry) == 0);
        }
        CHECK(json_object_object_add(bad, "keys", array) == 0);
        const char *text = json_object_to_json_string(bad); write_file(first.path, text);
        CHECK(!tb_preferences_load(&first) && first.blocked);
        CHECK(first.keys[TB_BIND_CONTINUE] == 5 && !tb_preferences_save_keys(&first, defaults));
        char preserved[1025] = {0}; FILE *file = fopen(first.path, "r"); CHECK(file);
        CHECK(fread(preserved, 1, sizeof preserved - 1, file) == strlen(text) && fclose(file) == 0);
        CHECK(strcmp(preserved, text) == 0); json_object_put(bad);
    }
    const char *invalid[] = {
        "{\"version\":1,\"theme\":\"catppuccin\",\"variant\":\"macchiato\",\"vim\":null}",
        "{\"version\":1,\"theme\":\"catppuccin\",\"variant\":\"macchiato\",\"vim\":1}",
        "{\"version\":1,\"theme\":\"catppuccin\",\"variant\":\"macchiato\",\"vim\":false,\"extra\":0}",
        "{", "{}", "{\"version\":2,\"theme\":\"catppuccin\",\"variant\":\"macchiato\"}",
        "{\"version\":1,\"theme\":\"catppuccin\",\"variant\":\"unknown\"}",
        "{\"version\":1,\"theme\":\"nightfox\",\"variant\":\"mocha\"}",
        "{\"version\":1,\"theme\":\"catppuccin\\u0000junk\",\"variant\":\"macchiato\"}",
        "{\"version\":1,\"theme\":\"catppuccin\",\"variant\":\"mocha\"} garbage"
        ,"{\"version\":1,\"theme\":\"catppuccin\",\"variant\":\"macchiato\",\"keys\":null}"
        ,"{\"version\":1,\"theme\":\"catppuccin\",\"variant\":\"macchiato\",\"keys\":[]}"
    };
    for (size_t i = 0; i < sizeof invalid / sizeof invalid[0]; ++i) {
        write_file(first.path, invalid[i]);
        CHECK(!tb_preferences_load(&first) && first.blocked);
        CHECK(first.palette == tb_theme_find("catppuccin", "macchiato"));
        CHECK(!tb_preferences_save(&first, 0));
        FILE *file = fopen(first.path, "r"); CHECK(file); char text[256] = {0};
        CHECK(fread(text, 1, sizeof text - 1, file) == strlen(invalid[i]) && fclose(file) == 0);
        CHECK(strcmp(text, invalid[i]) == 0);
    }
    CHECK(unlink(first.path) == 0);
    CHECK(symlink("/dev/null", first.path) == 0);
    CHECK(!tb_preferences_load(&first) && !tb_preferences_save(&first, 0));
    CHECK(unlink(first.path) == 0);
    CHECK(mkdir(first.path, 0700) == 0);
    CHECK(!tb_preferences_load(&first)); CHECK(rmdir(first.path) == 0);
    CHECK(tb_preferences_load(&first));
    CHECK(!tb_preferences_save(&first, tb_theme_count()));
    char lock[4096]; (void)snprintf(lock, sizeof lock, "%s/.preferences.lock", first.directory);
    CHECK(unlink(lock) == 0 && rmdir(first.directory) == 0 && rmdir(root) == 0);
    puts("PASS theme preferences: default, persistence, private atomic writes, stale writers, invalid schema and file preservation");
    return EXIT_SUCCESS;
}
