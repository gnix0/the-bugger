#define _POSIX_C_SOURCE 200809L
#include "native.h"
#include "../support/check.h"
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static void dump(TbNative *native)
{
    for (size_t i = 0; i < tb_native_log_count(native); ++i) { fprintf(stderr, "%s\n", tb_native_log_line(native, i)); }
    const TbCodeView *v = tb_code_view(tb_native_code(native), 0);
    const TbContextSnapshot *s = tb_context_snapshot(tb_native_context(native));
    fprintf(stderr, "state=%s ctx=%d/%d T%s #%d %s:%d PC=%s; code stale=%d lines=%zu insns=%zu src=%s asm=%s\n", tb_native_state_name(native), s->ready, s->stale, s->selected_thread, s->selected.level, s->selected.path, s->selected.line, s->selected.address, v->stale, v->line_count, v->instruction_count, v->source_error, v->assembly_error);
}
static void wait_state(TbNative *native, TbNativeState expected)
{
    uint64_t until = tb_gdb_now_ms() + 8000;
    while (tb_native_state(native) != expected && tb_gdb_now_ms() < until) { tb_native_pump(native, 5); }
    if (tb_native_state(native) != expected) { dump(native); CHECK(false); }
}
static void code_ready(TbNative *native, uint64_t after)
{
    const TbCodeView *v = tb_code_view(tb_native_code(native), 0);
    const TbContextSnapshot *s = tb_context_snapshot(tb_native_context(native));
    uint64_t until = tb_gdb_now_ms() + 10000;
    while ((s->stale || !s->ready || v->stale || v->assembly_revision <= after) && tb_gdb_now_ms() < until) { tb_native_pump(native, 5); }
    if (s->stale || !s->ready || v->stale || v->assembly_revision <= after) { dump(native); CHECK(false); }
}
static TbNative *start(const char *fixture, const char *command)
{
    TbNative *native = tb_native_create(); CHECK(native);
    TbNativeConfig config = {.gdb = "gdb", .executable = fixture};
    CHECK(tb_native_start(native, &config)); wait_state(native, TB_NATIVE_READY);
    tb_context_visible(tb_native_context(native), true, true);
    tb_code_visible(tb_native_code(native), 0, true, true);
    CHECK(tb_native_console(native, command)); wait_state(native, TB_NATIVE_STOPPED); code_ready(native, 0);
    return native;
}
static void close_session(TbNative *native)
{
    tb_native_close(native); wait_state(native, TB_NATIVE_CLOSED); CHECK(tb_native_destroy(native));
}
static void step(TbNative *native, TbStep action, bool instruction)
{
    uint64_t revision = tb_code_view(tb_native_code(native), 0)->assembly_revision;
    uint64_t stop = tb_context_snapshot(tb_native_context(native))->generation.stop;
    CHECK(tb_native_step(native, action, instruction));
    uint64_t until = tb_gdb_now_ms() + 8000;
    while ((tb_native_state(native) != TB_NATIVE_STOPPED || tb_context_snapshot(tb_native_context(native))->generation.stop == stop) && tb_gdb_now_ms() < until) { tb_native_pump(native, 5); }
    code_ready(native, revision);
}
static void ordinary(const char *fixture)
{
    TbNative *native = start(fixture, "start"); TbCode *code = tb_native_code(native);
    const TbContextSnapshot *s = tb_context_snapshot(tb_native_context(native));
    const TbCodeView *v = tb_code_view(code, 0);
    CHECK(strstr(s->selected.function, "main") && v->line_count > 10 && v->instruction_count > 0);
    CHECK(v->selected_line == s->selected.line && v->stopped_line == s->stopped.line);
    int call_line = tb_code_find(code, 0, "code_result = code_callee", 0) + 1; CHECK(call_line > 0);
    for (int i = 0; i < 6 && s->selected.line != call_line; ++i) { step(native, TB_STEP_OVER, false); }
    CHECK(s->selected.line == call_line);
    step(native, TB_STEP_INTO, false); CHECK(strstr(s->selected.function, "code_callee"));
    bool same_line = false;
    for (int i = 0; i < 3; ++i) {
        uint64_t anchor = v->anchor;
        int line = s->selected.line; char pc[48]; (void)snprintf(pc, sizeof pc, "%s", s->selected.address);
        step(native, TB_STEP_OVER, true);
        CHECK(strcmp(pc, s->selected.address) != 0);
        if (line == s->selected.line) { same_line = true; }
        CHECK(v->selected_line == s->selected.line);
        CHECK(v->anchor == anchor);
        bool retained = false;
        for (size_t row = 0; row < v->instruction_count; ++row) {
            if (strcmp(v->instructions[row].address, pc) == 0) { retained = true; }
        }
        CHECK(retained);
    }
    CHECK(same_line);
    bool att = false;
    for (size_t i = 0; i < v->instruction_count; ++i) { if (strchr(v->instructions[i].text, '%') || strchr(v->instructions[i].text, '$')) { att = true; } }
    CHECK(att);
    uint64_t revision = v->assembly_revision;
    tb_code_mixed(code, 0, true); code_ready(native, revision); CHECK(v->mixed_available);
    char stopped[48]; (void)snprintf(stopped, sizeof stopped, "%s", s->stopped.address);
    revision = v->assembly_revision; CHECK(tb_context_select_frame(tb_native_context(native), 1)); code_ready(native, revision);
    CHECK(s->selected.level == 1 && strcmp(stopped, s->stopped.address) == 0);
    revision = v->assembly_revision; CHECK(tb_code_stopped(code)); code_ready(native, revision);
    CHECK(s->selected.level == 0 && strcmp(stopped, s->selected.address) == 0);
    revision = v->assembly_revision; CHECK(tb_code_address(code, 0, "main")); code_ready(native, revision);
    CHECK(!v->assembly_follow && strcmp(stopped, s->stopped.address) == 0);
    CHECK(!tb_code_address(code, 0, "code_callee(1)")); CHECK(!tb_code_address(code, 0, "main; quit"));
    revision = v->assembly_revision; tb_code_return(code, 0); code_ready(native, revision);
    CHECK(v->assembly_follow);
    revision = v->assembly_revision; CHECK(tb_context_select_frame(tb_native_context(native), 1)); code_ready(native, revision);
    step(native, TB_STEP_OVER, true); CHECK(strstr(s->selected.function, "code_callee")); /* Actual execution frame, not inspected caller. */
    step(native, TB_STEP_OUT, false); CHECK(strstr(s->selected.function, "main"));
    CHECK(strcmp(stopped, s->stopped.address) != 0);
    (void)snprintf(stopped, sizeof stopped, "%s", s->stopped.address);
    revision = v->assembly_revision; uint64_t returned_window = v->anchor;
    CHECK(tb_code_assembly_page(code, 0, -1)); code_ready(native, revision);
    CHECK(v->anchor != returned_window && strcmp(stopped, s->stopped.address) == 0);
    close_session(native);
    puts("PASS M07 real source/AT&T/mixed, synchronized multi-instruction line, source/instruction stepping, caller/return-to-stop, read-only symbol navigation and Step Out");
}
static void degraded(const char *fixture, bool stripped)
{
    TbNative *native = start(fixture, stripped ? "starti" : "start"); TbCode *code = tb_native_code(native);
    const TbCodeView *v = tb_code_view(code, 0);
    CHECK(v->instruction_count > 0);
    if (stripped) { CHECK(v->line_count == 0 && *v->source_error); }
    uint64_t revision = v->assembly_revision; tb_code_mixed(code, 0, true); code_ready(native, revision);
    if (stripped) { CHECK(!v->mixed_available && v->instruction_count > 0); }
    step(native, TB_STEP_INTO, true);
    close_session(native);
    puts(stripped ? "PASS M07 stripped target retains assembly and instruction steps without fabricated source" : "PASS M07 optimized GDB mappings and instruction stepping");
}
static void cpp(const char *fixture)
{
    TbNative *native = start(fixture, "start"); TbCode *code = tb_native_code(native);
    uint64_t revision = tb_code_view(code, 0)->assembly_revision;
    CHECK(tb_code_address(code, 0, "code_object")); code_ready(native, revision);
    CHECK(tb_native_console(native, "print code_sideeffects"));
    bool zero = false; uint64_t until = tb_gdb_now_ms() + 4000;
    while (!zero && tb_gdb_now_ms() < until) {
        tb_native_pump(native, 5);
        for (size_t i = 0; i < tb_native_log_count(native); ++i) { if (strstr(tb_native_log_line(native, i), "$1 = 0")) { zero = true; } }
    }
    CHECK(zero); close_session(native);
    puts("PASS M07 C++ symbol address lookup does not invoke overloaded operator&");
}
static void copy_file(const char *source, const char *destination)
{
    int input = open(source, O_RDONLY), output = open(destination, O_WRONLY | O_CREAT | O_EXCL, 0600); CHECK(input >= 0 && output >= 0);
    char bytes[2048]; ssize_t count;
    while ((count = read(input, bytes, sizeof bytes)) > 0) { CHECK(write(output, bytes, (size_t)count) == count); }
    CHECK(count == 0 && close(input) == 0 && close(output) == 0);
}
static void relocated(const char *fixture, const char *sources, bool right_first)
{
    char directory[] = "/tmp/the bugger 'moved' \"quoted\" $()-XXXXXX"; CHECK(mkdtemp(directory));
    char left[512], right[512], from[1024], target[1024];
    (void)snprintf(left, sizeof left, "%s/left", directory); CHECK(mkdir(left, 0700) == 0);
    (void)snprintf(right, sizeof right, "%s/right", directory); CHECK(mkdir(right, 0700) == 0);
    const char *names[] = {"main.c", "left/duplicate.c", "right/duplicate.c"};
    for (size_t i = 0; i < 3; ++i) {
        (void)snprintf(from, sizeof from, "%s/%s", sources, names[i]);
        (void)snprintf(target, sizeof target, "%s/%s", directory, names[i]); copy_file(from, target);
    }
    TbNative *native = start(fixture, "start"); TbCode *code = tb_native_code(native);
    const TbCodeView *v = tb_code_view(code, 0);
    CHECK(v->line_count == 0 && *v->source_error);
    CHECK(tb_code_add_directory(code, directory));
    uint64_t until = tb_gdb_now_ms() + 4000;
    while (tb_code_directory_count(code) != 1 && tb_gdb_now_ms() < until) { tb_native_pump(native, 5); }
    CHECK(tb_code_directory_count(code) == 1);
    for (int i = 0; i < 50; ++i) { tb_native_pump(native, 5); }
    uint64_t revision = v->assembly_revision;
    CHECK(tb_code_add_rule(code, "/the bugger build tree", directory));
    until = tb_gdb_now_ms() + 5000;
    while (tb_code_rule_count(code) != 1 && tb_gdb_now_ms() < until) { tb_native_pump(native, 5); }
    CHECK(tb_code_rule_count(code) == 1); code_ready(native, revision);
    CHECK(v->line_count > 0 && strstr(v->path, directory));
    int matches = 0;
    size_t left_tab = 0, right_tab = 0;
    size_t file_count = tb_code_file_count(code);
    size_t files[2] = {file_count, file_count};
    for (size_t i = 0; i < file_count; ++i) {
        const TbCodeFile *file = tb_code_file(code, i);
        if (strstr(file->full, "/left/duplicate.c")) { files[0] = i; }
        if (strstr(file->full, "/right/duplicate.c")) { files[1] = i; }
    }
    CHECK(files[0] < file_count && files[1] < file_count && files[0] != files[1]);
    for (size_t step_index = 0; step_index < 2; ++step_index) {
        size_t i = files[right_first ? 1 - step_index : step_index];
        const TbCodeFile *file = tb_code_file(code, i);
        CHECK(strstr(file->full, directory)); CHECK(tb_code_open(code, 0, i));
        bool is_left = strstr(file->full, "/left/") != NULL;
        CHECK(is_left || strstr(file->full, "/right/"));
        if (is_left) { left_tab = tb_code_tab_active(code, 0); }
        else { right_tab = tb_code_tab_active(code, 0); }
        CHECK(tb_code_find(code, 0, is_left ? "LEFT_SOURCE_MARKER" : "RIGHT_SOURCE_MARKER", 0) >= 0); ++matches;
        TbBreakpoints *breaks = tb_native_breakpoints(native);
        until = tb_gdb_now_ms() + 5000;
        while (!tb_break_ready(breaks) && tb_gdb_now_ms() < until) { tb_native_pump(native, 5); }
        CHECK(tb_break_add_at(breaks, (TbBreakLocation){.source = file->full, .line = 3}, false)); until = tb_gdb_now_ms() + 5000;
        while (!tb_break_ready(breaks) && tb_gdb_now_ms() < until) { tb_native_pump(native, 5); }
        CHECK(tb_break_ready(breaks) && tb_break_count(breaks) == 1);
        const TbBreakpoint *bp = tb_break_row(breaks, 0);
        if (bp->pending || strcmp(bp->file, file->full) != 0) { dump(native); fprintf(stderr, "location=%s backend=%s\n", file->full, bp->file); CHECK(false); }
        CHECK(bp->line == 3); CHECK(tb_break_remove(breaks, bp->id)); until = tb_gdb_now_ms() + 5000;
        while (!tb_break_ready(breaks) && tb_gdb_now_ms() < until) { tb_native_pump(native, 5); }
        CHECK(tb_break_ready(breaks) && tb_break_count(breaks) == 0);
    }
    CHECK(matches == 2);
    CHECK(left_tab && right_tab && left_tab != right_tab);
    CHECK(tb_code_tab_count(code, 0) == 3 && tb_code_tab_active(code, 0) == 2);
    CHECK(strstr(tb_code_tab_file(code, 0, left_tab)->full, "/left/duplicate.c"));
    CHECK(strstr(tb_code_tab_file(code, 0, right_tab)->full, "/right/duplicate.c"));
    CHECK(tb_code_tab_select(code, 0, left_tab) && tb_code_find(code, 0, "LEFT_SOURCE_MARKER", 0) >= 0);
    CHECK(tb_code_tab_select(code, 0, right_tab) && tb_code_find(code, 0, "RIGHT_SOURCE_MARKER", 0) >= 0);
    TbBreakpoints *breaks = tb_native_breakpoints(native);
    uint64_t before_stop = tb_context_snapshot(tb_native_context(native))->generation.stop;
    uint64_t before_code = v->assembly_revision;
    CHECK(tb_native_run_at(native, (TbBreakLocation){.source = v->path, .line = 3}));
    until = tb_gdb_now_ms() + 5000;
    while ((tb_context_snapshot(tb_native_context(native))->generation.stop <= before_stop ||
            tb_native_state(native) != TB_NATIVE_STOPPED) && tb_gdb_now_ms() < until) { tb_native_pump(native, 5); }
    code_ready(native, before_code);
    CHECK(tb_context_snapshot(tb_native_context(native))->selected.line == 3);
    CHECK(tb_code_tab_active(code, 0) == right_tab && !v->source_follow && tb_code_find(code, 0, "RIGHT_SOURCE_MARKER", 0) >= 0);
    CHECK(tb_code_tab_select(code, 0, 0) && v->source_follow);
    CHECK(tb_code_tab_select(code, 0, 1) && tb_code_tab_close(code, 0, 1));
    CHECK(tb_code_tab_count(code, 0) == 2 && tb_code_tab_active(code, 0) == 0);
    until = tb_gdb_now_ms() + 5000;
    while (!tb_break_ready(breaks) && tb_gdb_now_ms() < until) { tb_native_pump(native, 5); }
    CHECK(tb_break_ready(breaks) && tb_break_count(breaks) == 0);
    CHECK(!tb_code_add_rule(code, "/bad\nquit", directory));
    CHECK(!tb_code_add_directory(code, "/no/such/source/directory"));
    revision = v->assembly_revision;
    CHECK(tb_code_add_rule(code, "/the bugger build tree/left", left));
    until = tb_gdb_now_ms() + 5000;
    while (tb_code_rule_count(code) != 2 && tb_gdb_now_ms() < until) { tb_native_pump(native, 5); }
    CHECK(tb_code_rule_count(code) == 2); code_ready(native, revision);
    CHECK(strcmp(tb_code_rule(code, 0)->from, "/the bugger build tree") == 0);
    revision = v->assembly_revision;
    CHECK(tb_code_add_rule(code, "/the bugger build tree", directory));
    code_ready(native, revision);
    CHECK(tb_code_rule_count(code) == 2);
    CHECK(strcmp(tb_code_rule(code, 1)->from, "/the bugger build tree") == 0);
    CHECK(tb_code_clear_settings(code));
    until = tb_gdb_now_ms() + 5000;
    while ((tb_code_rule_count(code) || tb_code_directory_count(code)) && tb_gdb_now_ms() < until) { tb_native_pump(native, 5); }
    CHECK(tb_code_rule_count(code) == 0 && tb_code_directory_count(code) == 0);
    close_session(native);
    for (size_t i = 0; i < 3; ++i) { (void)snprintf(target, sizeof target, "%s/%s", directory, names[i]); CHECK(unlink(target) == 0); }
    CHECK(rmdir(left) == 0 && rmdir(right) == 0 && rmdir(directory) == 0);
    puts(right_first ? "PASS M07 duplicate-basename tabs opened right-first, source recovery and mapping lifecycle" :
                       "PASS M07 duplicate-basename tabs opened left-first, source recovery and mapping lifecycle");
}
int main(int argc, char *argv[])
{
    CHECK(argc == 7);
    ordinary(argv[1]); degraded(argv[2], false); degraded(argv[3], true); cpp(argv[4]);
    relocated(argv[5], argv[6], false); relocated(argv[5], argv[6], true);
    return 0;
}
