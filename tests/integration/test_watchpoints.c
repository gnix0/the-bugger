#define _POSIX_C_SOURCE 200809L
#include "breakpoints.h"
#include "../support/check.h"
#include <string.h>

static TbBreakpoints *model;
static unsigned replies;
static void emit(void *unused, const TbGdbEvent *event)
{
    (void)unused; (void)tb_break_event(model, event);
    if (event->kind == TB_GDB_REPLY) { ++replies; }
}
static void pump(TbGdb *gdb) { tb_gdb_pump(gdb, 2); tb_break_pump(model); }
static void idle(TbGdb *gdb)
{
    uint64_t end = tb_gdb_now_ms() + 2000;
    while ((!tb_break_ready(model) || tb_break_busy(model)) && tb_gdb_now_ms() < end) { pump(gdb); }
    if (!tb_break_ready(model) || tb_break_busy(model)) { fprintf(stderr, "%s\n", tb_break_message(model)); }
    CHECK(tb_break_ready(model) && !tb_break_busy(model));
}
static void command(TbGdb *gdb, const char *operation, const char *argument)
{
    unsigned before = replies; const char *args[] = {argument};
    CHECK(tb_gdb_request(gdb, operation, args, argument ? 1 : 0, TB_GDB_SESSION, 777, 1000, NULL));
    uint64_t end = tb_gdb_now_ms() + 1500;
    while (replies == before && tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(replies > before);
}
static void blocked(TbGdb *gdb, const char *reason)
{
    uint64_t end = tb_gdb_now_ms() + 2000;
    while (!strstr(tb_break_message(model), reason) && tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(strstr(tb_break_message(model), reason) && tb_break_busy(model));
    unsigned before = replies;
    for (int i = 0; i < 20; ++i) { pump(gdb); }
    CHECK(replies <= before + 1); /* No cleanup retry spin. */
}
int main(int argc, char **argv)
{
    CHECK(argc == 2 || (argc == 3 && strcmp(argv[2], "--slow") == 0)); CHECK(setenv("TB_FAKE_MODE", "data-watch", 1) == 0);
    TbGdb *gdb = tb_gdb_create(emit, NULL); CHECK(gdb); model = tb_break_create(gdb); CHECK(model);
    CHECK(tb_gdb_start(gdb, argv[1], 1000)); idle(gdb);
    CHECK(!tb_break_watch(model, "watched", TB_WATCH_CHANGE)); /* Loaded, not stopped. */
    command(gdb, "-stop", NULL); idle(gdb);
    CHECK(!tb_break_watch(model, "side_effect()", TB_WATCH_CHANGE));
    CHECK(!tb_break_watch(model, "watched=3", TB_WATCH_CHANGE));
    CHECK(!tb_break_watch_address(model, UINT64_MAX, 8, TB_WATCH_CHANGE));
    CHECK(!tb_break_watch_address(model, 0x20000000, 3, TB_WATCH_CHANGE));
    for (int i = 0; i < TB_WATCH_MODES; ++i) {
        CHECK(tb_break_watch(model, "watched", (TbWatchMode)i)); CHECK(tb_break_busy(model)); idle(gdb);
        CHECK(tb_break_count(model) == 2);
        const TbBreakpoint *row = tb_break_row(model, 1);
        CHECK(row->watchpoint && row->hardware && row->watch_mode == (TbWatchMode)i && strcmp(row->location, "watched") == 0);
        command(gdb, "-data-hit", NULL); idle(gdb); CHECK(strstr(tb_break_message(model), "17 -> 20"));
        CHECK(tb_break_remove(model, row->id)); idle(gdb); CHECK(tb_break_count(model) == 1);
    }
    /* Cortex-M-style address, without assuming a host mapping or reading it. */
    CHECK(tb_break_watch_address(model, 0x20000000, 4, TB_WATCH_ACCESS)); idle(gdb);
    CHECK(strstr(tb_break_row(model, 1)->location, "unsigned int"));
    command(gdb, "-data-downgrade", NULL); idle(gdb);
    CHECK(tb_break_count(model) == 1 && strstr(tb_break_message(model), "no software fallback"));
    CHECK(tb_break_watch_address(model, 0x20000000, 4, TB_WATCH_ACCESS)); idle(gdb);
    command(gdb, "-data-scope", NULL); idle(gdb);
    CHECK(tb_break_count(model) == 1 && strstr(tb_break_message(model), "watchpoint-scope"));
    command(gdb, "-data-mode", "1");
    CHECK(tb_break_watch(model, "watched", TB_WATCH_CHANGE)); idle(gdb);
    CHECK(tb_break_count(model) == 1 && strstr(tb_break_message(model), "no software fallback"));
    command(gdb, "-data-mode", "2");
    CHECK(tb_break_watch(model, "watched", TB_WATCH_READ)); idle(gdb);
    CHECK(tb_break_count(model) == 1 && strstr(tb_break_message(model), "unsupported"));
    command(gdb, "-data-mode", "4");
    CHECK(tb_break_watch(model, "watched", TB_WATCH_CHANGE)); idle(gdb); CHECK(tb_break_count(model) == 1);
    command(gdb, "-data-mode", "5");
    CHECK(tb_break_watch(model, "watched", TB_WATCH_CHANGE)); idle(gdb); CHECK(tb_break_count(model) == 1);
    command(gdb, "-data-mode", "8");
    CHECK(tb_break_watch(model, "watched", TB_WATCH_CHANGE)); idle(gdb); CHECK(tb_break_count(model) == 1);
    command(gdb, "-data-mode", "7");
    CHECK(tb_break_watch(model, "watched", TB_WATCH_CHANGE)); blocked(gdb, "Malformed");
    command(gdb, "-data-mode", "0"); tb_break_refresh(model); idle(gdb);
    CHECK(tb_break_count(model) == 2); CHECK(tb_break_remove(model, "2")); idle(gdb);
    command(gdb, "-data-mode", "3");
    CHECK(tb_break_watch(model, "watched", TB_WATCH_CHANGE)); blocked(gdb, "cleanup failed");
    command(gdb, "-data-mode", "0"); tb_break_refresh(model); idle(gdb); CHECK(tb_break_count(model) == 1);
    if (argc == 3) {
        command(gdb, "-data-mode", "12"); CHECK(tb_break_watch(model, "watched", TB_WATCH_CHANGE));
        uint64_t deadline = tb_gdb_now_ms() + 5500;
        while ((!tb_break_ready(model) || tb_break_busy(model)) && tb_gdb_now_ms() < deadline) { pump(gdb); }
        idle(gdb); CHECK(tb_break_count(model) == 1);
        command(gdb, "-data-release", NULL); idle(gdb); CHECK(tb_break_count(model) == 1);
    }
    command(gdb, "-data-mode", "6");
    CHECK(tb_break_watch(model, "watched", TB_WATCH_CHANGE)); blocked(gdb, "Ambiguous");
    CHECK(tb_break_row(model, 0)->hits == 9); /* Never delete unrelated user entries. */
    tb_gdb_close(gdb); uint64_t end = tb_gdb_now_ms() + 2000;
    while (tb_gdb_state(gdb) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(tb_gdb_state(gdb) == TB_GDB_DISCONNECTED); tb_break_destroy(model); CHECK(tb_gdb_destroy(gdb));
    CHECK(unsetenv("TB_FAKE_MODE") == 0);
    puts("PASS M12 hardware change/read/access verification, address widths, scope expiry, fallback/downgrade removal, target errors, context races, uncertain ownership and blocked cleanup without retry spin");
    return EXIT_SUCCESS;
}
