#define _POSIX_C_SOURCE 200809L
#include "mmio.h"
#include "../support/check.h"
#include <string.h>

typedef struct { TbGdb *gdb; TbMmio *mmio; unsigned replies, reads; } Fixture;
static void emit(void *context, const TbGdbEvent *event)
{
    Fixture *f = context;
    (void)tb_mmio_event(f->mmio, event);
    if (event->kind == TB_GDB_REPLY) {
        ++f->replies;
        const TbMiValue *reads = tb_mi_find(event->record->results, "reads");
        if (reads) { CHECK(reads->type == TB_MI_STRING); f->reads = (unsigned)strtoul(reads->string.data, NULL, 10); }
    }
}
static void pump(Fixture *f) { tb_gdb_pump(f->gdb, 5); tb_mmio_pump(f->mmio); }
static void command(Fixture *f, const char *operation, const char *arg)
{
    const char *args[] = {arg}; unsigned before = f->replies;
    CHECK(tb_gdb_request(f->gdb, operation, args, arg ? 1 : 0, TB_GDB_SESSION, 777, 1000, NULL));
    uint64_t until = tb_gdb_now_ms() + 2000;
    while ((f->replies == before || tb_gdb_pending(f->gdb)) && tb_gdb_now_ms() < until) { pump(f); }
    CHECK(f->replies > before && !tb_gdb_pending(f->gdb));
}
static void start(Fixture *f, const char *peer)
{
    CHECK(tb_gdb_start(f->gdb, peer, 1000)); uint64_t until = tb_gdb_now_ms() + 2000;
    while (tb_gdb_state(f->gdb) != TB_GDB_READY && tb_gdb_now_ms() < until) { pump(f); }
    CHECK(tb_gdb_state(f->gdb) == TB_GDB_READY); command(f, "-stop", NULL);
    until = tb_gdb_now_ms() + 1000;
    while (tb_gdb_state(f->gdb) != TB_GDB_STOPPED && tb_gdb_now_ms() < until) { pump(f); }
    CHECK(tb_gdb_state(f->gdb) == TB_GDB_STOPPED);
}
static void close_peer(Fixture *f)
{
    tb_gdb_close(f->gdb); uint64_t until = tb_gdb_now_ms() + 3000;
    while (tb_gdb_state(f->gdb) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < until) { pump(f); }
    CHECK(tb_gdb_state(f->gdb) == TB_GDB_DISCONNECTED); tb_mmio_pump(f->mmio);
}
static void complete(Fixture *f)
{
    uint64_t until = tb_gdb_now_ms() + 4500;
    while (tb_mmio_busy(f->mmio) && tb_gdb_now_ms() < until) { pump(f); }
    CHECK(!tb_mmio_busy(f->mmio));
}
int main(int argc, char **argv)
{
    CHECK(argc == 2 && setenv("TB_FAKE_MODE", "mmio", 1) == 0);
    Fixture f = {0}; f.gdb = tb_gdb_create(emit, &f); CHECK(f.gdb);
    f.mmio = tb_mmio_create(f.gdb); CHECK(f.mmio); start(&f, argv[1]);
    CHECK(!tb_mmio_read(f.mmio, "target;reset", 0x40020014, 32));
    CHECK(!tb_mmio_read(f.mmio, "stm32f4x.cpu", 0x40020015, 32));
    CHECK(!tb_mmio_read(f.mmio, "stm32f4x.cpu", 0x40020010, 64));
    for (unsigned width = 8; width <= 32; width *= 2) {
        CHECK(tb_mmio_read(f.mmio, "stm32f4x.cpu", 0x40020014, width));
        CHECK(!tb_mmio_destroy(f.mmio));
        CHECK(!tb_gdb_request(f.gdb, "-echo", NULL, 0, TB_GDB_SESSION, 777, 1000, NULL));
        CHECK(!tb_mmio_read(f.mmio, "stm32f4x.cpu", 0x40020014, width)); complete(&f);
        const TbMmioResult *r = tb_mmio_result(f.mmio);
        CHECK(r->status == TB_MMIO_COMPLETE && r->width == width && r->address == 0x40020014);
        CHECK(r->value == (width == 8 ? 0x5a : width == 16 ? 0x125a : 0x1234125a));
        CHECK(r->generation.stop == tb_gdb_generation(f.gdb).stop);
    }
    command(&f, "-mmio-stats", NULL); CHECK(f.reads == 3);
    command(&f, "-mmio-mode", "1");
    CHECK(tb_mmio_read(f.mmio, "stm32f4x.cpu", 0x40020014, 32)); complete(&f);
    CHECK(tb_mmio_result(f.mmio)->status == TB_MMIO_UNCERTAIN);
    CHECK(!tb_mmio_read(f.mmio, "stm32f4x.cpu", 0x40020014, 32));
    uint64_t until = tb_gdb_now_ms() + 2000;
    while (tb_mmio_result(f.mmio)->status == TB_MMIO_UNCERTAIN && tb_gdb_now_ms() < until) { pump(&f); }
    CHECK(tb_mmio_result(f.mmio)->status == TB_MMIO_FAILED);
    command(&f, "-mmio-stats", NULL); CHECK(f.reads == 4);
    command(&f, "-mmio-mode", "0");
    CHECK(tb_mmio_read(f.mmio, "stm32f4x.cpu", 0x40020014, 32)); complete(&f);
    CHECK(tb_mmio_result(f.mmio)->status == TB_MMIO_COMPLETE);
    close_peer(&f);
    for (unsigned mode = 2; mode <= 11; ++mode) {
        start(&f, argv[1]); char text[16]; (void)snprintf(text, sizeof text, "%u", mode);
        command(&f, "-mmio-mode", text);
        CHECK(tb_mmio_read(f.mmio, "stm32f4x.cpu", 0x40020014, 32)); complete(&f);
        TbMmioStatus expected = mode == 9 || mode == 10 ? TB_MMIO_FAILED : TB_MMIO_UNCERTAIN;
        CHECK(tb_mmio_result(f.mmio)->status == expected);
        if (expected == TB_MMIO_UNCERTAIN) { CHECK(!tb_mmio_read(f.mmio, "stm32f4x.cpu", 0x40020014, 32)); }
        command(&f, "-mmio-stats", NULL); CHECK(f.reads == 1); /* No automatic retry or fallback. */
        close_peer(&f);
    }
    CHECK(tb_mmio_destroy(f.mmio) && tb_gdb_destroy(f.gdb)); CHECK(unsetenv("TB_FAKE_MODE") == 0);
    puts("PASS authored MMIO transport: serialized explicit widths, unique frames, malformed/error/stale rejection, timeout/late reconciliation, no retries/fallback and reconnect recovery; no hardware exercised");
    return 0;
}
