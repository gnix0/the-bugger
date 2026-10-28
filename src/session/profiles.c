#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- Standard POSIX feature selection. */
#include "profiles.h"
#include <json-c/json.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>

enum { FILE_LIMIT = 1024 * 1024 };
struct TbProfiles {
    TbProfile items[TB_PROFILES]; size_t count;
    char recent[TB_PROFILE_RECENT][TB_PROFILE_NAME]; size_t recent_count;
    char directory[4000], path[4096], error[192];
    struct stat stamp;
    bool existed, blocked;
    bool schema3;
    json_object *setups[TB_PROFILES]; /* Immutable, owned records, indexed with items. */
};
static bool fail(TbProfiles *p, const char *text)
{ (void)snprintf(p->error, sizeof p->error, "%s", text); return false; }
static bool clean(const char *text, size_t size, bool empty)
{
    size_t length = strnlen(text, size);
    if (length == size || (!length && !empty)) { return false; }
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c < 32 || c == 127) { return false; }
        if (c < 128) { continue; }
        unsigned count = c >= 0xc2 && c <= 0xdf ? 1 : c >= 0xe0 && c <= 0xef ? 2 : c >= 0xf0 && c <= 0xf4 ? 3 : 0;
        if (!count || length - i - 1 < count) { return false; }
        unsigned value = c & (0x7fU >> (count + 1));
        for (unsigned j = 0; j < count; ++j) {
            c = (unsigned char)text[++i]; if ((c & 0xc0U) != 0x80) { return false; }
            value = value * 64 + (c & 0x3fU);
        }
        if (value < (count == 1 ? 0xa0U : count == 2 ? 0x800U : 0x10000U) || value > 0x10ffff || (value >= 0xd800 && value <= 0xdfff)) { return false; }
    }
    return true;
}
void tb_profile_defaults(TbProfile *p, int kind)
{
    memset(p, 0, sizeof *p); p->kind = kind; p->start_server = true;
    (void)snprintf(p->gdb, sizeof p->gdb, "%s", kind == 1 ? "arm-none-eabi-gdb" : "gdb");
    (void)snprintf(p->openocd, sizeof p->openocd, "openocd");
    (void)snprintf(p->endpoint, sizeof p->endpoint, "localhost:3333");
    (void)snprintf(p->protocol, sizeof p->protocol, "remote");
}
static bool words_valid(const TbProfileWords *words)
{
    if (words->count > TB_PROFILE_WORDS) { return false; }
    for (size_t i = 0; i < words->count; ++i) { if (!clean(words->values[i], TB_PROFILE_WORD, true)) { return false; } }
    return true;
}
static bool words_equal(const TbProfileWords *a, const TbProfileWords *b)
{
    if (a->count != b->count) { return false; }
    for (size_t i = 0; i < a->count; ++i) { if (strcmp(a->values[i], b->values[i]) != 0) { return false; } } return true;
}
uint32_t tb_profile_differences(const TbProfile *a, const TbProfile *b)
{
    uint32_t changes = 0;
    if (a->kind != b->kind || (!a->kind && !b->kind && a->attach != b->attach) ||
        (a->kind == 1 && b->kind == 1 && a->start_server != b->start_server)) { changes |= TB_PROFILE_MODE; }
    if (strcmp(a->gdb, b->gdb) != 0 || (a->kind == 1 && b->kind == 1 && a->start_server && b->start_server && strcmp(a->openocd, b->openocd) != 0)) { changes |= TB_PROFILE_TOOLS; }
    if (strcmp(a->executable, b->executable) != 0) { changes |= TB_PROFILE_ELF; }
    if (!a->kind && !b->kind && !a->attach && !b->attach) {
        if (!words_equal(&a->arguments, &b->arguments)) { changes |= TB_PROFILE_ARGUMENTS; }
        if (strcmp(a->directory, b->directory) != 0) { changes |= TB_PROFILE_DIRECTORY; }
    }
    if (a->kind && b->kind && (strcmp(a->endpoint, b->endpoint) != 0 || strcmp(a->protocol, b->protocol) != 0)) { changes |= TB_PROFILE_ENDPOINT; }
    if (a->kind == 1 && b->kind == 1 && a->start_server && b->start_server) {
        if (!words_equal(&a->search, &b->search)) { changes |= TB_PROFILE_SEARCH; }
        bool same = words_equal(&a->scripts, &b->scripts);
        if (!a->scripts.count && !b->scripts.count) {
            same = a->adapter_target == b->adapter_target && (a->adapter_target ?
                strcmp(a->adapter, b->adapter) == 0 && strcmp(a->target, b->target) == 0 : strcmp(a->board, b->board) == 0);
        }
        if (!same) { changes |= TB_PROFILE_SCRIPTS; }
    }
    if (a->directory_count != b->directory_count || a->rule_count != b->rule_count) { changes |= TB_PROFILE_SOURCE; }
    else {
        for (size_t i = 0; i < a->directory_count; ++i) { if (strcmp(a->directories[i], b->directories[i]) != 0) { changes |= TB_PROFILE_SOURCE; } }
        for (size_t i = 0; i < a->rule_count; ++i) {
            if (strcmp(a->rules[i].from, b->rules[i].from) != 0 || strcmp(a->rules[i].to, b->rules[i].to) != 0) { changes |= TB_PROFILE_SOURCE; }
        }
    }
    return changes;
}
static bool relative_path(const char *path) { return *path && *path != '/'; }
bool tb_profile_relative_paths(const TbProfile *p)
{
    if (relative_path(p->executable) || (!p->kind && !p->attach && relative_path(p->directory)) ||
        (strchr(p->gdb, '/') && relative_path(p->gdb))) { return true; }
    for (size_t i = 0; i < p->directory_count; ++i) { if (relative_path(p->directories[i])) { return true; } }
    for (size_t i = 0; i < p->rule_count; ++i) { if (relative_path(p->rules[i].to)) { return true; } }
    if (p->kind != 1 || !p->start_server) { return false; }
    if (strchr(p->openocd, '/') && relative_path(p->openocd)) { return true; }
    for (size_t i = 0; i < p->search.count; ++i) { if (relative_path(p->search.values[i])) { return true; } }
    if (p->scripts.count) {
        for (size_t i = 0; i < p->scripts.count; ++i) { if (relative_path(p->scripts.values[i])) { return true; } }
    } else if (p->adapter_target ? relative_path(p->adapter) || relative_path(p->target) : relative_path(p->board)) { return true; }
    return false;
}
void tb_profile_image_stamp(const TbProfile *p, const char *directory, char stamp[256])
{
    if (!*p->executable) { (void)snprintf(stamp, 256, "none"); return; }
    char path[TB_SETUP_PATH + TB_PROFILE_PATH];
    int n = snprintf(path, sizeof path, "%s%s%s", p->executable[0] == '/' ? "" : directory,
                     p->executable[0] == '/' ? "" : "/", p->executable);
    struct stat st;
    if (n < 0 || (size_t)n >= sizeof path || stat(path, &st) != 0 || !S_ISREG(st.st_mode)) {
        (void)snprintf(stamp, 256, "unavailable"); return;
    }
    (void)snprintf(stamp, 256, "file:%ju:%ju:%jd:%jd:%ld:%jd:%ld", (uintmax_t)st.st_dev,
                   (uintmax_t)st.st_ino, (intmax_t)st.st_size, (intmax_t)st.st_mtim.tv_sec,
                   st.st_mtim.tv_nsec, (intmax_t)st.st_ctim.tv_sec, st.st_ctim.tv_nsec);
}
bool tb_profile_valid(const TbProfile *p)
{
    if (!clean(p->name, sizeof p->name, false) || p->kind < 0 || p->kind > 2 ||
        p->directory_count > TB_PROFILE_SOURCES || p->rule_count > TB_PROFILE_SOURCES ||
        !words_valid(&p->arguments) || !words_valid(&p->scripts) || !words_valid(&p->search)) { return false; }
    const char *paths[] = {p->executable, p->directory, p->gdb, p->openocd, p->board, p->adapter, p->target};
    for (size_t i = 0; i < sizeof paths / sizeof paths[0]; ++i) { if (!clean(paths[i], TB_PROFILE_PATH, true)) { return false; } }
    if (!*p->gdb || !clean(p->endpoint, sizeof p->endpoint, true) ||
        (strcmp(p->protocol, "remote") != 0 && strcmp(p->protocol, "extended-remote") != 0)) { return false; }
    for (size_t i = 0; i < p->scripts.count; ++i) { if (!*p->scripts.values[i]) { return false; } }
    for (size_t i = 0; i < p->search.count; ++i) { if (!*p->search.values[i]) { return false; } }
    for (size_t i = 0; i < p->directory_count; ++i) {
        if (!clean(p->directories[i], TB_PROFILE_PATH, false)) { return false; }
        for (size_t j = 0; j < i; ++j) { if (strcmp(p->directories[i], p->directories[j]) == 0) { return false; } }
    }
    for (size_t i = 0; i < p->rule_count; ++i) {
        if (!clean(p->rules[i].from, TB_PROFILE_PATH, false) || !clean(p->rules[i].to, TB_PROFILE_PATH, false)) { return false; }
        for (size_t j = 0; j < i; ++j) { if (strcmp(p->rules[i].from, p->rules[j].from) == 0) { return false; } }
    }
    return true;
}
TbProfiles *tb_profiles_create(void)
{
    TbProfiles *p = calloc(1, sizeof *p); if (!p) { return NULL; }
    const char *base = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    int length;
    if (base && *base == '/') { length = snprintf(p->directory, sizeof p->directory, "%s/the-bugger", base); }
    else if (home && *home == '/') { length = snprintf(p->directory, sizeof p->directory, "%s/.config/the-bugger", home); }
    else { length = -1; }
    if (length < 0 || (size_t)length >= sizeof p->directory - 15) {
        p->blocked = true; (void)fail(p, "A usable absolute XDG_CONFIG_HOME or HOME is required for profiles.");
    } else { (void)snprintf(p->path, sizeof p->path, "%s/profiles.json", p->directory); }
    return p;
}
void tb_profiles_destroy(TbProfiles *p)
{
    if (!p) { return; }
    for (size_t i = 0; i < TB_PROFILES; ++i) { json_object_put(p->setups[i]); }
    free(p);
}
size_t tb_profiles_count(const TbProfiles *p) { return p->count; }
const TbProfile *tb_profiles_at(const TbProfiles *p, size_t i) { return i < p->count ? &p->items[i] : NULL; }
const TbProfile *tb_profiles_find(const TbProfiles *p, const char *name)
{
    if (!name) { return NULL; }
    for (size_t i = 0; i < p->count; ++i) { if (strcmp(p->items[i].name, name) == 0) { return &p->items[i]; } }
    return NULL;
}
const char *tb_profiles_recent(const TbProfiles *p, size_t i) { return i < p->recent_count ? p->recent[i] : NULL; }
const char *tb_profiles_error(const TbProfiles *p) { return p->error; }
const char *tb_profiles_path(const TbProfiles *p) { return p->path; }
bool tb_profiles_read_only(const TbProfiles *p) { return p->blocked; }

