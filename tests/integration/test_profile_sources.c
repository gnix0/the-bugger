#define _POSIX_C_SOURCE 200809L
#include "code.h"
#include "../support/check.h"
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static TbCode_t *code;
static TbContext_t *context;
static unsigned replies;
static void emit(void *unused, const TbGdbEvent_t *event)
{
    (void)unused;
    if (!tb_code_event(code, event)) { (void)tb_context_event(context, event); }
    if (event->kind == TB_GDB_REPLY) { ++replies; }
}
static void pump(TbGdb_t *gdb) { tb_gdb_pump(gdb, 2); tb_code_pump(code); }
static void restored(TbGdb_t *gdb)
{
    uint64_t deadline = tb_gdb_now_ms() + 2000;
    while (tb_code_restoring(code) && tb_gdb_now_ms() < deadline) { pump(gdb); }
    CHECK(!tb_code_restoring(code));
}
int main(int argc, char **argv)
{
    CHECK(argc == 2); CHECK(setenv("TB_FAKE_MODE", "profile-source", 1) == 0);
    char root[] = "/tmp/the-bugger-m13-source-XXXXXX"; CHECK(mkdtemp(root));
    char first[128], second[128], rejected[128];
    (void)snprintf(first, sizeof first, "%s/first path", root); (void)snprintf(second, sizeof second, "%s/second \"path\"", root);
    (void)snprintf(rejected, sizeof rejected, "%s/rejected", root);
    CHECK(mkdir(first, 0700) == 0 && mkdir(second, 0700) == 0 && mkdir(rejected, 0700) == 0);
    TbGdb_t *gdb = tb_gdb_create(emit, NULL); CHECK(gdb); context = tb_context_create(gdb); CHECK(context); code = tb_code_create(gdb, context); CHECK(code);
    CHECK(tb_gdb_start(gdb, argv[1], 1000)); uint64_t end = tb_gdb_now_ms() + 2000;
    while (tb_gdb_state(gdb) != TB_GDB_READY && tb_gdb_now_ms() < end) { pump(gdb); } CHECK(tb_gdb_state(gdb) == TB_GDB_READY);
    const char *dirs[] = {first, second}; TbSourceRule_t rules[2] = {0};
    (void)snprintf(rules[0].from, TB_CODE_PATH, "/build/project"); (void)snprintf(rules[0].to, TB_CODE_PATH, "%s", first);
    (void)snprintf(rules[1].from, TB_CODE_PATH, "/build"); (void)snprintf(rules[1].to, TB_CODE_PATH, "%s", second);
    CHECK(tb_code_restore_settings(code, dirs, 2, rules, 2)); restored(gdb);
    CHECK(tb_code_directory_count(code) == 2 && strcmp(tb_code_directory(code, 0), first) == 0 && strcmp(tb_code_directory(code, 1), second) == 0);
    CHECK(tb_code_rule_count(code) == 2 && strcmp(tb_code_rule(code, 0)->from, "/build/project") == 0 && strcmp(tb_code_rule(code, 1)->from, "/build") == 0);
    CHECK(!tb_code_restore_settings(code, dirs, 2, rules, 2)); /* Fresh sessions only. */
    tb_code_reset(code); const char *bad[] = {rejected, first};
    CHECK(tb_code_restore_settings(code, bad, 2, rules, 2)); restored(gdb);
    /* The reverse directory application commits 'first', then fails. No
     * mappings or subsequent retries are issued after the failed override. */
    CHECK(tb_code_directory_count(code) == 1 && tb_code_rule_count(code) == 0 && strstr(tb_code_message(code), "rejected"));
    unsigned before = replies; for (int i = 0; i < 20; ++i) { pump(gdb); } CHECK(replies == before);
    tb_code_reset(code); CHECK(tb_code_directory_count(code) == 0 && !tb_code_restoring(code));
    /* Relative entries and GDB tokens must reach GDB without frontend cwd validation. */
    const char *relative[] = {"../nonexistent-gdb-relative", ".", "$cwd", "$cdir"};
    CHECK(tb_code_restore_settings(code, relative, 4, NULL, 0)); restored(gdb);
    CHECK(tb_code_directory_count(code) == 4);
    for (size_t i = 0; i < 4; ++i) { CHECK(!strcmp(tb_code_directory(code, i), relative[i])); }
    tb_code_reset(code);
    CHECK(setenv("HOME", root, 1) == 0);
    const char *home_dirs[] = {"~/first path"};
    TbSourceRule_t home_rule = {0}; (void)snprintf(home_rule.from, sizeof home_rule.from, "~/build-origin"); (void)snprintf(home_rule.to, sizeof home_rule.to, "~/first path");
    CHECK(tb_code_restore_settings(code, home_dirs, 1, &home_rule, 1));
    CHECK(setenv("HOME", "/different-home-after-queue", 1) == 0); restored(gdb);
    CHECK(!strcmp(tb_code_directory(code, 0), "~/first path") && !strcmp(tb_code_effective_directory(code, 0), first));
    CHECK(!strcmp(tb_code_rule(code, 0)->to, "~/first path") && !strcmp(tb_code_effective_rule(code, 0)->to, first));
    CHECK(!strcmp(tb_code_effective_rule(code, 0)->from, "~/build-origin"));
    tb_code_reset(code); CHECK(setenv("HOME", "relative", 1) == 0);
    CHECK(!tb_code_add_directory(code, "~/first path") && !tb_code_add_rule(code, "/build", "~/first path"));
    CHECK(!tb_code_restore_settings(code, home_dirs, 1, &home_rule, 1) && !tb_code_restoring(code));
    CHECK(setenv("HOME", "/tmp/path\nwith-line-break", 1) == 0);
    CHECK(!tb_code_add_rule(code, "/build", "~/source"));
    CHECK(!tb_code_add_directory(code, "~/source"));
    CHECK(!tb_code_restore_settings(code, home_dirs, 1, &home_rule, 1) && !tb_code_restoring(code));
    tb_gdb_close(gdb); end = tb_gdb_now_ms() + 2000;
    while (tb_gdb_state(gdb) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(tb_gdb_state(gdb) == TB_GDB_DISCONNECTED); tb_code_destroy(code); tb_context_destroy(context); CHECK(tb_gdb_destroy(gdb));
    CHECK(rmdir(first) == 0 && rmdir(second) == 0 && rmdir(rejected) == 0 && rmdir(root) == 0);
    CHECK(unsetenv("TB_FAKE_MODE") == 0);
    puts("PASS M13 asynchronous source-profile restore order, quoted paths, fresh-session restriction, visible failure, no retry spin and session reset");
    return EXIT_SUCCESS;
}
