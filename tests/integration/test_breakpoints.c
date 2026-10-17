#define _POSIX_C_SOURCE 200809L
#include "breakpoints.h"
#include "../support/check.h"
#include <string.h>

static TbBreakpoints *breakpoints;
static unsigned replies;
static bool unconfirmed;
static void emit(void *unused, const TbGdbEvent *event)
{
    (void)unused; (void)tb_break_event(breakpoints, event);
    if (event->kind == TB_GDB_REPLY) { ++replies; }
    if (event->kind == TB_GDB_DIAGNOSTIC && event->text && strstr(event->text, "Could not confirm stopped")) { unconfirmed = true; }
}
static void pump(TbGdb *gdb) { tb_gdb_pump(gdb, 5); tb_break_pump(breakpoints); }
static void idle(TbGdb *gdb)
{
    uint64_t end = tb_gdb_now_ms() + 6000;
    while ((!tb_break_ready(breakpoints) || tb_break_busy(breakpoints)) && tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(tb_break_ready(breakpoints) && !tb_break_busy(breakpoints));
}
static void command(TbGdb *gdb, const char *op, const char *argument)
{
    unsigned before = replies; const char *args[] = {argument};
    CHECK(tb_gdb_request(gdb, op, args, argument ? 1 : 0, TB_GDB_SESSION, 999, 1000, NULL));
    uint64_t end = tb_gdb_now_ms() + 2000;
    while (replies == before && tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(replies > before);
}
static void run(const char *peer, const char *mode)
{
    CHECK(setenv("TB_FAKE_MODE", mode, 1) == 0);
    unconfirmed = false;
    TbGdb *gdb = tb_gdb_create(emit, NULL); CHECK(gdb);
    breakpoints = tb_break_create(gdb); CHECK(breakpoints); CHECK(tb_gdb_start(gdb, peer, 1000));
    idle(gdb); CHECK(tb_break_count(breakpoints) == 5);
    CHECK(tb_break_row(breakpoints, 1)->multiple && tb_break_row(breakpoints, 1)->hardware);
    CHECK(tb_break_row(breakpoints, 2)->child && tb_break_row(breakpoints, 2)->enabled && tb_break_row(breakpoints, 2)->hardware);
    CHECK(!tb_break_row(breakpoints, 3)->enabled && tb_break_row(breakpoints, 4)->pending);
    CHECK(tb_break_at(breakpoints, "/source.c", 7, NULL));
    CHECK(!tb_break_remove(breakpoints, "1\n-break-delete 2"));
    CHECK(tb_break_add(breakpoints, "main", true)); idle(gdb);
    CHECK(strstr(tb_break_message(breakpoints), "slots exhausted") && tb_break_count(breakpoints) == 5);
    command(gdb, "-stop", NULL); CHECK(tb_gdb_state(gdb) == TB_GDB_STOPPED); idle(gdb);
    CHECK(tb_break_run_to(breakpoints, "main", "1"));
    CHECK(!tb_break_run_to(breakpoints, "main", "1"));
    if (strcmp(mode, "bp-late-error-running") == 0 || strcmp(mode, "bp-late-error-unknown") == 0) {
        uint64_t end = tb_gdb_now_ms() + 3000;
        while (!unconfirmed && tb_gdb_now_ms() < end) { pump(gdb); }
        CHECK(unconfirmed && tb_gdb_state(gdb) == TB_GDB_RUNNING);
        command(gdb, "-stop", NULL);
    }
    if (strcmp(mode, "bp-clean-error") == 0) {
        uint64_t end = tb_gdb_now_ms() + 5000;
        while (!strstr(tb_break_message(breakpoints), "cleanup failed") && tb_gdb_now_ms() < end) { pump(gdb); }
        CHECK(strstr(tb_break_message(breakpoints), "cleanup failed") && tb_break_busy(breakpoints));
        unsigned count = replies; for (int i = 0; i < 30; ++i) { pump(gdb); }
        CHECK(replies <= count + 1); /* one reconciliation read, no retry spin */
        CHECK(!tb_break_run_to(breakpoints, "main", "1")); tb_break_refresh(breakpoints);
    }
    if (strcmp(mode, "bp-timeout-ambiguous") == 0) {
        uint64_t deadline = tb_gdb_now_ms() + 5000;
        while (!strstr(tb_break_message(breakpoints), "Ambiguous") && tb_gdb_now_ms() < deadline) { pump(gdb); }
        CHECK(strstr(tb_break_message(breakpoints), "Ambiguous") && tb_break_busy(breakpoints));
        CHECK(tb_gdb_state(gdb) == TB_GDB_STOPPED);
        unsigned count = replies; for (int i = 0; i < 30; ++i) { pump(gdb); } CHECK(replies == count);
        tb_gdb_close(gdb); deadline = tb_gdb_now_ms() + 3000;
        while (tb_gdb_state(gdb) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < deadline) { pump(gdb); }
        CHECK(tb_gdb_state(gdb) == TB_GDB_DISCONNECTED); tb_break_destroy(breakpoints); CHECK(tb_gdb_destroy(gdb)); breakpoints = NULL;
        return;
    }
    idle(gdb); CHECK(tb_break_count(breakpoints) == 5);
    if (strcmp(mode, "bp-race") == 0) { CHECK(!strstr(tb_break_message(breakpoints), "incorrectly resumed")); }
    CHECK(tb_break_row(breakpoints, 0)->hits == 9); /* original user entry survives every path */
    command(gdb, "-bp-table", "1"); tb_break_refresh(breakpoints); idle(gdb);
    CHECK(tb_break_count(breakpoints) == TB_BREAK_ROWS);
    command(gdb, "-bp-table", "2"); tb_break_refresh(breakpoints);
    uint64_t revision = tb_break_revision(breakpoints), end = tb_gdb_now_ms() + 2000;
    while (tb_break_revision(breakpoints) == revision && tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(!tb_break_ready(breakpoints));
    unsigned before = replies; for (int i = 0; i < 30; ++i) { pump(gdb); } CHECK(replies == before);
    command(gdb, "-bp-table", "0"); tb_break_refresh(breakpoints); idle(gdb); CHECK(tb_break_count(breakpoints) == 5);
    tb_gdb_close(gdb); end = tb_gdb_now_ms() + 3000;
    while (tb_gdb_state(gdb) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(tb_gdb_state(gdb) == TB_GDB_DISCONNECTED); tb_break_destroy(breakpoints); CHECK(tb_gdb_destroy(gdb)); breakpoints = NULL;
}
int main(int argc, char *argv[])
{
    CHECK(argc == 2);
    run(argv[1], "bp-normal"); run(argv[1], "bp-race"); run(argv[1], "bp-resume-error"); run(argv[1], "bp-timeout");
    run(argv[1], "bp-timeout-silent"); run(argv[1], "bp-clean-error");
    run(argv[1], "bp-timeout-ambiguous");
    run(argv[1], "bp-late-error");
    run(argv[1], "bp-late-error-running"); run(argv[1], "bp-late-error-unknown");
    CHECK(unsetenv("TB_FAKE_MODE") == 0);
    puts("PASS M08 multiple/pending/hardware rows, resource errors, run-to intervening stops/races/timeout/resume failure, 301-row bound and malformed-table recovery without busy polling");
    return EXIT_SUCCESS;
}
