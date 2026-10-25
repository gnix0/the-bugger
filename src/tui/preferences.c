#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- Standard POSIX feature selection. */
#include "preferences.h"
#include "theme.h"
#include <json-c/json.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <unistd.h>

static bool fail(TbPreferences *p, const char *message)
{
    (void)snprintf(p->error, sizeof p->error, "%s", message); return false;
}

bool tb_preferences_load(TbPreferences *p)
{
    memset(p, 0, sizeof *p);
    p->palette = tb_theme_find("catppuccin", "macchiato");
    tb_keybind_defaults(p->keys);
    const char *base = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    int length = -1;
    if (base && *base == '/') { length = snprintf(p->directory, sizeof p->directory, "%s/the-bugger", base); }
    else if (home && *home == '/') { length = snprintf(p->directory, sizeof p->directory, "%s/.config/the-bugger", home); }
    if (length < 0 || (size_t)length >= sizeof p->directory) {
        p->blocked = true; return fail(p, "An absolute XDG_CONFIG_HOME or HOME is required to save preferences.");
    }
    (void)snprintf(p->path, sizeof p->path, "%s/preferences.json", p->directory);
    int fd = open(p->path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0 && errno == ENOENT) { return true; }
    struct stat info;
    if (fd < 0 || fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != getuid() || info.st_size > 1024) {
        if (fd >= 0) { (void)close(fd); }
        p->blocked = true; return fail(p, "Cannot read owned regular preferences.json (maximum 1 KiB); file preserved.");
    }
    char text[1025]; size_t used = 0; bool ok = true;
    while (used < sizeof text - 1) {
        ssize_t got = read(fd, text + used, sizeof text - 1 - used);
        if (got < 0 && errno == EINTR) { continue; }
        if (got < 0) { ok = false; break; }
        if (!got) { break; }
        used += (size_t)got;
    }
    (void)close(fd); text[used] = 0;
    json_tokener *parser = json_tokener_new();
    if (parser) { json_tokener_set_flags(parser, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8); }
    json_object *root = ok && parser ? json_tokener_parse_ex(parser, text, (int)used + 1) : NULL;
    json_object *version = NULL, *theme = NULL, *variant = NULL, *bindings = NULL;
    int keys[TB_BIND_COUNT]; tb_keybind_defaults(keys);
    ok = root && json_object_is_type(root, json_type_object) &&
         (json_object_object_length(root) == 3 || (json_object_object_length(root) == 4 && json_object_object_get_ex(root, "keys", &bindings) && bindings)) &&
         json_object_object_get_ex(root, "version", &version) && json_object_is_type(version, json_type_int) && json_object_get_int(version) == 1 &&
         json_object_object_get_ex(root, "theme", &theme) && json_object_is_type(theme, json_type_string) &&
         json_object_object_get_ex(root, "variant", &variant) && json_object_is_type(variant, json_type_string);
    size_t palette = tb_theme_count();
    if (ok) {
        const char *family = json_object_get_string(theme), *name = json_object_get_string(variant);
        ok = strlen(family) == (size_t)json_object_get_string_len(theme) && strlen(name) == (size_t)json_object_get_string_len(variant);
        if (ok) { palette = tb_theme_find(family, name); }
        ok = palette < tb_theme_count();
    }
    if (ok && bindings) {
        ok = json_object_is_type(bindings, json_type_array) && json_object_array_length(bindings) == TB_BIND_COUNT;
        for (size_t i = 0; ok && i < TB_BIND_COUNT; ++i) {
            json_object *entry = json_object_array_get_idx(bindings, i);
            int64_t key = json_object_get_int64(entry);
            ok = json_object_is_type(entry, json_type_int) && key >= -26 && key <= 382;
            if (ok) { keys[i] = (int)key; }
        }
        ok = ok && tb_keybinds_valid(keys);
    }
    json_object_put(root); if (parser) { json_tokener_free(parser); }
    if (!ok) { p->blocked = true; return fail(p, "Invalid/unsupported preferences; using defaults. File preserved."); }
    p->palette = palette; memcpy(p->keys, keys, sizeof keys); p->stamp = info; p->existed = true; return true;
}

