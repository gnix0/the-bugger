#include "native.h"
#include "../support/check.h"
#include <string.h>

static void stopped(TbNative *n, uint64_t after)
{
    uint64_t end = tb_gdb_now_ms() + 5000;
    const TbContextSnapshot *s = tb_context_snapshot(tb_native_context(n));
    while (tb_gdb_now_ms() < end) {
        tb_native_pump(n, 5);
        if (tb_native_state(n) == TB_NATIVE_STOPPED && s->ready && !s->stale && s->generation.stop > after) { return; }
    }
    fprintf(stderr, "context: %s\n", tb_native_message(n)); CHECK(false);
}
static void read_done(TbNative *n, int id, bool success)
{
    const TbMemoryView *v = tb_memory_view(tb_native_memory(n), id); uint64_t end = tb_gdb_now_ms() + 5000;
    while (v->pending && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    if (v->pending || v->ready != success) {
        fprintf(stderr, "memory: %s\n", v->error);
        for (size_t i = 0; i < tb_native_log_count(n); ++i) { fprintf(stderr, "%s\n", tb_native_log_line(n, i)); }
        CHECK(false);
    }
}
int main(int argc, char **argv)
{
    CHECK(argc == 2); TbNative *n = tb_native_create(); CHECK(n);
    TbNativeConfig config = {.gdb = "gdb", .executable = argv[1]}; CHECK(tb_native_start(n, &config));
    uint64_t end = tb_gdb_now_ms() + 5000;
    while (tb_native_state(n) != TB_NATIVE_READY && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    CHECK(tb_native_state(n) == TB_NATIVE_READY);
    TbMemory *m = tb_native_memory(n); tb_memory_visible(m, 0, true); tb_memory_visible(m, 1, true);
    CHECK(tb_native_console(n, "start")); stopped(n, 0);
    const TbContextSnapshot *s = tb_context_snapshot(tb_native_context(n)); uint64_t stop = s->generation.stop;
    TbBreakpoints *bp = tb_native_breakpoints(n); end = tb_gdb_now_ms() + 5000;
    while ((!tb_break_ready(bp) || tb_break_busy(bp)) && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    CHECK(tb_native_run_to(n, "memory_checkpoint")); stopped(n, stop);
    CHECK(tb_memory_get_order(m) == TB_MEMORY_LITTLE);
    CHECK(tb_memory_length(m, 0, 64)); CHECK(tb_memory_address(m, 0, "&memory_buffer")); read_done(n, 0, true);
    const TbMemoryView *a = tb_memory_view(m, 0), *b = tb_memory_view(m, 1);
    CHECK(a->readable == 64 && memcmp(a->bytes, "THE BUGGER MEMORY", 17) == 0 && !tb_memory_stale(m, 0));
    CHECK(tb_memory_length(m, 1, 256)); CHECK(tb_memory_address(m, 1, "&memory_other")); read_done(n, 1, true);
    CHECK(b->address != a->address && memcmp(b->bytes, "INDEPENDENT MEMORY", 18) == 0);
    CHECK(tb_memory_group(m, 0, 4)); char line[160]; tb_memory_line(m, 0, 0, line, sizeof line); CHECK(strstr(line, "20454854"));
    uint64_t first = a->address, second = b->address;
    CHECK(tb_memory_page(m, 0, 1)); read_done(n, 0, true); CHECK(a->address == first + 64 && b->address == second);
    CHECK(tb_memory_page(m, 0, -1)); read_done(n, 0, true); CHECK(a->address == first);
    CHECK(tb_memory_browse(m, 0, 1)); read_done(n, 0, true); CHECK(a->address == first + 64);
    CHECK(tb_memory_browse(m, 0, -1)); read_done(n, 0, true); CHECK(a->address == first);
    CHECK(tb_memory_address(m, 1, "memory_edge")); read_done(n, 1, true);
    /* GDB probes partially accessible ranges heuristically. Some native
     * versions return an extra boundary byte, so verify the known prefix
     * and missing tail rather than assuming an exact backend read count.
     * Exact sparse-byte parsing is covered by the deterministic MI test. */
    if (b->readable < 32 || b->readable >= b->data_size || !strstr(b->error, "Partial") || b->valid[b->data_size - 1]) {
        fprintf(stderr, "boundary read: %zu/%zu bytes, last valid=%d, error=%s\n",
                b->readable, b->data_size, b->valid[b->data_size - 1], b->error);
        CHECK(false);
    }
    for (unsigned i = 0; i < 32; ++i) { CHECK(b->valid[i] && b->bytes[i] == i); }
    tb_memory_line(m, 1, 2, line, sizeof line); CHECK(strstr(line, "?? ??"));
    CHECK(tb_memory_address(m, 1, "0x1")); read_done(n, 1, false); CHECK(*b->error && tb_memory_stale(m, 1));
    CHECK(!tb_memory_address(m, 1, "memory_checkpoint()"));
    stop = s->generation.stop; CHECK(tb_native_step(n, TB_STEP_OUT, false)); stopped(n, stop);
    stop = s->generation.stop; CHECK(tb_native_step(n, TB_STEP_OVER, false)); stopped(n, stop);
    CHECK(tb_memory_stale(m, 0) && a->bytes[0] == 'T');
    CHECK(tb_memory_refresh(m, 0)); read_done(n, 0, true); CHECK(a->bytes[0] == 'Z');
    tb_native_close(n); end = tb_gdb_now_ms() + 6000;
    while (tb_native_active(n) && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    CHECK(!tb_native_active(n) && tb_native_destroy(n));
    puts("PASS M10 actual GDB buffers/ASCII, independent Memory ranges, target-endian grouping, paging, partial/unmapped reads, rejected calls and explicit refresh after target changes");
    return EXIT_SUCCESS;
}
