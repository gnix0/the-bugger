#define _POSIX_C_SOURCE 200809L
#include "code.h"
#include "../support/check.h"
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

static TbContext *context;
static TbCode *code;
static unsigned completions;
static void emit(void *unused, const TbGdbEvent *event)
{
    (void)unused;
    if (!tb_code_event(code, event)) { (void)tb_context_event(context, event); }
    if (event->kind == TB_GDB_REPLY) { ++completions; }
}
static void pump(TbGdb *gdb) { tb_gdb_pump(gdb, 5); tb_context_pump(context); tb_code_pump(code); }
static void command(TbGdb *gdb, const char *operation)
{
    CHECK(tb_gdb_request(gdb, operation, NULL, 0, TB_GDB_SESSION, 999, 1000, NULL));
}
static void ready(TbGdb *gdb, uint64_t revision)
{
    const TbCodeView *view = tb_code_view(code, 0);
    uint64_t until = tb_gdb_now_ms() + 5000;
    while ((view->stale || view->assembly_revision <= revision) && tb_gdb_now_ms() < until) { pump(gdb); }
    if (view->stale || view->assembly_revision <= revision) {
        fprintf(stderr, "code ready timeout: %s / %s / %s\n", tb_code_message(code), view->source_error, view->assembly_error); CHECK(false);
    }
}
static void grammar(void)
{
    uint64_t address; bool symbol;
    CHECK(tb_code_location("0xffffffffffffffff", &address, &symbol) && address == UINT64_MAX && !symbol);
    CHECK(tb_code_location("0x0", &address, &symbol) && address == 0 && !symbol);
    CHECK(tb_code_location("main", &address, &symbol) && symbol);
    CHECK(tb_code_location("thing::method", &address, &symbol) && symbol);
    const char *bad[] = {"", "0x", "0x10000000000000000", "main()", "main;quit", "x=2", "*p", "&p", "++p", "main\nquit", "$(false)", "p + 4", "$pc"};
    for (size_t i = 0; i < sizeof bad / sizeof bad[0]; ++i) { CHECK(!tb_code_location(bad[i], &address, &symbol)); }
    char text[64]; tb_code_safe(text, sizeof text, "a\033\a\t\377", 5);
    CHECK(strstr(text, "\\x1b") && strstr(text, "\\x07") && strstr(text, "\\xff") && !strchr(text, '\033'));
}
static void source_bounds(const char *path, TbGdb *gdb)
{
    int fd = open(path, O_WRONLY | O_TRUNC); CHECK(fd >= 0);
    char *bytes = malloc(TB_CODE_SOURCE_BYTES + 32); CHECK(bytes);
    memset(bytes, 'a', TB_CODE_SOURCE_BYTES + 32);
    CHECK(write(fd, bytes, TB_CODE_SOURCE_BYTES + 32) == TB_CODE_SOURCE_BYTES + 32); CHECK(close(fd) == 0);
    uint64_t revision = tb_code_view(code, 0)->assembly_revision;
    tb_code_refresh(code); ready(gdb, revision); CHECK(tb_code_view(code, 0)->truncated);
    fd = open(path, O_WRONLY | O_TRUNC); CHECK(fd >= 0);
    memset(bytes, '\n', TB_CODE_LINES + 10); memcpy(bytes + TB_CODE_LINES + 10, "OUTSIDE_LIMIT", 13);
    CHECK(write(fd, bytes, TB_CODE_LINES + 23) == TB_CODE_LINES + 23); CHECK(close(fd) == 0); free(bytes);
    revision = tb_code_view(code, 0)->assembly_revision; tb_code_refresh(code); ready(gdb, revision);
    CHECK(tb_code_view(code, 0)->line_count == TB_CODE_LINES && tb_code_view(code, 0)->truncated);
    CHECK(tb_code_find(code, 0, "OUTSIDE_LIMIT", 0) == -1);
}
static void run(const char *peer, const char *mode)
{
    char path[] = "/tmp/the bugger code-XXXXXX"; int fd = mkstemp(path); CHECK(fd >= 0);
    const char source[] = "int main(void) {\n return 7; /* MARKER */\033\a\n}\n";
    CHECK(write(fd, source, sizeof source - 1) == (ssize_t)sizeof source - 1 && close(fd) == 0);
    CHECK(setenv("TB_FAKE_SOURCE", path, 1) == 0 && setenv("TB_FAKE_MODE", mode, 1) == 0);
    TbGdb *gdb = tb_gdb_create(emit, NULL); CHECK(gdb);
    context = tb_context_create(gdb); code = tb_code_create(gdb, context); CHECK(context && code);
    CHECK(tb_gdb_start(gdb, peer, 1000));
    uint64_t until = tb_gdb_now_ms() + 2000;
    while (tb_gdb_state(gdb) != TB_GDB_READY && tb_gdb_now_ms() < until) { pump(gdb); }
    CHECK(tb_gdb_state(gdb) == TB_GDB_READY); command(gdb, "-stop");
    tb_code_visible(code, 0, true, true);
    if (strcmp(mode, "context-code-stale") == 0) {
        for (int i = 0; i < 40; ++i) { pump(gdb); }
        tb_code_mixed(code, 0, true); command(gdb, "-ctx-release");
    }
    ready(gdb, 0);
    const TbCodeView *view = tb_code_view(code, 0);
    CHECK(tb_code_file_count(code) == 3 && view->line_count == 3 && view->selected_line == 2);
    CHECK(view->instruction_count == TB_CODE_INSNS && view->more);
    CHECK(view->instructions[4].numeric == 0x1234 && view->instructions[0].numeric == 0x1230);
    uint64_t earlier = view->assembly_revision;
    CHECK(tb_code_assembly_page(code, 0, -1)); ready(gdb, earlier);
    uint64_t base = strcmp(mode, "context-code-short") == 0 ? 0x122f : 0x1228;
    CHECK(view->instructions[0].numeric == base && view->instruction_count == TB_CODE_INSNS);
    bool retained_pc = false;
    for (size_t row = 0; row < view->instruction_count; ++row) {
        if (view->instructions[row].numeric == 0x1234) { retained_pc = true; }
    }
    CHECK(retained_pc);
    earlier = view->assembly_revision; tb_code_return(code, 0); ready(gdb, earlier);
    CHECK(view->assembly_follow && view->instruction_count == TB_CODE_INSNS);
    char text[128]; CHECK(tb_code_source_line(code, 0, 1, text, sizeof text));
    CHECK(strstr(text, "MARKER") && strstr(text, "\\x1b") && !strchr(text, '\033'));
    CHECK(tb_code_find(code, 0, "MARKER", 0) == 1 && tb_code_find(code, 0, "missing", 0) == -1);
    uint64_t revision = view->assembly_revision;
    tb_code_mixed(code, 0, true); ready(gdb, revision);
    CHECK(view->mixed_available == (strcmp(mode, "context-code-fallback") != 0));
    revision = view->assembly_revision; uint64_t anchor = view->anchor;
    CHECK(tb_code_assembly_page(code, 0, 1)); ready(gdb, revision); CHECK(view->anchor > anchor);
    revision = view->assembly_revision; CHECK(tb_code_assembly_page(code, 0, -1)); ready(gdb, revision); CHECK(view->anchor == anchor);
    revision = view->assembly_revision; CHECK(tb_code_address(code, 0, "main")); ready(gdb, revision); CHECK(view->anchor == 0x2000);
    revision = view->assembly_revision; CHECK(tb_code_address(code, 0, "0x0")); ready(gdb, revision);
    CHECK(view->anchor == 0 && view->instruction_count && view->instructions[0].numeric == 0);
    CHECK(tb_code_open(code, 0, 1) && *view->source_error && view->line_count == 0);
    command(gdb, "-stop");
    for (int i = 0; i < 40; ++i) { pump(gdb); }
    CHECK(tb_code_tab_active(code, 0) == 1 && !view->source_follow && strstr(view->path, "/not/here/duplicate.c"));
    CHECK(tb_code_open(code, 0, 2) && strstr(view->source_error, "not resolved by GDB") && view->line_count == 0);
    CHECK(tb_code_open(code, 0, 0) && view->line_count == 3);
    CHECK(tb_code_tab_count(code, 0) == 4 && tb_code_tab_active(code, 0) == 3);
    CHECK(tb_code_open(code, 0, 0) && tb_code_tab_count(code, 0) == 4);
    CHECK(tb_code_tab_file(code, 0, 1) && strcmp(tb_code_tab_file(code, 0, 1)->file, "duplicate.c") == 0);
    CHECK(!tb_code_tab_select(code, 0, 4) && !tb_code_tab_close(code, 0, 0));
    CHECK(tb_code_tab_select(code, 0, 1) && strstr(view->path, "/not/here/duplicate.c"));
    CHECK(tb_code_tab_select(code, 0, 3) && view->line_count == 3);
    command(gdb, "-stop");
    for (int i = 0; i < 40; ++i) { pump(gdb); }
    CHECK(tb_code_tab_active(code, 0) == 3 && !view->source_follow && view->line_count == 3);
    CHECK(tb_code_tab_close(code, 0, 2) && tb_code_tab_count(code, 0) == 3 && tb_code_tab_active(code, 0) == 2);
    CHECK(tb_code_tab_select(code, 0, 0) && view->source_follow && view->line_count == 3);
    CHECK(tb_code_tab_close(code, 0, 2) && tb_code_tab_close(code, 0, 1) && tb_code_tab_count(code, 0) == 1);
    CHECK(!tb_code_open(code, 0, 9999));
    for (int i = 0; i < 20; ++i) { pump(gdb); }
    unsigned idle = completions;
    for (int i = 0; i < 20; ++i) { pump(gdb); }
    CHECK(completions == idle);
    tb_code_visible(code, 0, false, false); tb_code_refresh(code);
    for (int i = 0; i < 20; ++i) { pump(gdb); }
    CHECK(completions == idle);
    tb_code_visible(code, 0, true, true); revision = view->assembly_revision; ready(gdb, revision);
    if (strcmp(mode, "context-code") == 0) { source_bounds(path, gdb); }
    command(gdb, "-run"); for (int i = 0; i < 20; ++i) { pump(gdb); }
    CHECK(view->stale && !tb_code_address(code, 0, "main") && !tb_code_assembly_page(code, 0, 1));
    idle = completions; tb_code_request_files(code);
    for (int i = 0; i < 20; ++i) { pump(gdb); }
    CHECK(completions == idle);
    tb_gdb_close(gdb); until = tb_gdb_now_ms() + 2000;
    while (tb_gdb_state(gdb) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < until) { pump(gdb); }
    CHECK(tb_gdb_state(gdb) == TB_GDB_DISCONNECTED);
    tb_code_reset(code); CHECK(tb_code_file_count(code) == 0 && tb_code_view(code, 0)->line_count == 0 && tb_code_tab_count(code, 0) == 1);
    tb_code_destroy(code); tb_context_destroy(context); CHECK(tb_gdb_destroy(gdb)); code = NULL; context = NULL;
    CHECK(unlink(path) == 0);
}
int main(int argc, char *argv[])
{
    CHECK(argc == 2); grammar(); run(argv[1], "context-code");
    run(argv[1], "context-code-short");
    run(argv[1], "context-code-stale"); run(argv[1], "context-code-fallback");
    CHECK(unsetenv("TB_FAKE_SOURCE") == 0 && unsetenv("TB_FAKE_MODE") == 0);
    puts("PASS M07 bounded source/disassembly, mixed/fallback, stale view replies, safe navigation, paging, source errors, control bytes and idle/running/invisible reads"); return 0;
}
