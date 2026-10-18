#include "native.h"
#include "../support/check.h"
#include <string.h>

static void ready(TbNative *n, uint64_t after)
{
    TbRegisters *r = tb_native_registers(n); const TbContextSnapshot *s = tb_context_snapshot(tb_native_context(n));
    uint64_t end = tb_gdb_now_ms() + 5000;
    while (tb_gdb_now_ms() < end) {
        tb_registers_want(r, 0, 32); tb_native_pump(n, 5);
        bool fresh = tb_registers_count(r) > 0;
        for (size_t i = 0; i < tb_registers_count(r) && i < 32; ++i) { fresh = fresh && tb_registers_row(r, i)->fresh; }
        if (fresh && !tb_registers_stale(r) && s->generation.stop > after) { return; }
    }
    fprintf(stderr, "native registers: %s / %s\n", tb_native_message(n), tb_registers_error(r)); CHECK(false);
}
static const TbRegister *pc(TbRegisters *r)
{
    for (size_t i = 0; i < tb_registers_count(r); ++i) {
        const TbRegister *row = tb_registers_row(r, i);
        if (strcmp(row->name, "rip") == 0) { return row; }
    }
    CHECK(false); return NULL;
}
int main(int argc, char **argv)
{
    CHECK(argc == 2); TbNative *n = tb_native_create(); CHECK(n);
    TbNativeConfig config = {.gdb = "gdb", .executable = argv[1]}; CHECK(tb_native_start(n, &config));
    uint64_t end = tb_gdb_now_ms() + 5000;
    while (tb_native_state(n) != TB_NATIVE_READY && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    CHECK(tb_native_state(n) == TB_NATIVE_READY);
    TbRegisters *r = tb_native_registers(n); tb_registers_visible(r, true);
    CHECK(tb_native_console(n, "start")); ready(n, 0);
    const TbContextSnapshot *s = tb_context_snapshot(tb_native_context(n));
    const TbRegister *instruction = pc(r); CHECK(instruction->fresh && instruction->available && !instruction->changed);
    CHECK(strtoull(instruction->value, NULL, 16) == strtoull(s->selected.address, NULL, 16));
    uint64_t stop = s->generation.stop; CHECK(tb_native_step(n, TB_STEP_INTO, true)); ready(n, stop);
    CHECK(instruction->changed && strtoull(instruction->value, NULL, 16) == strtoull(s->selected.address, NULL, 16));
    stop = s->generation.stop;
    tb_registers_format(r, TB_REG_DECIMAL); ready(n, stop - 1);
    CHECK(instruction->changed && strtoull(instruction->value, NULL, 10) == strtoull(s->selected.address, NULL, 16));
    tb_registers_refresh(r); ready(n, stop - 1); CHECK(instruction->changed);
    /* GDB normally ends its backtrace at main. Establish an actual caller
     * instead of assuming libc startup frames are exposed. */
    TbBreakpoints *breakpoints = tb_native_breakpoints(n);
    end = tb_gdb_now_ms() + 5000;
    while ((!tb_break_ready(breakpoints) || tb_break_busy(breakpoints)) && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    CHECK(tb_break_ready(breakpoints) && !tb_break_busy(breakpoints));
    CHECK(tb_native_run_to(n, "code_callee")); ready(n, stop);
    stop = s->generation.stop; CHECK(strstr(s->selected.function, "code_callee"));
    tb_context_visible(tb_native_context(n), false, true);
    end = tb_gdb_now_ms() + 5000;
    while ((s->stale || s->frame_count < 2) && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    CHECK(!s->stale && s->frame_count >= 2);
    CHECK(tb_context_select_frame(tb_native_context(n), 1)); ready(n, stop - 1);
    for (size_t i = 0; i < tb_registers_count(r) && i < 32; ++i) { CHECK(!tb_registers_row(r, i)->changed); }
    tb_native_close(n); end = tb_gdb_now_ms() + 6000;
    while (tb_native_active(n) && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    CHECK(!tb_native_active(n) && tb_native_destroy(n));
    puts("PASS M09 actual native registers-only context, discovered RIP/GDB PC agreement, stepping, numeric formats, stable change markers, refresh and caller baseline reset");
    return EXIT_SUCCESS;
}
