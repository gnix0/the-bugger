#define _POSIX_C_SOURCE 200809L
#include "memory.h"
#include "../support/check.h"
#include <string.h>

typedef struct { TbGdb *gdb; TbContext *context; TbMemory *memory; unsigned replies, reads; } Fixture;
static void emit(void *context, const TbGdbEvent *event)
{
    Fixture *f = context;
    if (!tb_memory_event(f->memory, event)) { (void)tb_context_event(f->context, event); }
    if (event->kind == TB_GDB_REPLY) {
        ++f->replies; const TbMiValue *reads = tb_mi_find(event->record->results, "reads");
        if (reads) { CHECK(reads->type == TB_MI_STRING); f->reads = (unsigned)strtoul(reads->string.data, NULL, 10); }
    }
}
static void pump(Fixture *f)
{
    tb_gdb_pump(f->gdb, 2); tb_memory_transport(f->memory);
    if (!tb_memory_register_busy(f->memory)) { tb_context_pump(f->context); }
    tb_memory_pump(f->memory);
}
static void idle(Fixture *f) { for (unsigned i = 0; i < 10; ++i) { pump(f); } }
static void command(Fixture *f, const char *operation)
{
    unsigned before = f->replies;
    CHECK(tb_gdb_request(f->gdb, operation, NULL, 0, TB_GDB_SESSION, 777, 1000, NULL));
    uint64_t until = tb_gdb_now_ms() + 3000;
    while (f->replies == before && tb_gdb_now_ms() < until) { pump(f); }
    CHECK(f->replies > before);
}
static void ready(Fixture *f)
{
    uint64_t until = tb_gdb_now_ms() + 3000;
    const TbContextSnapshot *s = tb_context_snapshot(f->context);
    while ((!s->ready || s->stale || tb_gdb_pending(f->gdb)) && tb_gdb_now_ms() < until) { pump(f); }
    CHECK(s->ready && !s->stale && !tb_gdb_pending(f->gdb));
}
static void complete(Fixture *f)
{
    uint64_t until = tb_gdb_now_ms() + 4500;
    while (tb_memory_register_busy(f->memory) && tb_gdb_now_ms() < until) { pump(f); }
    CHECK(!tb_memory_register_busy(f->memory));
}
int main(int argc, char **argv)
{
    CHECK(argc == 2 && setenv("TB_FAKE_MODE", "context-mmio", 1) == 0);
    Fixture f = {0}; f.gdb = tb_gdb_create(emit, &f); CHECK(f.gdb);
    f.context = tb_context_create(f.gdb); f.memory = tb_memory_create(f.gdb, f.context); CHECK(f.context && f.memory);
    CHECK(tb_gdb_start(f.gdb, argv[1], 1000)); uint64_t until = tb_gdb_now_ms() + 2000;
    while (tb_gdb_state(f.gdb) != TB_GDB_READY && tb_gdb_now_ms() < until) { pump(&f); }
    CHECK(tb_gdb_state(f.gdb) == TB_GDB_READY); tb_memory_visible(f.memory, 0, true);
    command(&f, "-stop"); ready(&f); tb_memory_target(f.memory, true, 32);
    TbMemoryBackend setup = {.target = "stm32f4x.cpu", .device = "Authored device identity",
        .server = "Authored OpenOCD fixture configuration", .firmware = "Authored ST-LINK firmware assertion",
        .configuration = 1, .widths = TB_MEMORY_WIDTH32};
    CHECK(tb_memory_backend_set(f.memory, &setup) && tb_memory_backend_confirm(f.memory, &setup));
    CHECK(tb_memory_range_set(f.memory, 0, 0, 0x40020010, 0x40020020, TB_MEMORY_MMIO, 32, "Unknown side effects"));
    TbMemoryRegister sample;
    CHECK(!tb_memory_register_sample(f.memory, 0, 0x40020014, 32, &sample));
    CHECK(!tb_memory_read_register(f.memory, 0, 0x40020014, 32));
    CHECK(tb_memory_range_authorize(f.memory, 0, 0)); idle(&f); command(&f, "-mmio-stats"); CHECK(f.reads == 0);
    CHECK(tb_memory_units(f.memory, 0, 0x40020010, 32));
    CHECK(tb_memory_rows(f.memory, 0) == 32 && !tb_memory_view(f.memory, 1)->mmio);
    uint64_t address;
    CHECK(tb_memory_unit_address(f.memory, 0, 1, &address) && address == 0x40020014);
    CHECK(!tb_memory_units(f.memory, 0, 0x40020011, 32));
    CHECK(!tb_memory_address(f.memory, 0, "&GPIOA_ODR")); /* No expression evaluation in MMIO navigation. */
    CHECK(!tb_memory_refresh(f.memory, 0)); /* Never refresh a whole MMIO window. */
    CHECK(tb_memory_browse(f.memory, 0, 1) && tb_memory_view(f.memory, 0)->unit_address == 0x40020090);
    CHECK(tb_memory_page(f.memory, 0, -1) && tb_memory_view(f.memory, 0)->unit_address == 0x40020010);
    CHECK(tb_memory_units(f.memory, 0, 0xfffffffc, 32) && tb_memory_rows(f.memory, 0) == 1);
    CHECK(!tb_memory_browse(f.memory, 0, 1));
    CHECK(tb_memory_address(f.memory, 0, "0x40020010"));
    idle(&f); command(&f, "-mmio-stats"); CHECK(f.reads == 0);
    CHECK(!tb_memory_read_register(f.memory, 1, 0x40020014, 32));
    CHECK(!tb_memory_read_register(f.memory, 0, 0x40020015, 32));
    CHECK(!tb_memory_read_register(f.memory, 0, 0x40020020, 32));
    CHECK(!tb_memory_read_register(f.memory, 0, 0x40020014, 16));
    CHECK(tb_memory_read_register(f.memory, 0, 0x40020014, 32));
    CHECK(tb_memory_range_revoke(f.memory, 0, 0)); complete(&f);
    command(&f, "-mmio-stats"); CHECK(f.reads == 0); /* Permission is rechecked before dispatch. */
    CHECK(tb_memory_range_authorize(f.memory, 0, 0));
    CHECK(tb_memory_read_register(f.memory, 0, 0x40020014, 32)); complete(&f);
    CHECK(tb_memory_register_sample(f.memory, 0, 0x40020014, 32, &sample));
    CHECK(sample.value == 0x1234125a && sample.address == 0x40020014 && sample.width == 32 && sample.sampled_ms);
    CHECK(!tb_memory_register_sample(f.memory, 1, 0x40020014, 32, &sample));
    CHECK(!tb_memory_register_sample(f.memory, 0, 0x40020014, 16, &sample));
    command(&f, "-mmio-stats"); CHECK(f.reads == 1);
    command(&f, "-run"); CHECK(!tb_memory_range_authorized(f.memory, 0, 0));
    CHECK(tb_memory_backend_confirmed(f.memory)); /* Ordinary execution does not reconfigure setup. */
    CHECK(tb_memory_browse(f.memory, 0, 1) && tb_memory_browse(f.memory, 0, -1));
    char row[256]; tb_memory_line(f.memory, 0, 1, row, sizeof row);
    CHECK(strstr(row, "sample") && !strstr(row, "register"));
    CHECK(tb_memory_register_sample(f.memory, 0, 0x40020014, 32, &sample));
    CHECK(!tb_memory_read_register(f.memory, 0, 0x40020014, 32)); idle(&f);
    command(&f, "-stop"); ready(&f); CHECK(!tb_memory_read_register(f.memory, 0, 0x40020014, 32));
    command(&f, "-mmio-stats"); CHECK(f.reads == 1); /* No reads from resume, stop, or cached inspection. */
    CHECK(tb_memory_range_authorize(f.memory, 0, 0));
    CHECK(tb_memory_read_register(f.memory, 0, 0x40020014, 32)); complete(&f);
    command(&f, "-mmio-stats"); CHECK(f.reads == 2);
    ++setup.configuration; CHECK(tb_memory_backend_set(f.memory, &setup));
    CHECK(!tb_memory_backend_confirmed(f.memory) && !tb_memory_range_authorized(f.memory, 0, 0));
    CHECK(!tb_memory_register_sample(f.memory, 0, 0x40020014, 32, &sample));
    CHECK(!tb_memory_read_register(f.memory, 0, 0x40020014, 32)); idle(&f);
    CHECK(tb_memory_range_authorize(f.memory, 0, 0));
    CHECK(!tb_memory_read_register(f.memory, 0, 0x40020014, 32)); /* Range alone cannot bypass missing setup. */
    command(&f, "-mmio-stats"); CHECK(f.reads == 2);
    CHECK(tb_memory_backend_confirm(f.memory, &setup));
    CHECK(tb_memory_range_set(f.memory, 1, 0, 0x40020010, 0x40020020, TB_MEMORY_MMIO, 32, "Authored peer only"));
    CHECK(tb_memory_range_authorize(f.memory, 1, 0));
    CHECK(tb_memory_read_register(f.memory, 1, 0x40020014, 32));
    tb_memory_pump(f.memory); /* Accepted request, not merely model intent. */
    tb_memory_clear(f.memory, 1); complete(&f);
    CHECK(!tb_memory_register_sample(f.memory, 1, 0x40020014, 32, &sample));
    command(&f, "-mmio-stats"); CHECK(f.reads == 3);
    CHECK(tb_memory_range_authorize(f.memory, 0, 0));
    CHECK(tb_memory_read_register(f.memory, 0, 0x40020014, 32));
    tb_memory_pump(f.memory);
    ++setup.configuration; CHECK(tb_memory_backend_set(f.memory, &setup)); complete(&f);
    CHECK(!tb_memory_register_sample(f.memory, 0, 0x40020014, 32, &sample));
    CHECK(!tb_memory_backend_confirmed(f.memory));
    command(&f, "-mmio-stats"); CHECK(f.reads == 4);
    tb_gdb_close(f.gdb); until = tb_gdb_now_ms() + 3000;
    while (tb_gdb_state(f.gdb) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < until) { pump(&f); }
    CHECK(tb_gdb_state(f.gdb) == TB_GDB_DISCONNECTED);
    tb_memory_destroy(f.memory); tb_context_destroy(f.context); CHECK(tb_gdb_destroy(f.gdb));
    CHECK(unsetenv("TB_FAKE_MODE") == 0);
    puts("PASS authored Memory MMIO consent isolation, dispatch-time revocation, exact-width samples, explicit refresh, cached inspection and generation/setup invalidation without unsolicited reads");
    return 0;
}
