#define _POSIX_C_SOURCE 200809L
#include "profiles.h"
#include "native.h"
#include "../support/check.h"
#include <string.h>
#include <unistd.h>

static TbNative *owned;
static void cleanup(void)
{
    if (!owned) { return; }
    tb_native_close(owned); uint64_t end = tb_gdb_now_ms() + 6000;
    while (tb_native_active(owned) && tb_gdb_now_ms() < end) { tb_native_pump(owned, 5); }
    if (!tb_native_active(owned)) { (void)tb_native_destroy(owned); owned = NULL; }
}
static void breaks_ready(TbNative *n)
{
    TbBreakpoints_t *b = tb_native_breakpoints(n); uint64_t end = tb_gdb_now_ms() + 5000;
    while ((!tb_break_ready(b) || tb_break_busy(b)) && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    CHECK(tb_break_ready(b) && !tb_break_busy(b));
}
static void stopped(TbNative *n, uint64_t after)
{
    uint64_t end = tb_gdb_now_ms() + 6000;
    const TbCodeView_t *v = tb_code_view(tb_native_code(n), 0);
    const TbContextSnapshot_t *s = tb_context_snapshot(tb_native_context(n));
    while (tb_gdb_now_ms() < end) {
        tb_native_pump(n, 5);
        if (tb_native_state(n) == TB_NATIVE_STOPPED && s->generation.stop > after && !s->stale && !v->stale && v->line_count) { return; }
    }
    fprintf(stderr, "Profile stop/source failed: %s / %s\n", tb_native_message(n), v->source_error); CHECK(false);
}
static void console_done(TbNative *n, const char *command, unsigned serial)
{
    uint64_t end = tb_gdb_now_ms() + 5000;
    while (!tb_native_console(n, command) && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    CHECK(tb_gdb_now_ms() < end);
    char marker[80], echo[96]; (void)snprintf(marker, sizeof marker, "[gdb] SOURCE-PATH-%u", serial); (void)snprintf(echo, sizeof echo, "echo SOURCE-PATH-%u", serial);
    while (!tb_native_console(n, echo) && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    bool found = false;
    while (!found && tb_gdb_now_ms() < end) {
        tb_native_pump(n, 5);
        for (size_t i = 0; i < tb_native_log_count(n); ++i) { found = found || strstr(tb_native_log_line(n, i), marker) != NULL; }
    }
    CHECK(found);
    for (int i = 0; i < 10; ++i) { tb_native_pump(n, 5); }
}
static void source_paths(TbNative *n, const char *source)
{
    char cwd[4096], command[4128]; CHECK(getcwd(cwd, sizeof cwd));
    console_done(n, "cd /tmp", 1);
    TbCode_t *code = tb_native_code(n);
    const char *entries[] = {".", "$cwd", "$cdir", "../tb-gdb-relative-not-present"};
    for (size_t i = 0; i < sizeof entries / sizeof *entries; ++i) {
        uint64_t end = tb_gdb_now_ms() + 5000;
        while (!tb_code_add_directory(code, entries[i]) && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
        CHECK(tb_gdb_now_ms() < end);
        while (tb_code_settings_busy(code) && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
        CHECK(!tb_code_settings_busy(code) && !strcmp(tb_code_directory(code, 0), entries[i]));
    }
    console_done(n, "cd /", 2); console_done(n, "show directories", 3);
    bool resolved = false;
    for (size_t i = 0; i < tb_native_log_count(n); ++i) {
        const char *line = tb_native_log_line(n, i);
        resolved = resolved || (strstr(line, "Source directories searched:") && strstr(line, "$cwd") && strstr(line, "$cdir") && strstr(line, "/tmp/../tb-gdb-relative-not-present") && strstr(line, ":/tmp"));
    }
    CHECK(resolved);
    (void)snprintf(command, sizeof command, "cd %s", cwd); console_done(n, command, 4);
    char *home = getenv("HOME") ? strdup(getenv("HOME")) : NULL; CHECK(setenv("HOME", source, 1) == 0);
    uint64_t end = tb_gdb_now_ms() + 5000;
    while (!tb_code_add_directory(code, "~") && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    CHECK(tb_gdb_now_ms() < end);
    while (tb_code_settings_busy(code) && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    CHECK(!tb_code_settings_busy(code) && !strcmp(tb_code_directory(code, 0), "~") && !strcmp(tb_code_effective_directory(code, 0), source));
    if (home) { CHECK(setenv("HOME", home, 1) == 0); free(home); } else { CHECK(unsetenv("HOME") == 0); }
}
int main(int argc, char **argv)
{
    CHECK(argc == 3); CHECK(atexit(cleanup) == 0);
    char root[] = "/tmp/the-bugger-m13-native-XXXXXX"; CHECK(mkdtemp(root)); CHECK(setenv("XDG_CONFIG_HOME", root, 1) == 0);
    TbProfiles_t *store = tb_profiles_create(); TbProfile_t *p = malloc(sizeof *p); CHECK(store && p && tb_profiles_load(store));
    tb_profile_defaults(p, 0); (void)snprintf(p->name, sizeof p->name, "relocated");
    CHECK(strlen(argv[1]) < sizeof p->executable && strlen(argv[2]) < TB_PROFILE_PATH);
    (void)snprintf(p->executable, sizeof p->executable, "%s", argv[1]);
    p->directory_count = p->rule_count = 1;
    (void)snprintf(p->directories[0], TB_PROFILE_PATH, "%s", argv[2]);
    (void)snprintf(p->rules[0].from, TB_PROFILE_PATH, "/the bugger build tree");
    (void)snprintf(p->rules[0].to, TB_PROFILE_PATH, "%s", argv[2]);
    CHECK(tb_profiles_put(store, p, false)); free(p); tb_profiles_destroy(store);
    store = tb_profiles_create(); CHECK(store && tb_profiles_load(store));
    const TbProfile_t *loaded = tb_profiles_find(store, "relocated"); CHECK(loaded);
    TbNative *n = tb_native_create(); CHECK(n); owned = n;
    TbNativeConfig config = {.gdb = loaded->gdb, .executable = loaded->executable}; CHECK(tb_native_start(n, &config));
    TbCode_t *code = tb_native_code(n); const char *dirs[] = {loaded->directories[0]};
    TbSourceRule_t rule = {0}; (void)snprintf(rule.from, sizeof rule.from, "%s", loaded->rules[0].from); (void)snprintf(rule.to, sizeof rule.to, "%s", loaded->rules[0].to);
    CHECK(tb_code_restore_settings(code, dirs, 1, &rule, 1)); CHECK(!tb_native_continue(n));
    tb_code_visible(code, 0, true, true); uint64_t end = tb_gdb_now_ms() + 6000; bool interlocked = false;
    while ((tb_native_state(n) != TB_NATIVE_READY || tb_code_restoring(code)) && tb_gdb_now_ms() < end) {
        tb_native_pump(n, 5);
        if (tb_native_state(n) == TB_NATIVE_READY && tb_code_restoring(code)) { CHECK(!tb_native_continue(n)); interlocked = true; }
    }
    CHECK(interlocked);
    CHECK(tb_native_state(n) == TB_NATIVE_READY && !tb_code_restoring(code));
    CHECK(tb_code_directory_count(code) == 1 && tb_code_rule_count(code) == 1);
    source_paths(n, argv[2]);
    CHECK(tb_native_console(n, "start")); stopped(n, 0); breaks_ready(n);
    const TbCodeView_t *view = tb_code_view(code, 0); CHECK(strstr(view->path, argv[2]));
    const TbCodeFile_t *left = NULL;
    for (size_t i = 0; i < tb_code_file_count(code); ++i) {
        const TbCodeFile_t *file = tb_code_file(code, i);
        if (strstr(file->full, "/left/duplicate.c")) { left = file; break; }
    }
    CHECK(left && strstr(left->full, argv[2]));
    TbBreakpoints_t *b = tb_native_breakpoints(n);
    CHECK(tb_break_add_at(b, (TbBreakLocation_t){.source = left->full, .line = 3}, false)); breaks_ready(n);
    CHECK(tb_break_count(b) == 1 && !tb_break_row(b, 0)->pending && strcmp(tb_break_row(b, 0)->file, left->full) == 0);
    uint64_t before = tb_context_snapshot(tb_native_context(n))->generation.stop;
    CHECK(tb_native_continue(n)); stopped(n, before);
    CHECK(strstr(view->path, "/left/duplicate.c") && tb_code_find(code, 0, "LEFT_SOURCE_MARKER", 0) >= 0);
    cleanup(); CHECK(!owned);
    CHECK(unlink(tb_profiles_path(store)) == 0);
    char path[256]; (void)snprintf(path, sizeof path, "%s/the-bugger/.lock", root); CHECK(unlink(path) == 0);
    (void)snprintf(path, sizeof path, "%s/the-bugger", root); CHECK(rmdir(path) == 0 && rmdir(root) == 0);
    tb_profiles_destroy(store);
    puts("PASS M13 actual GDB saved/reloaded relocated source settings, execution interlock, mapped duplicate-basename breakpoint resolution and real stop");
    return EXIT_SUCCESS;
}
