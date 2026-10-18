#define _POSIX_C_SOURCE 200809L
#include "profiles.h"
#include "../support/check.h"
#include <dirent.h>
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
        p->keys[3] = 7;
        CHECK(tb_profile_valid(p) && tb_profiles_put(store, p, false));
        CHECK(!tb_profiles_put(store, p, false));
        CHECK(tb_profiles_put(store, p, true));
        TbProfiles *copy = tb_profiles_create(); CHECK(copy && tb_profiles_load(copy));
        const TbProfile *got = tb_profiles_find(copy, p->name); CHECK(got && memcmp(got, p, sizeof *p) == 0);
        tb_profiles_destroy(copy);
    }
    struct stat info; CHECK(stat(tb_profiles_path(store), &info) == 0 && (info.st_mode & 0777) == 0600);
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
    const char *bad[] = {"", "{", "[]", "{\"version\":2,\"profiles\":[],\"recent\":[]}",
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
    p->keys[3] = p->keys[0]; CHECK(!tb_profile_valid(p)); p->keys[3] = -17; CHECK(!tb_profile_valid(p));
    p->keys[3] = -2; CHECK(tb_profile_valid(p)); p->arguments.count = TB_PROFILE_WORDS + 1; CHECK(!tb_profile_valid(p));
    p->arguments.count = 0; p->name[0] = (char)0xff; CHECK(!tb_profile_valid(p));
    for (int key = -26; key <= 24; ++key) {
        if (!tb_profile_key_valid(key)) { continue; }
        char text[32]; int parsed; tb_profile_key_name(key, text, sizeof text); CHECK(tb_profile_key_parse(text, &parsed) && key == parsed);
    }
    int key; CHECK(!tb_profile_key_parse("F0", &key) && !tb_profile_key_parse("F25", &key) && !tb_profile_key_parse("Ctrl+Q", &key));
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
    puts("PASS M13 XDG versioned profiles, all connection types, literal argv/paths, ordered source/scripts, key validation, atomic private writes, stale-writer rejection, corrupt-file preservation, recent/delete and bounds");
    return EXIT_SUCCESS;
}
