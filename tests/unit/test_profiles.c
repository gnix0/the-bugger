#define _POSIX_C_SOURCE 200809L
#include "profiles.h"
#include "../support/check.h"
#include <dirent.h>
#include <json-c/json.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <signal.h>
#include <unistd.h>

static void remove_tree(const char *directory)
{
    DIR *dir = opendir(directory); CHECK(dir);
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) { continue; }
        size_t size = strlen(directory) + strlen(entry->d_name) + 2; char *path = malloc(size); CHECK(path);
        (void)snprintf(path, size, "%s/%s", directory, entry->d_name);
        struct stat st; CHECK(lstat(path, &st) == 0);
        if (S_ISDIR(st.st_mode)) { remove_tree(path); } else { CHECK(unlink(path) == 0); }
        free(path);
    }
    CHECK(closedir(dir) == 0); CHECK(rmdir(directory) == 0);
}
static void write_file(const char *path, const char *text)
{
    FILE *file = fopen(path, "w"); CHECK(file); CHECK(fputs(text, file) >= 0); CHECK(fclose(file) == 0);
}
static char *read_file(const char *path)
{
    FILE *file = fopen(path, "r"); CHECK(file); CHECK(fseek(file, 0, SEEK_END) == 0);
    long length = ftell(file); CHECK(length >= 0); rewind(file);
    char *text = malloc((size_t)length + 1); CHECK(text);
    CHECK(fread(text, 1, (size_t)length, file) == (size_t)length); text[length] = 0; CHECK(fclose(file) == 0); return text;
}
static TbSetup *setup_fixture(void)
{
    TbSetup *s = calloc(1, sizeof *s); CHECK(s);
    s->next_id = 7; s->columns[0] = 3600; s->columns[1] = 3400; s->columns[2] = 3000; s->row_ratio = 6200;
    for (int i = 0; i < 6; ++i) {
        s->roots[i] = i; s->nodes[i] = (TbSetupNode){.used = true, .pane = i, .first = -1, .second = -1};
        TbSetupPane *p = &s->panes[i]; p->used = true; p->id = (uint64_t)i + 1;
        p->tab_count = 1; p->tabs[0] = p->selected = i; p->memory_bytes = 128; p->memory_group = 1;
    }
    return s;
}
static void setup_checks(TbProfiles *store)
{
    TbSetup *s = setup_fixture(); CHECK(tb_setup_valid(s));
    s->panes[0].source_count = 2;
    for (size_t i = 0; i < 2; ++i) {
        TbSetupSource *f = &s->panes[0].sources[i]; f->id = s->next_id++;
        (void)snprintf(f->file, sizeof f->file, "tree %zu/duplicate.c", i);
        (void)snprintf(f->full, sizeof f->full, "/missing tree %zu/duplicate.c", i);
    }
    s->panes[0].source_selected = s->panes[0].sources[1].id;
    (void)snprintf(s->panes[3].memory_expression, 256, "&buffer[4]");
    s->watch_count = 1; s->watches[0].id = s->next_id++; s->watches[0].format = 1;
    (void)snprintf(s->watches[0].expression, 256, "sample.member");
    s->break_count = 1; s->breaks[0] = (TbSetupBreak){.id = s->next_id++, .enabled = true, .ignore_set = true, .ignore = 13, .hardware = true};
    (void)snprintf(s->breaks[0].location, TB_SETUP_LOCATION, "a_symbol");
    (void)snprintf(s->breaks[0].condition, 512, "value == 3");
    s->breaks[0].mapping = (TbSetupMapping){.path = "/native/image", .identity = "reliable-current-file-identity", .bias = UINT64_MAX - 4096, .begin = UINT64_MAX - 1024, .end = UINT64_MAX};
    json_object *encoded = tb_setup_encode(s); CHECK(encoded);
    TbSetup *decoded = tb_setup_decode(encoded); CHECK(decoded && tb_setup_equal(s, decoded));
    ++decoded->next_id; decoded->nodes[63].ratio = 123; decoded->nodes[0].ratio = 456;
    CHECK(tb_setup_equal(s, decoded)); free(decoded);
    json_object *panes = NULL; CHECK(json_object_object_get_ex(encoded, "panes", &panes));
    json_object *pane = json_object_array_get_idx(panes, 0);
    CHECK(json_object_object_add(pane, "source_selected", json_object_new_int(999)) == 0);
    CHECK(!tb_setup_decode(encoded)); json_object_put(encoded);
    s->nodes[6] = (TbSetupNode){.used = true, .pane = -1, .first = 6, .second = 0, .ratio = 5000};
    s->roots[0] = 6; CHECK(!tb_setup_valid(s)); s->roots[0] = 0; memset(&s->nodes[6], 0, sizeof s->nodes[6]);
    uint64_t id = s->breaks[0].id; s->breaks[0].id = s->watches[0].id; CHECK(!tb_setup_valid(s)); s->breaks[0].id = id;

    const TbProfile *p = tb_profiles_find(store, "profile-0"); CHECK(p);
    char *before = read_file(tb_profiles_path(store));
    CHECK(!tb_profiles_save_setup(store, "missing", s, p, "/launch directory"));
    CHECK(tb_profiles_save_setup(store, p->name, s, p, "/launch directory"));
    char *saved = read_file(tb_profiles_path(store)); CHECK(strstr(saved, "\"version\":3") || strstr(saved, "\"version\": 3"));
    CHECK(strstr(saved, "\"setups\"") && !strstr(before, "\"setups\"")); free(before);
    TbProfiles *copy = tb_profiles_create(); CHECK(copy && tb_profiles_load(copy));
    char *unchanged_load = read_file(tb_profiles_path(store)); CHECK(strcmp(saved, unchanged_load) == 0); free(unchanged_load);
    decoded = tb_profiles_setup(copy, "profile-0"); CHECK(decoded && tb_setup_equal(s, decoded)); free(decoded);
    TbProfile *snapshot = calloc(1, sizeof *snapshot); char directory[TB_SETUP_PATH]; CHECK(snapshot);
    CHECK(tb_profiles_setup_connection(copy, "profile-0", snapshot, directory));
    CHECK(strcmp(directory, "/launch directory") == 0 && memcmp(snapshot, p, sizeof *p) == 0);
    CHECK(tb_profiles_duplicate(store, "profile-0", "independent"));
    s->breaks[0].enabled = false;
    CHECK(tb_profiles_save_setup(store, "profile-0", s, p, "/launch directory"));
    decoded = tb_profiles_setup(store, "independent"); CHECK(decoded && decoded->breaks[0].enabled); free(decoded);
    CHECK(tb_profiles_remove(store, "independent")); CHECK(!tb_profiles_setup(store, "independent"));
    /* Replacing connection data preserves the independently saved setup. */
    *snapshot = *p; (void)snprintf(snapshot->gdb, sizeof snapshot->gdb, "other-gdb");
    CHECK(tb_profiles_put(store, snapshot, true)); decoded = tb_profiles_setup(store, "profile-0");
    CHECK(decoded && tb_setup_equal(s, decoded)); free(decoded);
    CHECK(!tb_profiles_touch(copy, "profile-0")); /* The setup transaction participates in stale-writer detection. */
    CHECK(tb_profiles_load(copy));
    free(saved); saved = read_file(tb_profiles_path(store));
    json_object *root = json_tokener_parse(saved), *setups = NULL; CHECK(root);
    CHECK(json_object_object_get_ex(root, "setups", &setups));
    json_object *legacy_record = json_object_array_get_idx(setups, 0); char stamp[256];
    CHECK(tb_profiles_setup_stamp(copy, "profile-0", stamp) && *stamp);
    json_object_object_del(legacy_record, "elf_metadata");
    write_file(tb_profiles_path(store), json_object_to_json_string(root));
    CHECK(tb_profiles_load(copy) && !tb_profiles_read_only(copy) && !tb_profiles_setup_stamp(copy, "profile-0", stamp));
    CHECK(json_object_object_add(legacy_record, "elf_metadata", json_object_new_int(3)) == 0);
    write_file(tb_profiles_path(store), json_object_to_json_string(root));
    CHECK(tb_profiles_load(copy) && tb_profiles_read_only(copy) && !tb_profiles_has_setup(copy, "profile-0"));
    CHECK(json_object_object_add(legacy_record, "elf_metadata", json_object_new_string("unavailable")) == 0);
    CHECK(json_object_array_add(setups, json_object_new_object()) == 0);
    CHECK(json_object_array_add(setups, NULL) == 0);
    write_file(tb_profiles_path(store), json_object_to_json_string(root)); json_object_put(root);
    CHECK(tb_profiles_load(copy) && tb_profiles_read_only(copy));
    CHECK(tb_profiles_find(copy, "profile-0")); decoded = tb_profiles_setup(copy, "profile-0"); CHECK(decoded); free(decoded);
    char *broken = read_file(tb_profiles_path(store));
    CHECK(!tb_profiles_touch(copy, "profile-0") && !tb_profiles_remove(copy, "profile-0") &&
          !tb_profiles_put(copy, snapshot, true) && !tb_profiles_duplicate(copy, "profile-0", "blocked") &&
          !tb_profiles_save_setup(copy, "profile-0", s, snapshot, "/launch directory"));
    char *preserved = read_file(tb_profiles_path(store)); CHECK(strcmp(preserved, broken) == 0); free(preserved); free(broken);
    write_file(tb_profiles_path(store), saved); CHECK(tb_profiles_load(store) && tb_profiles_load(copy) && !tb_profiles_read_only(copy));
    /* Even a duplicate beyond the profile-count boundary must invalidate the
     * ambiguous association; valid connections remain usable. */
    root = json_tokener_parse(saved); CHECK(root && json_object_object_get_ex(root, "setups", &setups));
    json_object *duplicate = json_object_get(json_object_array_get_idx(setups, 0));
    for (size_t i = 1; i < TB_PROFILES; ++i) { CHECK(json_object_array_add(setups, json_object_new_object()) == 0); }
    CHECK(json_object_array_add(setups, duplicate) == 0);
    write_file(tb_profiles_path(store), json_object_to_json_string(root)); json_object_put(root);
    CHECK(tb_profiles_load(copy) && tb_profiles_read_only(copy) && !tb_profiles_has_setup(copy, "profile-0"));
    CHECK(tb_profiles_find(copy, "profile-0"));
    write_file(tb_profiles_path(store), saved); CHECK(tb_profiles_load(store) && tb_profiles_load(copy));
    /* Encode a valid maximum-sized definition. Actual encoding, including JSON
     * escaping, must fail as a whole without replacing the 1 MiB store. */
    s->break_count = TB_SETUP_BREAKS;
    for (size_t i = 0; i < s->break_count; ++i) {
        TbSetupBreak *b = &s->breaks[i]; memset(b, 0, sizeof *b); b->id = s->next_id++;
        memset(b->location, '\\', sizeof b->location - 1);
    }
    encoded = tb_setup_encode(s); CHECK(encoded);
    CHECK(strlen(json_object_to_json_string_ext(encoded, JSON_C_TO_STRING_PRETTY)) > 1024 * 1024);
    json_object_put(encoded);
    CHECK(!tb_profiles_save_setup(store, "profile-0", s, snapshot, "/launch directory"));
    CHECK(strstr(tb_profiles_error(store), "1 MiB")); preserved = read_file(tb_profiles_path(store)); CHECK(strcmp(saved, preserved) == 0); free(preserved);
    decoded = tb_profiles_setup(store, "profile-0"); CHECK(decoded && decoded->break_count == 1); free(decoded);
    CHECK(tb_profiles_remove(store, "profile-0") && !tb_profiles_setup(store, "profile-0"));
    /* Restore the original legacy fixture for the remainder of the M13 cases. */
    tb_profiles_destroy(copy); free(snapshot); free(saved); free(s);
}
int main(void)
{
    char root[] = "/tmp/the-bugger-m13-XXXXXX"; CHECK(mkdtemp(root)); CHECK(setenv("XDG_CONFIG_HOME", root, 1) == 0);
    TbProfiles *store = tb_profiles_create(); TbProfile *p = malloc(sizeof *p); CHECK(store && p);
    CHECK(tb_profiles_load(store) && tb_profiles_count(store) == 0);
    CHECK(access(tb_profiles_path(store), F_OK) != 0); /* Loading creates nothing. */
    for (int kind = 0; kind < 3; ++kind) {
        tb_profile_defaults(p, kind); (void)snprintf(p->name, sizeof p->name, "profile-%d", kind);
        p->attach = kind == 0;
        (void)snprintf(p->executable, sizeof p->executable, "/path with spaces/firmware.elf");
        p->arguments.count = 3;
        (void)snprintf(p->arguments.values[0], TB_PROFILE_WORD, "two words");
        p->arguments.values[1][0] = 0;
        (void)snprintf(p->arguments.values[2], TB_PROFILE_WORD, "$HOME;$(false)\\\"");
        p->scripts.count = 2; (void)snprintf(p->scripts.values[0], TB_PROFILE_WORD, "first board.cfg"); (void)snprintf(p->scripts.values[1], TB_PROFILE_WORD, "second.cfg");
        p->search.count = 1; (void)snprintf(p->search.values[0], TB_PROFILE_WORD, "/scripts with spaces");
        p->directory_count = 2; (void)snprintf(p->directories[0], TB_PROFILE_PATH, "/priority"); (void)snprintf(p->directories[1], TB_PROFILE_PATH, "/fallback");
        p->rule_count = 2; (void)snprintf(p->rules[0].from, TB_PROFILE_PATH, "/build/project"); (void)snprintf(p->rules[0].to, TB_PROFILE_PATH, "/local project");
        (void)snprintf(p->rules[1].from, TB_PROFILE_PATH, "/build"); (void)snprintf(p->rules[1].to, TB_PROFILE_PATH, "/other");
        CHECK(tb_profile_valid(p) && tb_profiles_put(store, p, false));
        CHECK(!tb_profiles_put(store, p, false));
        CHECK(tb_profiles_put(store, p, true));
        TbProfiles *copy = tb_profiles_create(); CHECK(copy && tb_profiles_load(copy));
        const TbProfile *got = tb_profiles_find(copy, p->name); CHECK(got && memcmp(got, p, sizeof *p) == 0);
        tb_profiles_destroy(copy);
    }
    char *legacy_store = read_file(tb_profiles_path(store));
    setup_checks(store); write_file(tb_profiles_path(store), legacy_store); free(legacy_store); CHECK(tb_profiles_load(store));
    struct stat info; CHECK(stat(tb_profiles_path(store), &info) == 0 && (info.st_mode & 0777) == 0600);
    char *clean = read_file(tb_profiles_path(store)); CHECK(strstr(clean, "\"keys\"") == NULL);
    json_object *legacy = json_tokener_parse(clean), *items = NULL; CHECK(legacy);
    CHECK(json_object_object_get_ex(legacy, "profiles", &items));
    CHECK(json_object_object_add(legacy, "version", json_object_new_int(1)) == 0);
    CHECK(json_object_object_add(json_object_array_get_idx(items, 0), "keys", json_object_new_string("ignored legacy bindings")) == 0);
    write_file(tb_profiles_path(store), json_object_to_json_string(legacy)); json_object_put(legacy); free(clean);
    CHECK(tb_profiles_load(store));
    CHECK(tb_profiles_touch(store, "profile-0"));
    clean = read_file(tb_profiles_path(store)); CHECK(strstr(clean, "\"keys\"") == NULL); free(clean);
    char *original = read_file(tb_profiles_path(store));
    CHECK(!strstr(original, "pid") && !strstr(original, "environment") && !strstr(original, "breakpoints"));
    char lock_path[256]; (void)snprintf(lock_path, sizeof lock_path, "%s/the-bugger/.lock", root);
    int locked = open(lock_path, O_RDWR); CHECK(locked >= 0 && flock(locked, LOCK_EX | LOCK_NB) == 0);
    CHECK(!tb_profiles_put(store, p, true)); CHECK(close(locked) == 0);
    struct rlimit limit; CHECK(getrlimit(RLIMIT_FSIZE, &limit) == 0);
    struct rlimit tiny = limit; tiny.rlim_cur = 128;
    void (*handler)(int) = signal(SIGXFSZ, SIG_IGN); CHECK(handler != SIG_ERR);
    CHECK(setrlimit(RLIMIT_FSIZE, &tiny) == 0); CHECK(!tb_profiles_put(store, p, true));
    CHECK(setrlimit(RLIMIT_FSIZE, &limit) == 0 && signal(SIGXFSZ, handler) != SIG_ERR);
    char *preserved = read_file(tb_profiles_path(store)); CHECK(strcmp(original, preserved) == 0); free(preserved);
    const char *bad[] = {"", "{", "[]", "{\"version\":3,\"profiles\":[],\"recent\":[]}",
        "{\"version\":1,\"profiles\":[],\"recent\":[]} trailing", "{\"version\":1,\"profiles\":[],\"recent\":[\"missing\"]}",
        "{\"version\":1,\"profiles\":[{}],\"recent\":[]}"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) {
        write_file(tb_profiles_path(store), bad[i]); CHECK(!tb_profiles_load(store));
        CHECK(tb_profiles_count(store) == 3); CHECK(!tb_profiles_put(store, p, true));
        char *same = read_file(tb_profiles_path(store)); CHECK(strcmp(same, bad[i]) == 0); free(same);
        write_file(tb_profiles_path(store), original); CHECK(tb_profiles_load(store));
    }
    int large = open(tb_profiles_path(store), O_WRONLY); CHECK(large >= 0);
    CHECK(ftruncate(large, 1024 * 1024 + 1) == 0 && close(large) == 0); CHECK(!tb_profiles_load(store));
    write_file(tb_profiles_path(store), original); CHECK(tb_profiles_load(store));
    TbProfiles *stale = tb_profiles_create(); CHECK(stale && tb_profiles_load(stale));
    CHECK(tb_profiles_touch(store, "profile-0")); CHECK(!tb_profiles_touch(stale, "profile-1"));
    CHECK(strstr(tb_profiles_error(stale), "changed on disk")); CHECK(tb_profiles_load(stale));
    CHECK(tb_profiles_touch(stale, "profile-1")); CHECK(tb_profiles_load(store));
    CHECK(strcmp(tb_profiles_recent(store, 0), "profile-1") == 0);
    CHECK(strcmp(tb_profiles_recent(store, 1), "profile-0") == 0);
    CHECK(tb_profiles_touch(store, "profile-0")); CHECK(strcmp(tb_profiles_recent(store, 0), "profile-0") == 0);
    CHECK(tb_profiles_remove(store, "profile-0")); CHECK(!tb_profiles_find(store, "profile-0"));
    CHECK(strcmp(tb_profiles_recent(store, 0), "profile-1") == 0);
    tb_profile_defaults(p, 0); (void)snprintf(p->name, sizeof p->name, "bad");
    p->arguments.count = TB_PROFILE_WORDS + 1; CHECK(!tb_profile_valid(p));
    p->arguments.count = 0; p->name[0] = (char)0xff; CHECK(!tb_profile_valid(p));
    /* A non-regular/symlink store is rejected without blocking or following it. */
    CHECK(unlink(tb_profiles_path(store)) == 0); CHECK(mkfifo(tb_profiles_path(store), 0600) == 0);
    CHECK(!tb_profiles_load(store)); CHECK(unlink(tb_profiles_path(store)) == 0);
    char target[256]; (void)snprintf(target, sizeof target, "%s/untouched", root); write_file(target, "sentinel");
    CHECK(symlink(target, tb_profiles_path(store)) == 0); CHECK(!tb_profiles_load(store));
    char *same = read_file(target); CHECK(strcmp(same, "sentinel") == 0); free(same);
    CHECK(unlink(tb_profiles_path(store)) == 0); CHECK(tb_profiles_load(store));
    for (int i = 0; i < TB_PROFILES; ++i) {
        tb_profile_defaults(p, i % 3); (void)snprintf(p->name, sizeof p->name, "bounded-%d", i); CHECK(tb_profiles_put(store, p, false));
        CHECK(tb_profiles_touch(store, p->name));
    }
    (void)snprintf(p->name, sizeof p->name, "overflow"); CHECK(!tb_profiles_put(store, p, false)); CHECK(!tb_profiles_recent(store, TB_PROFILE_RECENT));
    pid_t child = fork(); CHECK(child >= 0);
    if (child == 0) {
        CHECK(setenv("HOME", root, 1) == 0 && setenv("XDG_CONFIG_HOME", "relative-is-ignored", 1) == 0);
        TbProfiles *fallback = tb_profiles_create(); CHECK(fallback && tb_profiles_load(fallback));
        char expected[256]; (void)snprintf(expected, sizeof expected, "%s/.config/the-bugger/profiles.json", root);
        CHECK(strcmp(tb_profiles_path(fallback), expected) == 0);
        tb_profile_defaults(p, 0); (void)snprintf(p->name, sizeof p->name, "fallback"); CHECK(tb_profiles_put(fallback, p, false));
        tb_profiles_destroy(fallback); free(p); free(original); tb_profiles_destroy(stale); tb_profiles_destroy(store); _exit(0);
    }
    int status; CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    tb_profiles_destroy(stale); tb_profiles_destroy(store); free(p); free(original); remove_tree(root);
    puts("PASS M13 XDG versioned profiles, all connection types, literal argv/paths, ordered source/scripts, atomic private writes, stale-writer rejection, corrupt-file preservation, recent/delete and bounds");
    return EXIT_SUCCESS;
}
