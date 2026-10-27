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
    TbCodeView view = {.mixed = true, .mixed_available = true, .instruction_count = 3,
        .instructions = {{.numeric = 0x1000, .bytes = 2, .line = 7, .source = "a.c"},
                         {.numeric = 0x1002, .bytes = 2, .line = 7, .source = "a.c"},
                         {.numeric = 0x1010, .bytes = 2, .line = 1, .source = "b.c"}}};
    TbCodeRow rows[TB_CODE_ROWS];
    CHECK(tb_code_rows(&view, rows) == 6 && tb_code_rows(&view, NULL) == 6);
    CHECK(rows[0].kind == TB_CODE_SOURCE && rows[1].kind == TB_CODE_INSTRUCTION && rows[1].instruction == 0);
    CHECK(rows[2].kind == TB_CODE_INSTRUCTION && rows[2].instruction == 1);
    CHECK(rows[3].kind == TB_CODE_GAP && rows[3].begin == 0x1004 && rows[3].end == 0x1010);
    CHECK(rows[4].kind == TB_CODE_SOURCE && rows[5].kind == TB_CODE_INSTRUCTION && rows[5].instruction == 2);
    view.gap_begin = 0xff0; view.gap_end = 0x1000;
    CHECK(tb_code_rows(&view, rows) == 7 && rows[0].kind == TB_CODE_GAP);
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
    char other_path[] = "/tmp/the bugger mixed-XXXXXX"; fd = mkstemp(other_path); CHECK(fd >= 0);
    const char other_source[] = "int helper(void)\n\n{\n}\n";
    CHECK(write(fd, other_source, sizeof other_source - 1) == (ssize_t)sizeof other_source - 1 && close(fd) == 0);
    CHECK(setenv("TB_FAKE_SOURCE2", other_path, 1) == 0);
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
    if (strcmp(mode, "context-code") == 0) {
        /* Select an uncached window so this specifically exercises a failed
         * backend extension, rather than a successful cache hit. */
        earlier = view->assembly_revision;
        CHECK(tb_code_address(code, 0, "0x3000")); ready(gdb, earlier);
        earlier = view->assembly_revision;
        CHECK(tb_code_assembly_page(code, 0, 1)); ready(gdb, earlier);
        uint64_t retained = view->instructions[0].numeric;
        size_t retained_count = view->instruction_count;
        unsigned before = completions;
        command(gdb, "-code-page-error");
        until = tb_gdb_now_ms() + 1000;
        while (completions == before && tb_gdb_now_ms() < until) { pump(gdb); }
        CHECK(completions > before);
        earlier = view->assembly_revision;
        CHECK(tb_code_assembly_page(code, 0, 1)); ready(gdb, earlier);
        CHECK(strstr(view->assembly_error, "Cannot access memory"));
        CHECK(view->instruction_count == retained_count && view->anchor == retained);
        CHECK(view->instructions[0].numeric == retained);
        /* A failed adjacent read must leave a listing from which the user can
         * navigate backward, rather than an empty pane that rejects paging. */
        earlier = view->assembly_revision;
        CHECK(tb_code_assembly_page(code, 0, -1)); ready(gdb, earlier);
        CHECK(view->instruction_count && !*view->assembly_error);
        for (unsigned bad = 1; bad <= 3; ++bad) {
            char argument[8]; (void)snprintf(argument, sizeof argument, "%u", bad);
            const char *args[] = {argument}; before = completions;
            CHECK(tb_gdb_request(gdb, "-code-bad-reply", args, 1, TB_GDB_SESSION, 999, 1000, NULL));
            until = tb_gdb_now_ms() + 1000;
            while (completions == before && tb_gdb_now_ms() < until) { pump(gdb); }
            CHECK(completions > before);
            TbCodeView *saved = malloc(sizeof *saved); CHECK(saved); *saved = *view;
            earlier = view->assembly_revision;
            CHECK(tb_code_address(code, 0, "0x6000")); ready(gdb, earlier);
            CHECK(strstr(view->assembly_error, "prior listing retained"));
            CHECK(view->instruction_count == saved->instruction_count &&
                  memcmp(view->instructions, saved->instructions, sizeof view->instructions) == 0);
            CHECK(memcmp(&view->assembly_generation, &saved->assembly_generation, sizeof view->assembly_generation) == 0);
            CHECK(view->assembly_epoch == saved->assembly_epoch && view->anchor == saved->instructions[0].numeric);
            free(saved); before = completions;
            for (int i = 0; i < 10; ++i) { pump(gdb); } CHECK(completions == before);
        }
        earlier = view->assembly_revision; tb_code_return(code, 0); ready(gdb, earlier);
    }
    char text[128]; CHECK(tb_code_source_line(code, 0, 1, text, sizeof text));
    CHECK(strstr(text, "MARKER") && strstr(text, "\\x1b") && !strchr(text, '\033'));
    CHECK(tb_code_find(code, 0, "MARKER", 0) == 1 && tb_code_find(code, 0, "missing", 0) == -1);
    uint64_t revision = view->assembly_revision;
    tb_code_mixed(code, 0, true); ready(gdb, revision);
    CHECK(view->mixed_available == (strcmp(mode, "context-code-fallback") != 0));
    if (strcmp(mode, "context-code-multifile") == 0) {
        unsigned before = completions;
        for (unsigned i = 0; i < 10; ++i) { pump(gdb); }
        CHECK(completions == before); /* Local source lookup adds no debugger reads. */
        CHECK(strcmp(view->instructions[1].source, other_path) == 0);
        CHECK(tb_code_mixed_line(code, 0, &view->instructions[1], text, sizeof text) && !*text);
        CHECK(tb_code_mixed_line(code, 0, &view->instructions[2], text, sizeof text) && strcmp(text, "{") == 0);
        CHECK(!tb_code_mixed_line(code, 0, &view->instructions[3], text, sizeof text));
        CHECK(tb_browse_allocated(tb_code_cache(code)) <= TB_BROWSE_LIMIT);
    }
    revision = view->assembly_revision; uint64_t anchor = view->anchor;
    CHECK(tb_code_assembly_page(code, 0, 1)); ready(gdb, revision); CHECK(view->anchor > anchor);
    revision = view->assembly_revision; CHECK(tb_code_assembly_page(code, 0, -1)); ready(gdb, revision); CHECK(view->anchor == anchor);
    if (strcmp(mode, "context-code") == 0) {
        /* More than the former sixteen saved anchors, with no debugger reads
         * when traversing retained windows in reverse. */
        uint64_t anchors[21]; anchors[0] = view->anchor;
        for (size_t i = 1; i < 21; ++i) {
            revision = view->assembly_revision; CHECK(tb_code_assembly_page(code, 0, 1)); ready(gdb, revision);
            anchors[i] = view->anchor; CHECK(anchors[i] > anchors[i - 1]);
        }
        unsigned before = completions;
        for (size_t i = 20; i > 0; --i) {
            CHECK(tb_code_assembly_page(code, 0, -1)); CHECK(view->anchor == anchors[i - 1]);
        }
        CHECK(completions == before && tb_browse_allocated(tb_code_cache(code)) <= TB_BROWSE_LIMIT);
    }
    revision = view->assembly_revision; CHECK(tb_code_address(code, 0, "main")); ready(gdb, revision); CHECK(view->anchor == 0x2000);
    revision = view->assembly_revision; CHECK(tb_code_address(code, 0, "0x0")); ready(gdb, revision);
    CHECK(view->anchor == 0 && view->instruction_count && view->instructions[0].numeric == 0);
    CHECK(tb_code_open(code, 0, 1) && *view->source_error && view->line_count == 0);
    uint64_t manual_revision = view->assembly_revision;
    command(gdb, "-stop");
    for (int i = 0; i < 40; ++i) { pump(gdb); }
    CHECK(view->anchor == 0 && view->assembly_revision == manual_revision && !view->assembly_follow);
    CHECK(view->assembly_historical);
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
    revision = view->assembly_revision; CHECK(tb_code_address(code, 0, "0x5000")); ready(gdb, revision);
    revision = view->assembly_revision; CHECK(tb_code_assembly_page(code, 0, 1)); ready(gdb, revision);
    uint64_t forward_anchor = view->anchor;
    command(gdb, "-run"); for (int i = 0; i < 20; ++i) { pump(gdb); }
    CHECK(view->stale && !tb_code_address(code, 0, "main"));
    idle = completions;
    CHECK(tb_code_assembly_page(code, 0, -1) && view->anchor == 0x5000 && view->assembly_historical);
    CHECK(tb_code_assembly_page(code, 0, 1) && view->anchor == forward_anchor);
    CHECK(!tb_code_assembly_page(code, 0, 1));
    for (int i = 0; i < 20; ++i) { pump(gdb); }
    CHECK(completions == idle && view->anchor == forward_anchor);
    idle = completions; tb_code_request_files(code);
    for (int i = 0; i < 20; ++i) { pump(gdb); }
    CHECK(completions == idle);
    tb_gdb_close(gdb); until = tb_gdb_now_ms() + 2000;
    while (tb_gdb_state(gdb) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < until) { pump(gdb); }
    CHECK(tb_gdb_state(gdb) == TB_GDB_DISCONNECTED);
    tb_code_reset(code); CHECK(tb_code_file_count(code) == 0 && tb_code_view(code, 0)->line_count == 0 && tb_code_tab_count(code, 0) == 1);
    tb_code_destroy(code); tb_context_destroy(context); CHECK(tb_gdb_destroy(gdb)); code = NULL; context = NULL;
    CHECK(unlink(path) == 0 && unlink(other_path) == 0);
}
int main(int argc, char *argv[])
{
    CHECK(argc == 2); grammar(); run(argv[1], "context-code");
    run(argv[1], "context-code-short");
    run(argv[1], "context-code-stale"); run(argv[1], "context-code-fallback");
    run(argv[1], "context-code-multifile");
    CHECK(unsetenv("TB_FAKE_SOURCE") == 0 && unsetenv("TB_FAKE_SOURCE2") == 0 && unsetenv("TB_FAKE_MODE") == 0);
    puts("PASS M07 bounded source/disassembly, mixed/fallback, stale view replies, safe navigation, paging, source errors, control bytes and idle/running/invisible reads"); return 0;
}
