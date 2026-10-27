#define _POSIX_C_SOURCE 200809L
#include "memory.h"
#include "../support/check.h"
#include <string.h>
#include <unistd.h>

static TbContext *context;
static TbMemory *memory;
static unsigned replies;
static void emit(void *unused, const TbGdbEvent *event)
{
    (void)unused;
    if (!tb_memory_event(memory, event)) { (void)tb_context_event(context, event); }
    if (event->kind == TB_GDB_REPLY) { ++replies; }
}
static void pump(TbGdb *gdb)
{
    tb_gdb_pump(gdb, 1); tb_context_pump(context); tb_memory_pump(memory);
}
static void command(TbGdb *gdb, const char *op, const char *arg)
{
    unsigned before = replies; const char *args[] = {arg};
    CHECK(tb_gdb_request(gdb, op, args, arg ? 1 : 0, TB_GDB_SESSION, 999, 1000, NULL));
    uint64_t end = tb_gdb_now_ms() + 1500;
    while (replies == before && tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(replies > before);
}
static void context_ready(TbGdb *gdb)
{
    uint64_t end = tb_gdb_now_ms() + 1500;
    while ((!tb_context_snapshot(context)->ready || tb_context_snapshot(context)->stale || tb_gdb_pending(gdb)) && tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(tb_context_snapshot(context)->ready && !tb_context_snapshot(context)->stale);
}
static void ready(TbGdb *gdb, int id, bool success)
{
    uint64_t end = tb_gdb_now_ms() + 1500;
    while ((tb_memory_view(memory, id)->pending || tb_gdb_pending(gdb)) && tb_gdb_now_ms() < end) { pump(gdb); }
    const TbMemoryView *v = tb_memory_view(memory, id);
    if (v->pending || v->ready != success) { fprintf(stderr, "memory view %d expr %s: pending %d ready %d wanted %d state %d context %d/%d: %s\n", id, v->expression, v->pending, v->ready, success, tb_gdb_state(gdb), tb_context_snapshot(context)->ready, tb_context_snapshot(context)->stale, v->error); CHECK(false); }
}
static void idle(TbGdb *gdb)
{
    unsigned before = replies; for (int i = 0; i < 8; ++i) { pump(gdb); } CHECK(before == replies);
}
static void consent(TbGdb *gdb)
{
    CHECK(!tb_memory_range_set(memory, 0, 16, 1, 2, TB_MEMORY_ORDINARY, 0, ""));
    CHECK(!tb_memory_range_set(memory, 0, 0, 2, 1, TB_MEMORY_ORDINARY, 0, ""));
    CHECK(!tb_memory_range_set(memory, 0, 0, 1, 2, TB_MEMORY_MMIO, 24, ""));
    CHECK(tb_memory_range_set(memory, 0, 0, 0x40000000, 0x40000004, TB_MEMORY_MMIO, 32, "Unknown effects"));
    CHECK(!tb_memory_range_authorized(memory, 0, 0));
    CHECK(tb_memory_range_authorize(memory, 0, 0) && tb_memory_range_authorized(memory, 0, 0));
    CHECK(!tb_memory_range_authorized(memory, 1, 0));
    CHECK(tb_memory_range_set(memory, 0, 1, 0x40000000, 0x40000008, TB_MEMORY_ORDINARY, 0, "Reviewed RAM"));
    CHECK(!tb_memory_range_authorize(memory, 0, 1)); /* A conflicting grant cannot broaden access. */
    CHECK(tb_memory_range_set(memory, 1, 0, 0x40000000, 0x40000004, TB_MEMORY_MMIO, 16, ""));
    CHECK(tb_memory_range_authorize(memory, 1, 0)); /* Independent per view. */
    CHECK(tb_memory_range_set(memory, 0, 0, 0x40000000, 0x40000008, TB_MEMORY_MMIO, 32, "Read clears status"));
    CHECK(!tb_memory_range_authorized(memory, 0, 0)); /* Editing always removes consent. */
    CHECK(tb_memory_range_authorize(memory, 0, 0)); CHECK(tb_memory_range_revoke(memory, 0, 0));
    CHECK(!tb_memory_range_authorized(memory, 0, 0)); idle(gdb);
    for (unsigned i = 0; i < TB_MEMORY_RANGES; ++i) {
        CHECK(tb_memory_range_set(memory, 0, i, 0x30000000 + i * 256, 0x30000080 + i * 256, TB_MEMORY_ORDINARY, 0, "Owner-reviewed ordinary range"));
    }
    CHECK(tb_memory_range_authorize(memory, 0, 0)); idle(gdb);
    /* An authorized MMIO range still cannot fall back to a byte read while
     * the real explicit-width transport gate remains unverified. */
    CHECK(tb_memory_address(memory, 1, "0x40000000")); ready(gdb, 1, false);
    CHECK(strstr(tb_memory_view(memory, 1)->error, "generic byte reads are blocked")); idle(gdb);
    tb_memory_clear(memory, 1); tb_memory_visible(memory, 1, true);
}
static void backend_consent(TbGdb *gdb)
{
    TbMemoryBackend reviewed = {.target = "stm32f4x.cpu", .device = "STM32F446 / owner board",
        .server = "OpenOCD 0.12.0-01004-g9ea7f3d64; board/st_nucleo_f4.cfg; localhost:3333",
        .firmware = "ST-LINK V2J48M35", .configuration = 1,
        .widths = TB_MEMORY_WIDTH8 | TB_MEMORY_WIDTH16 | TB_MEMORY_WIDTH32};
    CHECK(!tb_memory_backend_confirmed(memory) && !tb_memory_backend(memory));
    CHECK(!tb_memory_backend_confirm(memory, &reviewed));
    CHECK(tb_memory_backend_set(memory, &reviewed));
    CHECK(!tb_memory_backend_confirmed(memory));
    CHECK(tb_memory_backend_confirm(memory, &reviewed)); idle(gdb);
    CHECK(tb_memory_backend_confirmed(memory));
    CHECK(tb_memory_backend_set(memory, tb_memory_backend(memory)) && tb_memory_backend_confirmed(memory));
    for (unsigned field = 0; field < 6; ++field) {
        CHECK(tb_memory_range_set(memory, 1, 0, 0x40020014, 0x40020018, TB_MEMORY_MMIO, 32, "Reviewed risks"));
        CHECK(tb_memory_range_authorize(memory, 1, 0));
        TbMemoryBackend changed = reviewed;
        switch (field) {
        case 0: (void)snprintf(changed.target, sizeof changed.target, "other.cpu"); break;
        case 1: (void)snprintf(changed.device, sizeof changed.device, "other board"); break;
        case 2: (void)snprintf(changed.server, sizeof changed.server, "other server/configuration"); break;
        case 3: (void)snprintf(changed.firmware, sizeof changed.firmware, "ST-LINK V2J25M14"); break;
        case 4: changed.configuration = 2; break;
        default: changed.widths = TB_MEMORY_WIDTH32; break;
        }
        CHECK(tb_memory_backend_set(memory, &changed));
        CHECK(!tb_memory_backend_confirmed(memory) && !tb_memory_range_authorized(memory, 1, 0));
        CHECK(!tb_memory_backend_confirm(memory, &reviewed)); /* A stale review cannot confirm a changed setup. */
        CHECK(tb_memory_backend_confirm(memory, &changed)); idle(gdb);
        CHECK(tb_memory_backend_set(memory, &reviewed) && !tb_memory_backend_confirmed(memory));
        CHECK(tb_memory_backend_confirm(memory, &reviewed));
    }
    TbMemoryBackend invalid = reviewed; memset(invalid.firmware, 'x', sizeof invalid.firmware);
    CHECK(!tb_memory_backend_set(memory, &invalid));
    CHECK(!tb_memory_backend(memory) && !tb_memory_backend_confirmed(memory));
    CHECK(tb_memory_backend_set(memory, &reviewed) && tb_memory_backend_confirm(memory, &reviewed));
    CHECK(tb_memory_backend_set(memory, NULL) && !tb_memory_backend_confirmed(memory));
    CHECK(!tb_memory_backend_confirm(memory, &reviewed));
    CHECK(tb_memory_backend_set(memory, &reviewed) && tb_memory_backend_confirm(memory, &reviewed));
    tb_memory_clear(memory, 1); tb_memory_visible(memory, 1, true); idle(gdb);
}
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    const char *good[] = {"0x1234", "4096", "&buffer", " $sp ", "ptr->member", "&obj.field[3]", "&space::buffer", "buffer + 16", "0xffffffffffffffff"};
    const char *bad[] = {"", " ", "fn()", "a++", "--ptr", "a=1", "(char*)a", "*ptr", "a[fn()]", "a[2", "a;quit", "a\nquit", "0x", "18446744073709551616", "a + -1"};
    for (size_t i = 0; i < sizeof good / sizeof *good; ++i) { CHECK(tb_memory_expression(good[i])); }
    for (size_t i = 0; i < sizeof bad / sizeof *bad; ++i) { CHECK(!tb_memory_expression(bad[i])); }
    CHECK(setenv("TB_FAKE_MODE", "context-memory", 1) == 0);
    TbGdb *gdb = tb_gdb_create(emit, NULL); CHECK(gdb);
    context = tb_context_create(gdb); memory = tb_memory_create(gdb, context); CHECK(context && memory);
    CHECK(tb_gdb_start(gdb, argv[1], 1000)); uint64_t end = tb_gdb_now_ms() + 1500;
    while (tb_gdb_state(gdb) != TB_GDB_READY && tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(tb_gdb_state(gdb) == TB_GDB_READY);
    tb_memory_visible(memory, 0, true); tb_memory_visible(memory, 1, true);
    command(gdb, "-stop", NULL); context_ready(gdb); idle(gdb);
    consent(gdb);
    backend_consent(gdb);
    CHECK(tb_memory_get_order(memory) == TB_MEMORY_UNKNOWN);
    CHECK(!tb_memory_group(memory, 0, 2));
    tb_memory_order(memory, TB_MEMORY_LITTLE);
    CHECK(tb_memory_length(memory, 0, 64)); CHECK(tb_memory_address(memory, 0, "&buffer")); ready(gdb, 0, true);
    const TbMemoryView *a = tb_memory_view(memory, 0), *b = tb_memory_view(memory, 1);
    CHECK(a->address == 0x1000 && a->data_size == 64 && a->readable == 64 && !tb_memory_stale(memory, 0));
    CHECK(!tb_memory_browse(memory, 0, 1)); idle(gdb); CHECK(a->address == 0x1000);
    const TbMemoryRegion ram[] = {{0x1000, 0x1100}, {0x20000000, 0x20001000}};
    tb_memory_regions(memory, ram, 2);
    CHECK(tb_memory_browse(memory, 0, 1)); ready(gdb, 0, true); CHECK(a->address == 0x1040);
    CHECK(tb_memory_browse(memory, 0, -1)); ready(gdb, 0, true); CHECK(a->address == 0x1000);
    CHECK(!tb_memory_browse(memory, 0, -1)); idle(gdb); CHECK(a->address == 0x1000);
    const TbMemoryRegion adjacent[] = {{0x1000, 0x1050}, {0x1050, 0x1060}};
    tb_memory_regions(memory, adjacent, 2);
    /* Use an uncached forward address; adjacent proofs compose and the read
     * ends at 0x1060 instead of crossing into an unapproved range. */
    CHECK(tb_memory_address(memory, 0, "0x1010")); ready(gdb, 0, true);
    CHECK(tb_memory_browse(memory, 0, 1)); ready(gdb, 0, true);
    CHECK(a->address == 0x1050 && a->data_size == 16 && a->readable == 16 && strstr(a->error, "clipped"));
    CHECK(!tb_memory_browse(memory, 0, 1)); idle(gdb); CHECK(a->address == 0x1050);
    tb_memory_regions(memory, ram, 2);
    CHECK(tb_memory_address(memory, 0, "0x1000")); ready(gdb, 0, true);
    char line[160]; tb_memory_line(memory, 0, 2, line, sizeof line); CHECK(strstr(line, "20 21 22 23")); CHECK(strstr(line, "| !\"#$%&"));
    CHECK(tb_memory_group(memory, 0, 4)); tb_memory_line(memory, 0, 0, line, sizeof line); CHECK(strstr(line, "03020100")); idle(gdb);
    tb_memory_order(memory, TB_MEMORY_BIG); tb_memory_line(memory, 0, 0, line, sizeof line); CHECK(strstr(line, "00010203"));
    CHECK(tb_memory_group(memory, 0, 8)); tb_memory_line(memory, 0, 0, line, sizeof line); CHECK(strstr(line, "0001020304050607"));
    CHECK(tb_memory_address(memory, 1, "0x20000000")); ready(gdb, 1, true);
    CHECK(tb_memory_browse(memory, 1, 1)); ready(gdb, 1, true); CHECK(b->address == 0x20000100);
    CHECK(tb_memory_address(memory, 1, "0x40000000")); ready(gdb, 1, true);
    CHECK(!tb_memory_browse(memory, 1, 1)); idle(gdb); /* absent/MMIO metadata never authorizes adjacent reads */
    CHECK(tb_memory_length(memory, 1, 17)); CHECK(tb_memory_address(memory, 1, "0x2020")); ready(gdb, 1, true);
    CHECK(b->address == 0x2020 && b->length == 17 && b->grouping == 1 && a->address == 0x1000 && a->grouping == 8);
    tb_memory_line(memory, 1, 1, line, sizeof line); CHECK(strstr(line, "30 ")); CHECK(strstr(line, "|0               |"));
    CHECK(tb_memory_page(memory, 0, 1)); ready(gdb, 0, true); CHECK(a->address == 0x1040 && b->address == 0x2020);
    CHECK(tb_memory_page(memory, 0, -1)); ready(gdb, 0, true); CHECK(a->address == 0x1000);
    CHECK(tb_memory_length(memory, 0, 1024)); idle(gdb); CHECK(tb_memory_stale(memory, 0));
    CHECK(tb_memory_refresh(memory, 0)); ready(gdb, 0, true); CHECK(a->data_size == 1024 && a->readable == 1024);
    CHECK(!tb_memory_length(memory, 0, 1025)); CHECK(!tb_memory_length(memory, 0, 0));
    CHECK(tb_memory_length(memory, 0, 64));
    command(gdb, "-memory-mode", "1"); CHECK(tb_memory_refresh(memory, 0)); ready(gdb, 0, true);
    CHECK(a->readable == 32 && !a->valid[32] && strstr(a->error, "Partial")); tb_memory_line(memory, 0, 2, line, sizeof line); CHECK(strstr(line, "????????"));
    for (unsigned mode = 2; mode <= 7; ++mode) {
        char arg[8]; (void)snprintf(arg, sizeof arg, "%u", mode); command(gdb, "-memory-mode", arg);
        CHECK(tb_memory_refresh(memory, 0)); ready(gdb, 0, mode == 6);
        CHECK(*a->error); if (mode == 6) { CHECK(a->readable == 0 && a->data_size == 64); }
        idle(gdb);
    }
    command(gdb, "-memory-mode", "0"); CHECK(tb_memory_refresh(memory, 0)); ready(gdb, 0, true);
    CHECK(tb_memory_address(memory, 0, "0xffffffffffffffff")); ready(gdb, 0, false); CHECK(a->address == 0x1000); CHECK(strstr(a->error, "overflow"));
    CHECK(tb_memory_address(memory, 0, "0")); ready(gdb, 0, true); CHECK(tb_memory_page(memory, 0, -1)); ready(gdb, 0, false); CHECK(strstr(a->error, "overflow"));
    CHECK(!tb_memory_address(memory, 0, "fn()"));
    CHECK(tb_memory_address(memory, 0, "0x1000")); tb_memory_pump(memory);
    tb_memory_clear(memory, 0); ready(gdb, 0, false); CHECK(!a->configured && !a->data_size);
    tb_memory_visible(memory, 0, true);
    CHECK(tb_memory_address(memory, 0, "0x1000")); tb_memory_pump(memory);
    CHECK(tb_memory_address(memory, 0, "0x2000")); ready(gdb, 0, true); CHECK(a->address == 0x2000);
    CHECK(tb_memory_page(memory, 0, 1)); ready(gdb, 0, true); CHECK(a->address == 0x2100);
    tb_memory_visible(memory, 0, false); CHECK(tb_memory_refresh(memory, 0)); idle(gdb); CHECK(a->pending);
    CHECK(tb_memory_range_set(memory, 0, 0, 0x2000, 0x2200, TB_MEMORY_ORDINARY, 0, ""));
    CHECK(tb_memory_range_authorize(memory, 0, 0));
    command(gdb, "-run", NULL); CHECK(tb_memory_stale(memory, 0)); CHECK(!tb_memory_refresh(memory, 1));
    CHECK(!tb_memory_range_authorized(memory, 0, 0));
    CHECK(tb_memory_browse(memory, 0, -1) && a->address == 0x2000 && tb_memory_stale(memory, 0));
    CHECK(tb_memory_browse(memory, 0, 1) && a->address == 0x2100);
    CHECK(!tb_memory_browse(memory, 0, 1) && a->address == 0x2100); idle(gdb);
    command(gdb, "-stop", NULL); context_ready(gdb); CHECK(!a->pending);
    tb_memory_visible(memory, 0, true); idle(gdb); CHECK(tb_memory_stale(memory, 0));
    CHECK(tb_memory_refresh(memory, 0)); ready(gdb, 0, true);
    CHECK(!tb_memory_range_authorized(memory, 0, 0) && tb_memory_range_authorize(memory, 0, 0));
    CHECK(tb_context_select_thread(context, "42")); context_ready(gdb); CHECK(tb_memory_stale(memory, 0)); idle(gdb);
    CHECK(!tb_memory_range_authorized(memory, 0, 0));
    command(gdb, "-memory-mode", "8"); CHECK(tb_memory_refresh(memory, 0)); ready(gdb, 0, false); context_ready(gdb); CHECK(tb_memory_stale(memory, 0)); idle(gdb);
    CHECK(tb_memory_backend_confirmed(memory));
    tb_memory_target(memory, true, 32);
    CHECK(!tb_memory_backend(memory) && !tb_memory_backend_confirmed(memory)); idle(gdb);
    tb_gdb_close(gdb); end = tb_gdb_now_ms() + 1500;
    while (tb_gdb_state(gdb) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(tb_gdb_state(gdb) == TB_GDB_DISCONNECTED);
    CHECK(!tb_memory_backend_confirmed(memory) && !tb_memory_backend(memory));
    tb_memory_reset(memory); CHECK(!tb_memory_view(memory, 0)->configured && !tb_memory_view(memory, 1)->data_size);
    CHECK(tb_memory_get_order(memory) == TB_MEMORY_UNKNOWN && !tb_memory_browse(memory, 0, 1));
    tb_memory_destroy(memory); tb_context_destroy(context); CHECK(tb_gdb_destroy(gdb)); CHECK(unsetenv("TB_FAKE_MODE") == 0);
    puts("PASS M10 independent bounded Memory, safe scalar expressions, target byte order, ordinary/unknown/MMIO bounds, hex/ASCII, partial/errors, obsolete replies and session reset without unsolicited reads");
    return EXIT_SUCCESS;
}