static bool mkdirs(const char *path)
{
    char copy[4000]; (void)snprintf(copy, sizeof copy, "%s", path);
    for (char *c = copy + 1; ; ++c) {
        if (*c && *c != '/') { continue; }
        char saved = *c; *c = 0;
        if (mkdir(copy, 0700) != 0 && errno != EEXIST) { return false; }
        struct stat info;
        if (stat(copy, &info) != 0 || !S_ISDIR(info.st_mode)) { return false; }
        *c = saved; if (!saved) { break; }
    }
    return true;
}

static bool unchanged(const TbPreferences *p)
{
    struct stat info;
    if (lstat(p->path, &info) != 0) { return errno == ENOENT && !p->existed; }
    return p->existed && S_ISREG(info.st_mode) && info.st_uid == getuid() &&
           info.st_dev == p->stamp.st_dev && info.st_ino == p->stamp.st_ino && info.st_size == p->stamp.st_size &&
           info.st_mtim.tv_sec == p->stamp.st_mtim.tv_sec && info.st_mtim.tv_nsec == p->stamp.st_mtim.tv_nsec;
}

static bool save(TbPreferences *p, size_t palette, const int keys[TB_BIND_COUNT])
{
    const TbPalette *selected = tb_theme_palette(palette);
    if (!tb_keybinds_valid(keys)) { return fail(p, "Reserved or conflicting keybinds; preferences unchanged."); }
    if (!selected || p->blocked || !*p->path) { return fail(p, "Saving blocked; correct preferences.json and restart first."); }
    p->error[0] = 0;
    if (!mkdirs(p->directory)) { return fail(p, "Cannot create preference directory."); }
    int dir = open(p->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (dir < 0) { return fail(p, "Cannot open preference directory."); }
    int lock = openat(dir, ".preferences.lock", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    struct stat info;
    if (lock < 0 || fstat(lock, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != getuid() || flock(lock, LOCK_EX | LOCK_NB) != 0) {
        if (lock >= 0) { (void)close(lock); } (void)close(dir); return fail(p, "Preferences are locked or inaccessible.");
    }
    bool ok = unchanged(p);
    if (!ok) { (void)fail(p, "Preferences changed in another instance; restart before saving."); }
    char text[512], temporary[4096], bindings[256] = {0}; size_t at = 0;
    for (size_t i = 0; i < TB_BIND_COUNT; ++i) {
        int n = snprintf(bindings + at, sizeof bindings - at, "%s%d", i ? "," : "", keys[i]);
        if (n < 0 || (size_t)n >= sizeof bindings - at) { ok = false; break; }
        at += (size_t)n;
    }
    int length = snprintf(text, sizeof text, "{\"version\":1,\"theme\":\"%s\",\"variant\":\"%s\",\"keys\":[%s]}\n", selected->family, selected->variant, bindings);
    if (length < 0 || (size_t)length >= sizeof text) { ok = false; }
    (void)snprintf(temporary, sizeof temporary, "%s/.preferences-XXXXXX", p->directory);
    int fd = -1; bool created = false;
    if (ok) { fd = mkstemp(temporary); created = fd >= 0; ok = created && fcntl(fd, F_SETFD, FD_CLOEXEC) == 0 && fchmod(fd, 0600) == 0; }
    size_t written = 0;
    while (ok && written < (size_t)length) {
        ssize_t n = write(fd, text + written, (size_t)length - written);
        if (n < 0 && errno == EINTR) { continue; }
        if (n <= 0) { ok = false; break; }
        written += (size_t)n;
    }
    if (ok) { ok = fsync(fd) == 0 && fstat(fd, &info) == 0; }
    if (fd >= 0 && close(fd) != 0) { ok = false; }
    if (ok) { ok = rename(temporary, p->path) == 0; }
    if (!ok) {
        if (created) { (void)unlink(temporary); }
        if (!*p->error) { (void)fail(p, "Could not save preferences; previous file preserved."); }
    } else {
        p->palette = palette; memmove(p->keys, keys, sizeof p->keys); p->stamp = info; p->existed = true; p->error[0] = 0;
        if (fsync(dir) != 0) { (void)fail(p, "Preferences saved; directory sync failed, crash durability uncertain."); }
    }
    (void)close(lock); (void)close(dir); return ok;
}

bool tb_preferences_save(TbPreferences *p, size_t palette)
{
    return save(p, palette, p->keys);
}
bool tb_preferences_save_keys(TbPreferences *p, const int keys[TB_BIND_COUNT])
{
    return save(p, p->palette, keys);
}
