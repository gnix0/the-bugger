#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- Standard POSIX feature selection. */
#include "preferences.h"
#include "theme.h"
#include "../support/check.h"
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

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
    const char *invalid[] = {
        "{", "{}", "{\"version\":2,\"theme\":\"catppuccin\",\"variant\":\"macchiato\"}",
        "{\"version\":1,\"theme\":\"catppuccin\",\"variant\":\"unknown\"}",
        "{\"version\":1,\"theme\":\"nightfox\",\"variant\":\"mocha\"}",
        "{\"version\":1,\"theme\":\"catppuccin\\u0000junk\",\"variant\":\"macchiato\"}",
        "{\"version\":1,\"theme\":\"catppuccin\",\"variant\":\"mocha\"} garbage"
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
