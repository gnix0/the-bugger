#define _POSIX_C_SOURCE 200809L
#include "mmio.h"
#include "../support/check.h"
#include <string.h>

/* Opt-in physical transport validation, NOT part of test-ci execution.
 * Requires the documented STM32F446 / ST-LINK V2J48M35 / OpenOCD setup.
 * Connecting can halt firmware. Exactly three frontend memory operations:
 * GPIOA_ODR 0x40020014, count 1 each, widths 8/16/32. No flash/reset/write,
 * resume, polling or retry command. Backend physical attempts may differ.
 * This tests transport; it does not test or grant interactive range consent. */
typedef struct { TbGdb *gdb; TbMmio *mmio; bool reply, success; } Probe;
static void emit(void *context, const TbGdbEvent *event)
{
    Probe *p = context;
    (void)tb_mmio_event(p->mmio, event);
    if (event->kind == TB_GDB_RECORD && event->record && event->record->kind == TB_MI_TARGET) {
        CHECK(fwrite(event->record->stream.data, 1, event->record->stream.length, stdout) == event->record->stream.length);
    }
    if (event->cookie == 19801 && event->kind == TB_GDB_REPLY) {
        p->reply = true; p->success = event->record &&
            (strcmp(event->record->klass, "connected") == 0 || strcmp(event->record->klass, "done") == 0);
    }
}
static void pump(Probe *p) { tb_gdb_pump(p->gdb, 2); tb_mmio_pump(p->mmio); }
static void request(Probe *p, const char *operation, const char *const args[], size_t count)
{
    p->reply = p->success = false;
    CHECK(tb_gdb_request(p->gdb, operation, args, count, TB_GDB_SESSION, 19801, 3000, NULL));
    uint64_t until = tb_gdb_now_ms() + 4000;
    while (!p->reply && tb_gdb_now_ms() < until) { pump(p); }
    CHECK(p->reply && p->success);
}
int main(int argc, char **argv)
{
    CHECK(argc == 4 && strcmp(argv[3], "--stm32f446-gpioa-odr") == 0);
    CHECK(*argv[2] && strspn(argv[2], "0123456789.:[]") == strlen(argv[2]));
    Probe p = {0}; p.gdb = tb_gdb_create(emit, &p); CHECK(p.gdb);
    p.mmio = tb_mmio_create(p.gdb); CHECK(p.mmio);
    CHECK(tb_gdb_start(p.gdb, argv[1], 3000)); uint64_t until = tb_gdb_now_ms() + 4000;
    while (tb_gdb_state(p.gdb) != TB_GDB_READY && tb_gdb_now_ms() < until) { pump(&p); }
    CHECK(tb_gdb_state(p.gdb) == TB_GDB_READY);
    const char *connect[] = {"extended-remote", argv[2]}; request(&p, "-target-select", connect, 2);
    CHECK(tb_gdb_state(p.gdb) == TB_GDB_STOPPED);
    for (unsigned width = 8; width <= 32; width *= 2) {
        CHECK(tb_mmio_read(p.mmio, "stm32f4x.cpu", 0x40020014, width));
        until = tb_gdb_now_ms() + 4000;
        while (tb_mmio_busy(p.mmio) && tb_gdb_now_ms() < until) { pump(&p); }
        const TbMmioResult *r = tb_mmio_result(p.mmio);
        CHECK(r->status == TB_MMIO_COMPLETE && r->address == 0x40020014 && r->width == width);
        printf("PASS production MMIO transport GPIOA_ODR width %u value 0x%llx; one frontend request, physical attempts not asserted\n",
               width, (unsigned long long)r->value);
    }
    request(&p, "-target-disconnect", NULL, 0);
    tb_gdb_close(p.gdb); until = tb_gdb_now_ms() + 4000;
    while (tb_gdb_state(p.gdb) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < until) { pump(&p); }
    CHECK(tb_gdb_state(p.gdb) == TB_GDB_DISCONNECTED);
    CHECK(tb_mmio_destroy(p.mmio) && tb_gdb_destroy(p.gdb));
    puts("PASS physical production transport only; interactive consent/UI and other targets not claimed");
    return 0;
}