/* A strict schema; obsolete v1 personal keys are explicitly ignored on read. */
static json_object *member(json_object *object, const char *name, enum json_type type)
{
    json_object *value = NULL;
    return json_object_object_get_ex(object, name, &value) && json_object_is_type(value, type) ? value : NULL;
}
static bool fields(json_object *object, const char *const names[], size_t count)
{
    if (!json_object_is_type(object, json_type_object)) { return false; }
    json_object_object_foreach(object, key, value) {
        (void)value; bool found = false;
        for (size_t i = 0; i < count; ++i) { found = found || strcmp(key, names[i]) == 0; }
        if (!found) { return false; }
    }
    return json_object_object_length(object) == (int)count;
}
static bool string_get(json_object *object, const char *key, char *out, size_t size)
{
    json_object *v = member(object, key, json_type_string); if (!v) { return false; }
    const char *text = json_object_get_string(v); size_t length = (size_t)json_object_get_string_len(v);
    if (length >= size || strlen(text) != length || !clean(text, size, true)) { return false; }
    memcpy(out, text, length + 1); return true;
}
static bool array_get(json_object *object, const char *key, TbProfileWords *words)
{
    json_object *array = member(object, key, json_type_array);
    if (!array || json_object_array_length(array) > TB_PROFILE_WORDS) { return false; }
    words->count = json_object_array_length(array);
    for (size_t i = 0; i < words->count; ++i) {
        json_object *value = json_object_array_get_idx(array, i);
        if (!json_object_is_type(value, json_type_string)) { return false; }
        const char *s = json_object_get_string(value); size_t n = (size_t)json_object_get_string_len(value);
        if (n >= TB_PROFILE_WORD || strlen(s) != n || !clean(s, TB_PROFILE_WORD, true)) { return false; }
        memcpy(words->values[i], s, n + 1);
    }
    return true;
}
static json_object *array_make(const TbProfileWords *words)
{
    json_object *array = json_object_new_array(); if (!array) { return NULL; }
    for (size_t i = 0; i < words->count; ++i) {
        json_object *value = json_object_new_string(words->values[i]);
        if (!value || json_object_array_add(array, value) != 0) { json_object_put(value); json_object_put(array); return NULL; }
    }
    return array;
}
static const char *const profile_fields[] = {"name", "kind", "attach", "start_server", "adapter_target", "executable", "directory", "gdb", "openocd", "endpoint", "board", "adapter", "target", "protocol", "arguments", "scripts", "search", "directories", "mappings", "keys"};
static bool profile_get(json_object *o, TbProfile *p)
{
    /* Legacy v1 profile keys are accepted only for reading and never applied. */
    json_object *legacy = NULL;
    size_t count = sizeof profile_fields / sizeof profile_fields[0];
    if (!fields(o, profile_fields, json_object_object_get_ex(o, "keys", &legacy) ? count : count - 1)) { return false; }
    json_object *kind = member(o, "kind", json_type_int);
    if (!kind || json_object_get_int64(kind) < 0 || json_object_get_int64(kind) > 2) { return false; }
    p->kind = json_object_get_int(kind);
    bool *flags[] = {&p->attach, &p->start_server, &p->adapter_target};
    for (size_t i = 0; i < 3; ++i) {
        json_object *value = member(o, profile_fields[2 + i], json_type_boolean); if (!value) { return false; }
        *flags[i] = json_object_get_boolean(value) != 0;
    }
    char *strings[] = {p->name, p->executable, p->directory, p->gdb, p->openocd, p->endpoint, p->board, p->adapter, p->target, p->protocol};
    const char *names[] = {"name", "executable", "directory", "gdb", "openocd", "endpoint", "board", "adapter", "target", "protocol"};
    const size_t sizes[] = {sizeof p->name, sizeof p->executable, sizeof p->directory, sizeof p->gdb, sizeof p->openocd, sizeof p->endpoint, sizeof p->board, sizeof p->adapter, sizeof p->target, sizeof p->protocol};
    for (size_t i = 0; i < 10; ++i) { if (!string_get(o, names[i], strings[i], sizes[i])) { return false; } }
    if (!array_get(o, "arguments", &p->arguments) || !array_get(o, "scripts", &p->scripts) || !array_get(o, "search", &p->search)) { return false; }
    json_object *dirs = member(o, "directories", json_type_array), *maps = member(o, "mappings", json_type_array);
    if (!dirs || !maps || json_object_array_length(dirs) > TB_PROFILE_SOURCES || json_object_array_length(maps) > TB_PROFILE_SOURCES) { return false; }
    p->directory_count = json_object_array_length(dirs); p->rule_count = json_object_array_length(maps);
    for (size_t i = 0; i < p->directory_count; ++i) {
        json_object *v = json_object_array_get_idx(dirs, i);
        if (!json_object_is_type(v, json_type_string) || (size_t)json_object_get_string_len(v) >= TB_PROFILE_PATH ||
            strlen(json_object_get_string(v)) != (size_t)json_object_get_string_len(v)) { return false; }
        (void)snprintf(p->directories[i], TB_PROFILE_PATH, "%s", json_object_get_string(v));
    }
    for (size_t i = 0; i < p->rule_count; ++i) {
        static const char *const names_map[] = {"from", "to"}; json_object *v = json_object_array_get_idx(maps, i);
        if (!fields(v, names_map, 2) || !string_get(v, "from", p->rules[i].from, TB_PROFILE_PATH) || !string_get(v, "to", p->rules[i].to, TB_PROFILE_PATH)) { return false; }
    }
    return tb_profile_valid(p);
}
static bool add(json_object *object, const char *key, json_object *value)
{
    if (!value) { return false; }
    if (json_object_object_add(object, key, value) != 0) { json_object_put(value); return false; }
    return true;
}
static json_object *profile_make(const TbProfile *p)
{
    json_object *o = json_object_new_object(); if (!o) { return NULL; }
    bool ok = add(o, "name", json_object_new_string(p->name)) && add(o, "kind", json_object_new_int(p->kind)) &&
              add(o, "attach", json_object_new_boolean(p->attach)) && add(o, "start_server", json_object_new_boolean(p->start_server)) && add(o, "adapter_target", json_object_new_boolean(p->adapter_target));
    const char *strings[] = {p->executable, p->directory, p->gdb, p->openocd, p->endpoint, p->board, p->adapter, p->target, p->protocol};
    for (size_t i = 0; ok && i < 9; ++i) { ok = add(o, profile_fields[5 + i], json_object_new_string(strings[i])); }
    ok = ok && add(o, "arguments", array_make(&p->arguments)) && add(o, "scripts", array_make(&p->scripts)) && add(o, "search", array_make(&p->search));
    json_object *dirs = json_object_new_array(), *maps = json_object_new_array();
    if (!dirs || !maps) { ok = false; }
    for (size_t i = 0; ok && i < p->directory_count; ++i) { json_object *v = json_object_new_string(p->directories[i]); if (!v || json_object_array_add(dirs, v) != 0) { json_object_put(v); ok = false; } }
    for (size_t i = 0; ok && i < p->rule_count; ++i) {
        json_object *v = json_object_new_object();
        if (!v || !add(v, "from", json_object_new_string(p->rules[i].from)) || !add(v, "to", json_object_new_string(p->rules[i].to)) || json_object_array_add(maps, v) != 0) { json_object_put(v); ok = false; }
    }
    if (!ok) { json_object_put(dirs); json_object_put(maps); json_object_put(o); return NULL; }
    /* Ownership transfers even if a subsequent field fails. */
    if (!add(o, "directories", dirs)) { json_object_put(maps); json_object_put(o); return NULL; }
    if (!add(o, "mappings", maps)) { json_object_put(o); return NULL; }
    return o;
}
bool tb_profiles_load(TbProfiles *p)
{
    if (!*p->path) { return false; }
    int fd = open(p->path, O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
    if (fd < 0 && errno == ENOENT) {
        for (size_t i = 0; i < TB_PROFILES; ++i) { json_object_put(p->setups[i]); p->setups[i] = NULL; }
        p->count = p->recent_count = 0; p->schema3 = false; p->existed = p->blocked = false; p->error[0] = 0; return true;
    }
    struct stat info;
    if (fd < 0 || fstat(fd, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != getuid() || info.st_size < 0 || info.st_size > FILE_LIMIT) {
        if (fd >= 0) { (void)close(fd); } p->blocked = true; return fail(p, "Cannot read owned regular profiles.json (maximum 1 MiB). File preserved.");
    }
    char *buffer = malloc((size_t)info.st_size + 1); size_t used = 0;
    if (!buffer) { (void)close(fd); return fail(p, "Profile allocation failed."); }
    while (used < (size_t)info.st_size) {
        ssize_t n = read(fd, buffer + used, (size_t)info.st_size - used);
        if (n < 0 && errno == EINTR) { continue; }
        if (n <= 0) { break; } used += (size_t)n;
    }
    (void)close(fd); buffer[used] = 0;
    json_tokener *tokener = json_tokener_new_ex(16); json_object *root = NULL;
    if (tokener) { json_tokener_set_flags(tokener, JSON_TOKENER_STRICT | JSON_TOKENER_VALIDATE_UTF8); root = json_tokener_parse_ex(tokener, buffer, (int)used); }
    bool ok = tokener && root && json_tokener_get_error(tokener) == json_tokener_success && used == (size_t)info.st_size;
    size_t end = tokener ? json_tokener_get_parse_end(tokener) : 0;
    for (size_t i = end; ok && i < used; ++i) { ok = isspace((unsigned char)buffer[i]) != 0; }
    if (tokener) { json_tokener_free(tokener); } free(buffer);
    static const char *const names[] = {"version", "profiles", "recent", "setups"};
    json_object *version = ok ? member(root, "version", json_type_int) : NULL;
    json_object *items = ok ? member(root, "profiles", json_type_array) : NULL;
    json_object *recent = ok ? member(root, "recent", json_type_array) : NULL;
    int64_t schema = version ? json_object_get_int64(version) : 0;
    json_object *setups = ok ? member(root, "setups", json_type_array) : NULL;
    bool recovery = false;
    ok = ok && fields(root, names, schema == 3 ? 4 : 3) && version && schema >= 1 && schema <= 3 && items && recent &&
         json_object_array_length(items) <= TB_PROFILES && json_object_array_length(recent) <= TB_PROFILE_RECENT;
    if (ok && schema == 3 && !setups) { recovery = true; }
    TbProfiles *next = ok ? calloc(1, sizeof *next) : NULL; ok = ok && next;
    if (ok) {
        next->count = json_object_array_length(items); next->recent_count = json_object_array_length(recent);
        for (size_t i = 0; ok && i < next->count; ++i) {
            ok = profile_get(json_object_array_get_idx(items, i), &next->items[i]);
            for (size_t j = 0; ok && j < i; ++j) { ok = strcmp(next->items[i].name, next->items[j].name) != 0; }
        }
        for (size_t i = 0; ok && i < next->recent_count; ++i) {
            json_object *v = json_object_array_get_idx(recent, i);
            ok = json_object_is_type(v, json_type_string);
            if (!ok) { break; }
            const char *name = json_object_get_string(v);
            ok = strlen(name) == (size_t)json_object_get_string_len(v) && strlen(name) < TB_PROFILE_NAME && tb_profiles_find(next, name);
            for (size_t j = 0; ok && j < i; ++j) { ok = strcmp(next->recent[j], name) != 0; }
            if (ok) { (void)snprintf(next->recent[i], TB_PROFILE_NAME, "%s", name); }
        }
    }
    if (ok && schema == 3 && setups) {
        size_t count = json_object_array_length(setups);
        if (count > TB_PROFILES) { recovery = true; }
        bool seen[TB_PROFILES] = {0};
        for (size_t i = 0; i < count; ++i) {
            json_object *record = json_object_array_get_idx(setups, i);
            static const char *const record_names[] = {"profile", "connection", "launch_directory", "definition", "elf_metadata"};
            json_object *metadata = NULL; bool has_metadata = json_object_object_get_ex(record, "elf_metadata", &metadata);
            char stamp[256];
            char name[TB_PROFILE_NAME], directory[TB_SETUP_PATH]; TbProfile connection = {0};
            json_object *definition = member(record, "definition", json_type_object);
            bool valid = fields(record, record_names, has_metadata ? 5 : 4) &&
                         (!has_metadata || string_get(record, "elf_metadata", stamp, sizeof stamp)) &&
                         string_get(record, "profile", name, sizeof name) &&
                         string_get(record, "launch_directory", directory, sizeof directory) && *directory == '/' &&
                         profile_get(member(record, "connection", json_type_object), &connection);
            size_t index = 0;
            if (valid) { while (index < next->count && strcmp(next->items[index].name, name) != 0) { ++index; } }
            if (!valid || index == next->count) { recovery = true; }
            else if (seen[index]) {
                recovery = true; json_object_put(next->setups[index]); next->setups[index] = NULL;
            } else {
                TbSetup *setup = definition ? tb_setup_decode(definition) : NULL;
                if (setup) { next->setups[index] = json_object_get(record); } else { recovery = true; }
                free(setup);
            }
            if (valid && index < next->count) { seen[index] = true; }
        }
    }
    json_object_put(root);
    if (ok) {
        for (size_t i = 0; i < TB_PROFILES; ++i) { json_object_put(p->setups[i]); p->setups[i] = next->setups[i]; next->setups[i] = NULL; }
        memcpy(p->items, next->items, sizeof p->items); memcpy(p->recent, next->recent, sizeof p->recent);
        p->count = next->count; p->recent_count = next->recent_count; p->stamp = info; p->existed = true;
        p->schema3 = schema == 3; p->blocked = recovery; p->error[0] = 0;
        if (recovery) { (void)fail(p, "Read-only recovery: malformed setup records rejected. Repair profiles.json, then Reload before any writes."); }
    }
    tb_profiles_destroy(next);
    if (!ok) { p->blocked = true; return fail(p, "Invalid/unsupported profile schema or JSON; file preserved. Correct it, then Reload."); }
    return true;
}
static bool mkdirs(const char *path)
{
    char copy[4096]; (void)snprintf(copy, sizeof copy, "%s", path);
    for (char *p = copy + 1; ; ++p) {
        if (*p && *p != '/') { continue; }
        char c = *p; *p = 0;
        if (mkdir(copy, 0700) != 0 && errno != EEXIST) { return false; }
        struct stat st; if (stat(copy, &st) != 0 || !S_ISDIR(st.st_mode)) { return false; }
        *p = c; if (!c) { break; }
    }
    return true;
}
static bool unchanged(TbProfiles *p)
{
    struct stat s;
    if (lstat(p->path, &s) != 0) { return errno == ENOENT && !p->existed; }
    return p->existed && S_ISREG(s.st_mode) && s.st_uid == getuid() && s.st_ino == p->stamp.st_ino &&
           s.st_dev == p->stamp.st_dev && s.st_size == p->stamp.st_size &&
           s.st_mtim.tv_sec == p->stamp.st_mtim.tv_sec && s.st_mtim.tv_nsec == p->stamp.st_mtim.tv_nsec;
}
static bool save(TbProfiles *p)
{
    if (p->blocked || !*p->path) { return fail(p, "Profile writes blocked; correct/reload the existing file first."); }
    if (!mkdirs(p->directory)) { return fail(p, "Cannot create profile directory."); }
    int dir = open(p->directory, O_RDONLY | O_DIRECTORY | O_CLOEXEC | O_NOFOLLOW);
    if (dir < 0) { return fail(p, "Cannot open profile directory."); }
    int lock = openat(dir, ".lock", O_RDWR | O_CREAT | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK, 0600);
    struct stat st;
    if (lock < 0 || fstat(lock, &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != getuid() || flock(lock, LOCK_EX | LOCK_NB) != 0) {
        if (lock >= 0) { (void)close(lock); } (void)close(dir); return fail(p, "Profile store is locked or inaccessible.");
    }
    bool ok = unchanged(p);
    if (!ok) { (void)fail(p, "Profiles changed on disk. Reload before saving; existing file preserved."); }
    json_object *root = json_object_new_object(), *items = json_object_new_array(), *recent = json_object_new_array();
    if (!root || !items || !recent) { ok = false; }
    for (size_t i = 0; ok && i < p->count; ++i) {
        json_object *o = profile_make(&p->items[i]); if (!o || json_object_array_add(items, o) != 0) { json_object_put(o); ok = false; }
    }
    for (size_t i = 0; ok && i < p->recent_count; ++i) {
        json_object *v = json_object_new_string(p->recent[i]); if (!v || json_object_array_add(recent, v) != 0) { json_object_put(v); ok = false; }
    }
    if (ok) { ok = add(root, "version", json_object_new_int(p->schema3 ? 3 : 2)); }
    if (ok) { ok = add(root, "profiles", items); items = NULL; }
    if (ok) { ok = add(root, "recent", recent); recent = NULL; }
    if (ok && p->schema3) {
        json_object *setups = json_object_new_array(); ok = setups != NULL;
        for (size_t i = 0; ok && i < p->count; ++i) {
            if (!p->setups[i]) { continue; }
            json_object *record = json_object_get(p->setups[i]);
            if (json_object_array_add(setups, record) != 0) { json_object_put(record); ok = false; }
        }
        if (ok) { ok = add(root, "setups", setups); } else { json_object_put(setups); }
    }
    json_object_put(items); json_object_put(recent);
    char temporary[4096]; (void)snprintf(temporary, sizeof temporary, "%s/.profiles-XXXXXX", p->directory);
    int fd = -1;
    if (ok) { fd = mkstemp(temporary); ok = fd >= 0 && fcntl(fd, F_SETFD, FD_CLOEXEC) == 0 && fchmod(fd, 0600) == 0; }
    if (ok) {
        const char *text = json_object_to_json_string_ext(root, JSON_C_TO_STRING_PRETTY); size_t length = text ? strlen(text) : 0, at = 0;
        if (!text || length > FILE_LIMIT) { ok = fail(p, "Profile serialization failed or exceeds the 1 MiB file limit."); }
        while (ok && at < length) { ssize_t n = write(fd, text + at, length - at); if (n < 0 && errno == EINTR) { continue; } if (n <= 0) { ok = false; break; } at += (size_t)n; }
        if (ok) { ok = fsync(fd) == 0 && fstat(fd, &st) == 0; }
    }
    if (fd >= 0 && close(fd) != 0) { ok = false; }
    if (ok) { ok = rename(temporary, p->path) == 0; }
    if (ok) {
        p->stamp = st; p->existed = true;
        /* The rename has committed. Do not report rollback if directory fsync
         * fails; retain an explicit durability warning instead. */
        p->error[0] = 0;
        if (fsync(dir) != 0) { (void)fail(p, "Profiles saved; directory sync failed, crash durability is uncertain."); }
    } else { if (fd >= 0) { (void)unlink(temporary); } if (!*p->error) { (void)fail(p, "Profile write failed; previous file preserved."); } }
    json_object_put(root); (void)close(lock); (void)close(dir); return ok;
}
static void recent_remove(TbProfiles *p, const char *name)
{
    for (size_t i = 0; i < p->recent_count; ++i) { if (strcmp(p->recent[i], name) == 0) { memmove(p->recent[i], p->recent[i + 1], (p->recent_count - i - 1) * sizeof p->recent[0]); --p->recent_count; break; } }
}
static bool transaction(TbProfiles *p, TbProfiles *next)
{
    next->error[0] = 0; bool ok = save(next);
    if (ok) {
        for (size_t i = 0; i < TB_PROFILES; ++i) { json_object_put(p->setups[i]); }
        memcpy(p, next, sizeof *p); memset(next->setups, 0, sizeof next->setups);
    } else { (void)fail(p, next->error); }
    tb_profiles_destroy(next); return ok;
}
static TbProfiles *clone(TbProfiles *p)
{
    TbProfiles *next = malloc(sizeof *next);
    if (!next) { (void)fail(p, "Profile allocation failed."); return NULL; }
    memcpy(next, p, sizeof *next);
    for (size_t i = 0; i < TB_PROFILES; ++i) { next->setups[i] = json_object_get(p->setups[i]); }
    return next;
}
bool tb_profiles_put(TbProfiles *p, const TbProfile *profile, bool replace)
{
    if (!tb_profile_valid(profile)) { return fail(p, "Invalid profile fields or bounds."); }
    size_t index = 0; while (index < p->count && strcmp(p->items[index].name, profile->name) != 0) { ++index; }
    if (index < p->count && !replace) { return fail(p, "A profile already has that name; confirm replacement."); }
    if (index == TB_PROFILES) { return fail(p, "At most 16 named profiles are supported."); }
    TbProfiles *next = clone(p); if (!next) { return false; }
    next->items[index] = *profile; if (index == next->count) { ++next->count; }
    return transaction(p, next);
}
bool tb_profiles_remove(TbProfiles *p, const char *name)
{
    size_t index = 0; while (index < p->count && strcmp(p->items[index].name, name) != 0) { ++index; }
    if (index == p->count) { return fail(p, "Profile no longer exists."); }
    TbProfiles *next = clone(p); if (!next) { return false; }
    memmove(&next->items[index], &next->items[index + 1], (next->count - index - 1) * sizeof next->items[0]);
    json_object_put(next->setups[index]);
    memmove(&next->setups[index], &next->setups[index + 1], (next->count - index - 1) * sizeof next->setups[0]);
    next->setups[next->count - 1] = NULL;
    --next->count; recent_remove(next, name); return transaction(p, next);
}
bool tb_profiles_touch(TbProfiles *p, const char *name)
{
    if (!tb_profiles_find(p, name)) { return fail(p, "Save a named profile before recording it as recent."); }
    TbProfiles *next = clone(p); if (!next) { return false; }
    recent_remove(next, name);
    if (next->recent_count == TB_PROFILE_RECENT) { --next->recent_count; }
    memmove(next->recent[1], next->recent[0], next->recent_count * sizeof next->recent[0]);
    (void)snprintf(next->recent[0], TB_PROFILE_NAME, "%s", name); ++next->recent_count;
    return transaction(p, next);
}
static size_t profile_index(const TbProfiles *p, const char *name)
{
    size_t i = 0; while (i < p->count && (!name || strcmp(p->items[i].name, name) != 0)) { ++i; } return i;
}
TbSetup *tb_profiles_setup(const TbProfiles *p, const char *name)
{
    size_t i = profile_index(p, name);
    return i < p->count && p->setups[i] ? tb_setup_decode(member(p->setups[i], "definition", json_type_object)) : NULL;
}
bool tb_profiles_has_setup(const TbProfiles *p, const char *name)
{
    size_t i = profile_index(p, name); return i < p->count && p->setups[i];
}
bool tb_profiles_setup_connection(const TbProfiles *p, const char *name, TbProfile *connection, char directory[TB_SETUP_PATH])
{
    size_t i = profile_index(p, name);
    return i < p->count && p->setups[i] && profile_get(member(p->setups[i], "connection", json_type_object), connection) &&
           string_get(p->setups[i], "launch_directory", directory, TB_SETUP_PATH);
}
bool tb_profiles_setup_stamp(const TbProfiles *p, const char *name, char stamp[256])
{
    size_t i = profile_index(p, name); stamp[0] = 0;
    return i < p->count && p->setups[i] && string_get(p->setups[i], "elf_metadata", stamp, 256);
}
bool tb_profiles_save_setup(TbProfiles *p, const char *name, const TbSetup *setup, const TbProfile *connection, const char *directory)
{
    size_t i = profile_index(p, name);
    if (i == p->count) { return fail(p, "Save a connection profile before saving setup."); }
    if (!tb_setup_valid(setup) || !tb_profile_valid(connection) || !directory || *directory != '/' || !clean(directory, TB_SETUP_PATH, false)) {
        return fail(p, "Invalid setup definitions or connection snapshot.");
    }
    char stamp[256]; tb_profile_image_stamp(connection, directory, stamp);
    json_object *record = json_object_new_object();
    if (!record || !add(record, "elf_metadata", json_object_new_string(stamp)) || !add(record, "profile", json_object_new_string(name)) || !add(record, "connection", profile_make(connection)) ||
        !add(record, "launch_directory", json_object_new_string(directory)) || !add(record, "definition", tb_setup_encode(setup))) {
        json_object_put(record); return fail(p, "Setup allocation/serialization failed.");
    }
    TbProfiles *next = clone(p); if (!next) { json_object_put(record); return false; }
    json_object_put(next->setups[i]); next->setups[i] = record; next->schema3 = true;
    return transaction(p, next);
}
bool tb_profiles_duplicate(TbProfiles *p, const char *name, const char *new_name)
{
    size_t i = profile_index(p, name);
    if (i == p->count || !new_name || !clean(new_name, TB_PROFILE_NAME, false) || tb_profiles_find(p, new_name) || p->count == TB_PROFILES) {
        return fail(p, "Choose an existing profile and a unique name; at most 16 profiles are supported.");
    }
    TbProfiles *next = clone(p); if (!next) { return false; }
    size_t out = next->count++; next->items[out] = next->items[i];
    (void)snprintf(next->items[out].name, TB_PROFILE_NAME, "%s", new_name);
    if (next->setups[i]) {
        json_object *record = NULL;
        if (json_object_deep_copy(next->setups[i], &record, NULL) != 0 || !record || !add(record, "profile", json_object_new_string(new_name))) {
            json_object_put(record); tb_profiles_destroy(next); return fail(p, "Setup duplication failed.");
        }
        next->setups[out] = record;
    }
    return transaction(p, next);
}
