#define _POSIX_C_SOURCE 200809L
#include "breakpoints.h"
#include "../support/check.h"
#include <string.h>

static TbBreakpoints *b;
static unsigned replies, command_replies, inserts, enables, queries;
static void event(void *unused, const TbGdbEvent *e)
{
    (void)unused; (void)tb_break_event(b, e);
    if (e->kind == TB_GDB_REPLY) {
        ++replies; if (e->cookie == 777) { ++command_replies; }
        const TbMiValue *v = e->record ? tb_mi_find(e->record->results, "inserts") : NULL;
        if (v) {
            inserts = (unsigned)strtoul(v->string.data, NULL, 10);
            enables = (unsigned)strtoul(tb_mi_find(e->record->results, "enables")->string.data, NULL, 10);
            queries = (unsigned)strtoul(tb_mi_find(e->record->results, "queries")->string.data, NULL, 10);
        }
    }
}
static void pump(TbGdb *g) { tb_gdb_pump(g, 5); tb_break_pump(b); }
static void command(TbGdb *g, const char *op, const char *argument)
{
    unsigned before = command_replies; const char *args[] = {argument};
    CHECK(tb_gdb_request(g, op, args, argument ? 1 : 0, TB_GDB_SESSION, 777, 1000, NULL));
    uint64_t end = tb_gdb_now_ms() + 2000;
    while (command_replies == before && tb_gdb_now_ms() < end) { pump(g); }
    CHECK(command_replies > before);
}
static void settled(TbGdb *g)
{
    uint64_t end = tb_gdb_now_ms() + 6000;
    while ((tb_break_restoration_active(b) || !tb_break_ready(b)) && tb_gdb_now_ms() < end) { pump(g); }
    CHECK(!tb_break_restoration_active(b));
}
static void run(const char *peer, unsigned scenario)
{
    CHECK(setenv("TB_FAKE_MODE", "f01", 1) == 0);
    TbGdb *g = tb_gdb_create(event, NULL); CHECK(g); b = tb_break_create(g); CHECK(b);
    CHECK(tb_gdb_start(g, peer, 1000)); uint64_t end = tb_gdb_now_ms() + 3000;
    while (!tb_break_ready(b) && tb_gdb_now_ms() < end) { pump(g); }
    CHECK(tb_break_ready(b)); char mode[16]; (void)snprintf(mode, sizeof mode, "%u", scenario == 12 ? 1 : scenario);
    command(g, "-fcase", mode);
    if (scenario == 9 || scenario == 10) { tb_break_refresh(b); for (int i = 0; i < 30; ++i) { pump(g); } }
    TbSetupBreak d = {.id = 7, .location = "main", .enabled = true, .condition = "1", .ignore_set = true, .ignore = 2, .hardware = scenario == 7};
    TbSetupBreak address = {.id = 9, .location = "*0x1000", .enabled = true};
    CHECK(tb_break_restore(b, &address, false)); CHECK(tb_break_restoration(b, 9)->state == TB_RESTORE_ADDRESS);
    CHECK(!tb_break_restoration_active(b) && tb_break_definition_count(b) == 0);
    CHECK(tb_break_restore(b, &d, false)); CHECK(tb_break_definition_count(b) == 0);
    if (scenario == 9 || scenario == 10) {
        end = tb_gdb_now_ms() + 5000;
        while (tb_break_restoration_active(b) && tb_gdb_now_ms() < end) { pump(g); }
        CHECK(!tb_break_restoration_active(b) && tb_break_restoration(b, 7)->state == TB_RESTORE_UNRESOLVED);
        for (int i = 0; i < 30; ++i) { pump(g); }
        command(g, "-fstats", NULL); unsigned previous = queries;
        for (int i = 0; i < 30; ++i) { pump(g); }
        command(g, "-fstats", NULL); CHECK(queries == previous && enables == 0 && inserts == (scenario == 9 ? 1 : 0));
    } else if (scenario == 8) {
        tb_break_pump(b); CHECK(tb_break_restoration_active(b)); tb_break_restore_cancel(b);
        CHECK(tb_break_restoration_active(b)); settled(g);
        CHECK(tb_break_restoration(b, 7)->state == TB_RESTORE_UNRESOLVED);
    } else if (scenario == 4 || scenario == 7) {
        end = tb_gdb_now_ms() + 6000;
        while (!tb_break_restoration_uncertain(b) && tb_gdb_now_ms() < end) { pump(g); }
        CHECK(tb_break_restoration_uncertain(b) && tb_break_busy(b));
        for (int i = 0; i < 30; ++i) { pump(g); }
        command(g, "-fstats", NULL); unsigned previous = queries;
        for (int i = 0; i < 30; ++i) { pump(g); }
        command(g, "-fstats", NULL); CHECK(queries == previous && inserts == 1 && enables == 0);
        CHECK(tb_break_reconcile(b)); for (int i = 0; i < 30; ++i) { pump(g); }
        CHECK(tb_break_restoration_uncertain(b));
        if (scenario == 4) { command(g, "-fcase", "0"); CHECK(tb_break_reconcile(b)); settled(g); }
    } else {
        settled(g);
        const TbBreakRestoration *r = tb_break_restoration(b, 7); CHECK(r);
        if (scenario == 1 || scenario == 2 || scenario == 12) {
            CHECK(r->state == TB_RESTORE_UNRESOLVED && !tb_break_row(b, 0)->enabled);
            CHECK(tb_break_definition_count(b) == 0);
            command(g, "-fstats", NULL); CHECK(inserts == 1 && enables == 0);
            command(g, "-fcase", scenario == 12 ? "11" : "0"); CHECK(tb_break_restore(b, &d, false)); settled(g);
        }
        CHECK(tb_break_restoration(b, 7)->state == TB_RESTORE_INSTALLED);
        CHECK(tb_break_definition_count(b) == 1 && tb_break_definition(b, 0)->enabled);
        CHECK(tb_break_definition(b, 0)->ignore_set && tb_break_definition(b, 0)->ignore == 2);
        command(g, "-fstats", NULL); CHECK(inserts == (scenario == 12 ? 2 : 1) && enables == 1);
        TbBreakDefinition imported; CHECK(tb_break_importable(b, "1", &imported));
        CHECK(!imported.ignore_set && !imported.hardware); /* Never infer historical policy/count. */
        command(g, "-fcase", "5"); tb_break_refresh(b);
        end = tb_gdb_now_ms() + 2000; while (!tb_break_ready(b) && tb_gdb_now_ms() < end) { pump(g); }
        CHECK(!tb_break_importable(b, "1", &imported) && !tb_break_definitions_ready(b));
        command(g, "-fcase", "6"); tb_break_refresh(b);
        end = tb_gdb_now_ms() + 2000; while (!tb_break_ready(b) && tb_gdb_now_ms() < end) { pump(g); }
        CHECK(!tb_break_importable(b, "1", &imported));
    }
    tb_gdb_close(g); end = tb_gdb_now_ms() + 4000;
    while (tb_gdb_state(g) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < end) { pump(g); }
    CHECK(tb_gdb_state(g) == TB_GDB_DISCONNECTED); tb_break_destroy(b); CHECK(tb_gdb_destroy(g)); b = NULL;
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    run(argv[1], 0); run(argv[1], 1); run(argv[1], 2); run(argv[1], 3); run(argv[1], 4); run(argv[1], 7); run(argv[1], 8); run(argv[1], 9); run(argv[1], 10); run(argv[1], 12);
    CHECK(unsetenv("TB_FAKE_MODE") == 0);
    puts("PASS F01 disabled-first breakpoint configuration, intermediate states, condition/ignore failure, no duplicate retry, bounded complete/incomplete reconciliation, hardware verification, cancellation and whole-entry import exclusions (synthetic MI)");
    return EXIT_SUCCESS;
}
