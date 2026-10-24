#define _POSIX_C_SOURCE 200809L
#include "registers.h"
#include "../support/check.h"
#include <string.h>

static TbContext *context;
static TbRegisters *registers;
static unsigned replies;
static void emit(void *unused, const TbGdbEvent *event)
{
    (void)unused;
    if (!tb_registers_event(registers, event)) { (void)tb_context_event(context, event); }
    if (event->kind == TB_GDB_REPLY) { ++replies; }
}
static void pump(TbGdb *gdb)
{
    tb_gdb_pump(gdb, 2); tb_context_pump(context); tb_registers_pump(registers);
}
static void command(TbGdb *gdb, const char *op)
{
    unsigned before = replies;
    CHECK(tb_gdb_request(gdb, op, NULL, 0, TB_GDB_SESSION, 999, 1000, NULL));
    uint64_t end = tb_gdb_now_ms() + 2000;
    while (replies == before && tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(replies > before);
}
static void stopped(TbGdb *gdb, uint64_t previous_stop)
{
    uint64_t end = tb_gdb_now_ms() + 2000;
    while ((tb_gdb_state(gdb) != TB_GDB_STOPPED || tb_gdb_generation(gdb).stop == previous_stop) &&
           tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(tb_gdb_state(gdb) == TB_GDB_STOPPED);
    CHECK(tb_gdb_generation(gdb).stop != previous_stop);
}
static void ready(TbGdb *gdb)
{
    uint64_t end = tb_gdb_now_ms() + 3000;
    while (tb_gdb_now_ms() < end) {
        tb_registers_want(registers, 0, 8); pump(gdb);
        const TbRegister *row = tb_registers_row(registers, 0);
        if (!tb_registers_stale(registers) && row && row->fresh && !tb_gdb_pending(gdb)) { return; }
    }
    fprintf(stderr, "registers not ready: %s\n", tb_registers_error(registers)); CHECK(false);
}
static void run(const char *peer, const char *mode)
{
    CHECK(setenv("TB_FAKE_MODE", mode, 1) == 0);
    TbGdb *gdb = tb_gdb_create(emit, NULL); CHECK(gdb);
    context = tb_context_create(gdb); registers = tb_registers_create(gdb, context); CHECK(context && registers);
    CHECK(tb_gdb_start(gdb, peer, 1000)); uint64_t end = tb_gdb_now_ms() + 2000;
    while (tb_gdb_state(gdb) != TB_GDB_READY && tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(tb_gdb_state(gdb) == TB_GDB_READY);
    tb_registers_visible(registers, true); tb_context_visible(context, false, true);
    uint64_t previous_stop = tb_gdb_generation(gdb).stop;
    command(gdb, "-stop"); stopped(gdb, previous_stop); ready(gdb);
    const TbRegister *row = tb_registers_row(registers, 0);
    CHECK(row->number == 0 && row->available && !row->changed && strcmp(row->value, "0x10") == 0);
    if (strstr(mode, "bounds")) { CHECK(tb_registers_count(registers) == TB_REGISTERS_MAX && tb_registers_limited(registers)); }
    else {
        CHECK(tb_registers_count(registers) == 4);
        CHECK(strcmp(row->name, strstr(mode, "arm") ? "r0" : "rax") == 0);
        CHECK(tb_registers_row(registers, 1)->number == 2); /* preserve unnamed-register holes */
        CHECK(tb_registers_row(registers, 2)->truncated && !tb_registers_row(registers, 2)->changed);
        CHECK(!tb_registers_row(registers, 3)->available);
    }
    unsigned before = replies; for (int i = 0; i < 12; ++i) { pump(gdb); } CHECK(replies == before);
    command(gdb, "-register-change"); command(gdb, "-run"); CHECK(tb_registers_stale(registers));
    before = replies; for (int i = 0; i < 12; ++i) { pump(gdb); } CHECK(replies == before);
    previous_stop = tb_gdb_generation(gdb).stop;
    command(gdb, "-stop"); stopped(gdb, previous_stop); ready(gdb);
    CHECK(row->changed && strcmp(row->value, "0x20") == 0);
    tb_registers_format(registers, TB_REG_DECIMAL); tb_registers_pump(registers);
    tb_registers_format(registers, TB_REG_HEX); ready(gdb);
    CHECK(strcmp(row->value, "0x20") == 0 && row->changed); /* obsolete format reply rejected */
    tb_registers_format(registers, TB_REG_DECIMAL); ready(gdb);
    CHECK(row->changed && strcmp(row->value, "32") == 0); /* format is not a target change */
    tb_registers_format(registers, TB_REG_HEX); ready(gdb); CHECK(row->changed);
    CHECK(tb_context_select_frame(context, 1)); ready(gdb);
    CHECK(!row->changed && strcmp(row->value, "0x21") == 0);
    command(gdb, "-register-error"); tb_registers_refresh(registers);
    end = tb_gdb_now_ms() + 1000;
    while (!*tb_registers_error(registers) && tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(*tb_registers_error(registers) && !row->fresh);
    before = replies; for (int i = 0; i < 12; ++i) { pump(gdb); } CHECK(replies == before);
    command(gdb, "-register-error"); tb_registers_refresh(registers); ready(gdb);
    command(gdb, "-register-malformed"); tb_registers_refresh(registers);
    end = tb_gdb_now_ms() + 1000;
    while (!*tb_registers_error(registers) && tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(strstr(tb_registers_error(registers), "Malformed"));
    command(gdb, "-register-malformed"); tb_registers_refresh(registers); ready(gdb);
    tb_registers_visible(registers, false); tb_registers_refresh(registers);
    before = replies; for (int i = 0; i < 12; ++i) { pump(gdb); } CHECK(replies == before);
    command(gdb, "-register-change"); command(gdb, "-run");
    previous_stop = tb_gdb_generation(gdb).stop;
    /* Acknowledgement alone must not end the hidden interval: the stop is async. */
    command(gdb, "-stop-held");
    CHECK(tb_gdb_state(gdb) == TB_GDB_RUNNING && tb_gdb_generation(gdb).stop == previous_stop);
    command(gdb, "-release-stop"); stopped(gdb, previous_stop);
    tb_registers_visible(registers, true); ready(gdb); CHECK(!row->changed);
    CHECK(tb_context_select_thread(context, "42")); ready(gdb); CHECK(!row->changed);
    tb_gdb_close(gdb); end = tb_gdb_now_ms() + 3000;
    while (tb_gdb_state(gdb) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(tb_gdb_state(gdb) == TB_GDB_DISCONNECTED);
    tb_registers_destroy(registers); tb_context_destroy(context); CHECK(tb_gdb_destroy(gdb));
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    run(argv[1], "context-registers-x86"); run(argv[1], "context-registers-arm"); run(argv[1], "context-registers-bounds");
    CHECK(unsetenv("TB_FAKE_MODE") == 0);
    puts("PASS M09 discovered x86/ARM registers, sparse IDs, bounds, unavailable/long values, formats/change baselines, frame reset, errors and no idle/running/hidden reads");
    return EXIT_SUCCESS;
}
