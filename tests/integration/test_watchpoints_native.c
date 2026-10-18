#include "native.h"
#include "../support/check.h"
#include <string.h>

static void idle(TbNative *n)
{
    TbBreakpoints *b = tb_native_breakpoints(n); uint64_t end = tb_gdb_now_ms() + 5000;
    while ((!tb_break_ready(b) || tb_break_busy(b)) && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    if (!tb_break_ready(b) || tb_break_busy(b)) { fprintf(stderr, "%s\n", tb_break_message(b)); }
    CHECK(tb_break_ready(b) && !tb_break_busy(b));
}
static void stop(TbNative *n, uint64_t previous)
{
    const TbContextSnapshot *s = tb_context_snapshot(tb_native_context(n));
    uint64_t end = tb_gdb_now_ms() + 6000;
    do {
        tb_native_pump(n, 5);
        if (tb_native_state(n) == TB_NATIVE_STOPPED && s->ready && !s->stale && s->generation.stop > previous) { idle(n); return; }
    } while (tb_gdb_now_ms() < end);
    fprintf(stderr, "Stop failed: %s / %s\n", tb_native_message(n), tb_break_message(tb_native_breakpoints(n))); CHECK(false);
}
static TbNative *start(const char *fixture, bool scope)
{
    TbNative *n = tb_native_create(); CHECK(n);
    TbNativeConfig config = {.gdb = "gdb", .executable = fixture}; CHECK(tb_native_start(n, &config));
    uint64_t end = tb_gdb_now_ms() + 5000;
    while (tb_native_state(n) != TB_NATIVE_READY && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    CHECK(tb_native_state(n) == TB_NATIVE_READY);
    tb_context_visible(tb_native_context(n), false, true);
    TbBreakpoints *b = tb_native_breakpoints(n); idle(n);
    CHECK(tb_break_add(b, scope ? "watch_scope" : "watch_checkpoint", false)); idle(n);
    CHECK(tb_native_continue(n)); stop(n, 0);
    return n;
}
static void close_session(TbNative *n)
{
    tb_native_close(n); uint64_t end = tb_gdb_now_ms() + 5000;
    while (tb_native_active(n) && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    CHECK(!tb_native_active(n) && tb_native_destroy(n));
}
static bool log_has(TbNative *n, const char *text)
{
    for (size_t i = 0; i < tb_native_log_count(n); ++i) { if (strstr(tb_native_log_line(n, i), text)) { return true; } }
    return false;
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    for (int mode = 0; mode < TB_WATCH_MODES; ++mode) {
        TbNative *n = start(argv[1], false); TbBreakpoints *b = tb_native_breakpoints(n);
        CHECK(tb_break_watch(b, "watched_value", (TbWatchMode)mode));
        CHECK(!tb_native_continue(n)); idle(n);
        CHECK(tb_break_count(b) == 2);
        const TbBreakpoint *row = tb_break_row(b, 1);
        CHECK(row->watchpoint && row->hardware && row->watch_mode == (TbWatchMode)mode);
        char id[32]; (void)snprintf(id, sizeof id, "%s", row->id);
        CHECK(tb_break_enable(b, id, false)); idle(n); CHECK(!tb_break_row(b, 1)->enabled);
        CHECK(tb_break_enable(b, id, true)); idle(n); CHECK(tb_break_row(b, 1)->enabled);
        uint64_t previous = tb_context_snapshot(tb_native_context(n))->generation.stop;
        CHECK(tb_native_continue(n)); stop(n, previous);
        CHECK(log_has(n, "watchpoint-trigger")); CHECK(tb_break_row(b, 1)->hits > 0);
        CHECK(tb_break_remove(b, id)); idle(n); CHECK(tb_break_count(b) == 1);
        /* Deliberately force GDB's software mode to prove frontend rejection. */
        CHECK(tb_native_console(n, "set can-use-hw-watchpoints 0"));
        uint64_t end = tb_gdb_now_ms() + 5000;
        while (strcmp(tb_native_message(n), "GDB command completed.") != 0 && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
        CHECK(strcmp(tb_native_message(n), "GDB command completed.") == 0);
        idle(n); CHECK(tb_break_watch(b, "watched_value", TB_WATCH_CHANGE)); idle(n);
        CHECK(tb_break_count(b) == 1 && strstr(tb_break_message(b), "no software fallback"));
        CHECK(tb_break_watch(b, "watched_value", TB_WATCH_READ)); idle(n);
        CHECK(tb_break_count(b) == 1 && *tb_break_message(b)); /* Hardware disabled: no read-watchpoint fallback. */
        close_session(n);
    }
    TbNative *n = start(argv[1], true); TbBreakpoints *b = tb_native_breakpoints(n);
    uint64_t previous = tb_context_snapshot(tb_native_context(n))->generation.stop;
    CHECK(tb_native_step(n, TB_STEP_OVER, false)); stop(n, previous); /* Initialize local. */
    CHECK(tb_break_watch(b, "local", TB_WATCH_CHANGE)); idle(n);
    CHECK(tb_break_count(b) == 2 && tb_break_row(b, 1)->hardware);
    previous = tb_context_snapshot(tb_native_context(n))->generation.stop;
    CHECK(tb_native_continue(n)); stop(n, previous);
    CHECK(log_has(n, "watchpoint-trigger"));
    previous = tb_context_snapshot(tb_native_context(n))->generation.stop;
    CHECK(tb_native_continue(n)); stop(n, previous);
    CHECK(log_has(n, "watchpoint-scope") && tb_break_count(b) == 1);
    close_session(n);
    puts("PASS M12 native GDB hardware value-change/read/access stops, hit counts, enable/disable/remove, local scope expiry, execution interlock and forced software-fallback cleanup");
    return EXIT_SUCCESS;
}
