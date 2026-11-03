#define _POSIX_C_SOURCE 200809L
#include "model/registers.h"
#include "../support/check.h"

#include <string.h>

static TbContext_t   *context;
static TbRegisters_t *registers;
static unsigned       replies;
static unsigned       writes;
static unsigned       reads;
static unsigned       last_number;

static void emit(void *unused, const TbGdbEvent_t *event)
{
    (void)unused;
    if (!tb_registers_event(registers, event))
        (void)tb_context_event(context, event);

    if (event->kind == TB_GDB_REPLY) {
        ++replies;
        if (event->cookie == 99999 && event->record) {
            const TbMiValue_t *w = tb_mi_find(event->record->results, "writes");
            const TbMiValue_t *r = tb_mi_find(event->record->results, "reads");
            const TbMiValue_t *n = tb_mi_find(event->record->results, "number");
            if (w && r && n) {
                writes      = (unsigned)strtoul(w->string.data, NULL, 10);
                reads       = (unsigned)strtoul(r->string.data, NULL, 10);
                last_number = (unsigned)strtoul(n->string.data, NULL, 10);
            }
        }
    }
}


static void pump(TbGdb_t *gdb)
{
    tb_gdb_pump(gdb, 2);
    tb_context_pump(context);
    tb_registers_pump(registers);
}


static void idle(TbGdb_t *gdb)
{
    uint64_t end = tb_gdb_now_ms() + 5000;
    while (tb_gdb_now_ms() < end) {
        pump(gdb);
        const TbContextSnapshot_t *s = tb_context_snapshot(context);
        if (s->ready && !s->stale && !tb_gdb_pending(gdb))
            return;
    }
    fprintf(stderr, "Idle: %s / %s\n", tb_context_snapshot(context)->error, tb_registers_edit(registers)->message);
    CHECK(false);
}


static void command(TbGdb_t *gdb, const char *operation, const char *value)
{
    unsigned before = replies;
    const char *args[] = {value};
    CHECK(tb_gdb_request(gdb, operation, value ? args : NULL, value ? 1 : 0,
                         TB_GDB_SESSION, 99999, 1000, NULL));
    uint64_t end = tb_gdb_now_ms() + 2000;
    while (replies == before && tb_gdb_now_ms() < end)
        pump(gdb);

    CHECK(replies > before);
}


static void mode(TbGdb_t *gdb, unsigned value)
{
    char number[16];
    (void)snprintf(number, sizeof(number), "%u", value);
    command(gdb, "-register-edit-mode", number);
}


static void sample(TbGdb_t *gdb)
{
    tb_registers_refresh(registers);
    uint64_t end = tb_gdb_now_ms() + 5000;
    while (tb_gdb_now_ms() < end) {
        tb_registers_want(registers, 0, 4);
        pump(gdb);
        if (tb_registers_count(registers) && tb_registers_row(registers, 0)->fresh &&
            !tb_registers_stale(registers) && !tb_gdb_pending(gdb)) {
            return;
        }
    }
    CHECK(false);
}


static void state(TbGdb_t *gdb, TbRegisterEditState_e expected)
{
    uint64_t end = tb_gdb_now_ms() + 6000;
    while (tb_gdb_now_ms() < end) {
        pump(gdb);
        TbRegisterEditState_e current = tb_registers_edit(registers)->state;
        if (current == expected && !tb_gdb_pending(gdb))
            return;
        if (current == TB_REGISTER_EDIT_FAILED && expected != current)
            break;
    }
    fprintf(stderr, "State %d expected %d: %s\n", tb_registers_edit(registers)->state, expected,
            tb_registers_edit(registers)->message);
    CHECK(false);
}


static void prepared(TbGdb_t *gdb, size_t row)
{
    idle(gdb);
    CHECK(tb_registers_edit_begin(registers, row));
    state(gdb, TB_REGISTER_EDIT_READY);
}


static TbGdb_t *start(const char *peer)
{
    CHECK(setenv("TB_FAKE_MODE", "context-registers-edit", 1) == 0);
    TbGdb_t *gdb = tb_gdb_create(emit, NULL);
    CHECK(gdb);
    context   = tb_context_create(gdb);
    registers = tb_registers_create(gdb, context);
    CHECK(context && registers);
    CHECK(tb_gdb_start(gdb, peer, 1000));
    uint64_t end = tb_gdb_now_ms() + 2000;
    while (tb_gdb_state(gdb) != TB_GDB_READY && tb_gdb_now_ms() < end)
        pump(gdb);

    CHECK(tb_gdb_state(gdb) == TB_GDB_READY);
    tb_registers_visible(registers, true);
    tb_registers_target(registers, true);
    tb_context_visible(context, false, true);
    command(gdb, "-stop", NULL);
    sample(gdb);

    return gdb;
}


static void close_session(TbGdb_t *gdb)
{
    tb_gdb_close(gdb);
    uint64_t end = tb_gdb_now_ms() + 5000;
    while (tb_gdb_state(gdb) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < end)
        pump(gdb);

    CHECK(tb_gdb_state(gdb) == TB_GDB_DISCONNECTED && tb_gdb_destroy(gdb));
    tb_registers_destroy(registers);
    tb_context_destroy(context);
}


static void normal(const char *peer)
{
    TbGdb_t *gdb = start(peer);
    CHECK(!tb_registers_edit_begin(registers, 2));
    CHECK(!tb_registers_edit_begin(registers, 3));
    CHECK(!tb_registers_edit_begin(registers, 999));

    prepared(gdb, 0);
    CHECK(!tb_registers_edit_value(registers, "0x10000000000000000"));
    CHECK(!tb_registers_edit_value(registers, "1+1"));
    CHECK(!tb_registers_edit_value(registers, "-1"));
    CHECK(!tb_registers_edit_value(registers, ""));
    CHECK(tb_registers_edit_value(registers, "0x42"));
    tb_registers_edit_cancel(registers);
    idle(gdb);
    command(gdb, "-register-edit-stats", NULL);
    CHECK(writes == 0);

    prepared(gdb, 1);
    CHECK(tb_registers_edit(registers)->number == 2);
    command(gdb, "-register-edit-stats", NULL);
    unsigned before = reads;
    CHECK(tb_registers_edit_apply(registers, "0x8000000000000001"));
    CHECK(!tb_registers_edit_apply(registers, "0x1"));
    state(gdb, TB_REGISTER_EDIT_ACKNOWLEDGED);
    idle(gdb);
    command(gdb, "-register-edit-stats", NULL);
    CHECK(writes == 1 && last_number == 2 && reads == before);
    CHECK(!tb_registers_row(registers, 1)->fresh && !tb_registers_edit(registers)->verified);
    CHECK(tb_registers_edit_verify(registers));
    idle(gdb);
    CHECK(tb_registers_edit(registers)->verified && tb_registers_edit(registers)->matches);
    CHECK(strcmp(tb_registers_edit(registers)->observed_value, "0x8000000000000001") == 0);
    sample(gdb);

    prepared(gdb, 0);
    CHECK(tb_context_select_frame(context, 1));
    idle(gdb);
    CHECK(!tb_registers_edit_apply(registers, "0x1"));
    state(gdb, TB_REGISTER_EDIT_FAILED);
    sample(gdb);
    CHECK(!tb_registers_edit_begin(registers, 0));
    CHECK(tb_context_select_frame(context, 0));
    sample(gdb);

    const unsigned failures[] = {1, 2, 3, 4, 9, 13};
    for (size_t i = 0; i < sizeof(failures) / sizeof(failures[0]); ++i) {
        mode(gdb, failures[i]);
        CHECK(tb_registers_edit_begin(registers, 0));
        state(gdb, TB_REGISTER_EDIT_FAILED);
        idle(gdb);
    }

    mode(gdb, 0);
    prepared(gdb, 0);
    mode(gdb, 10);
    CHECK(tb_registers_edit_apply(registers, "ab"));
    state(gdb, TB_REGISTER_EDIT_FAILED);
    command(gdb, "-register-edit-stats", NULL);
    CHECK(writes == 1);

    mode(gdb, 0);
    prepared(gdb, 0);
    tb_registers_refresh(registers);
    CHECK(!tb_registers_edit_apply(registers, "1"));
    state(gdb, TB_REGISTER_EDIT_FAILED);
    idle(gdb);
    sample(gdb);

    prepared(gdb, 0);
    tb_registers_target(registers, false);
    CHECK(!tb_registers_edit_apply(registers, "1"));
    state(gdb, TB_REGISTER_EDIT_FAILED);
    CHECK(!tb_registers_edit_begin(registers, 0));
    tb_registers_target(registers, true);
    sample(gdb);

    prepared(gdb, 0);
    mode(gdb, 11);
    CHECK(tb_registers_edit_apply(registers, "ab"));
    state(gdb, TB_REGISTER_EDIT_ACKNOWLEDGED);
    idle(gdb);
    CHECK(tb_registers_edit_verify(registers));
    idle(gdb);
    CHECK(tb_registers_edit(registers)->verified && !tb_registers_edit(registers)->matches);
    CHECK(strstr(tb_registers_edit(registers)->message, "DIFFERENT"));
    close_session(gdb);
}


static void uncertain(const char *peer, unsigned failure)
{
    TbGdb_t *gdb = start(peer);
    prepared(gdb, 0);
    mode(gdb, failure);
    CHECK(tb_registers_edit_apply(registers, "42"));
    state(gdb, TB_REGISTER_EDIT_UNCERTAIN);
    idle(gdb);
    CHECK(tb_context_write_blocked(context));
    CHECK(!tb_context_write_begin(context, 12345));
    tb_registers_target(registers, false);
    tb_registers_target(registers, true);
    CHECK(tb_context_write_blocked(context));
    CHECK(!tb_registers_edit_apply(registers, "43"));
    tb_registers_edit_cancel(registers);
    CHECK(tb_registers_edit(registers)->state == TB_REGISTER_EDIT_UNCERTAIN);
    CHECK(!tb_registers_edit_reconcile(registers));
    CHECK(!tb_registers_edit_begin(registers, 1));
    CHECK(tb_context_write_blocked(context));
    CHECK(tb_registers_edit_verify(registers));
    idle(gdb);
    CHECK(tb_registers_edit(registers)->verified);
    if (failure == 6) {
        CHECK(!tb_registers_edit_reconcile(registers));
        command(gdb, "-register-edit-release", NULL);
        CHECK(tb_context_write_blocked(context));
        CHECK(!tb_registers_edit_reconcile(registers));
    } else {
        CHECK(tb_registers_edit_reconcile(registers));
        CHECK(!tb_context_write_blocked(context));
    }

    command(gdb, "-register-edit-stats", NULL);
    CHECK(writes == 1);
    close_session(gdb);
}


static void lifecycle(const char *peer)
{
    TbGdb_t *gdb = start(peer);
    CHECK(tb_registers_edit_begin(registers, 0));
    pump(gdb);
    tb_registers_edit_cancel(registers);
    idle(gdb);
    CHECK(tb_registers_edit(registers)->state == TB_REGISTER_EDIT_NONE);
    command(gdb, "-register-edit-stats", NULL);
    CHECK(writes == 0);

    prepared(gdb, 0);
    mode(gdb, 1);
    CHECK(tb_registers_edit_apply(registers, "42"));
    state(gdb, TB_REGISTER_EDIT_FAILED);
    command(gdb, "-register-edit-stats", NULL);
    CHECK(writes == 0);

    mode(gdb, 0);
    prepared(gdb, 0);
    mode(gdb, 12);
    tb_registers_edit_cancel(registers);
    idle(gdb);
    mode(gdb, 0);
    CHECK(!tb_registers_edit_begin(registers, 0));
    command(gdb, "-register-edit-stats", NULL);
    CHECK(writes == 0);
    close_session(gdb);

    gdb = start(peer);
    mode(gdb, 14);
    CHECK(tb_registers_edit_begin(registers, 0));
    state(gdb, TB_REGISTER_EDIT_FAILED);
    idle(gdb);
    CHECK(!tb_registers_edit_begin(registers, 0));
    command(gdb, "-register-edit-stats", NULL);
    CHECK(writes == 0);
    close_session(gdb);

    gdb = start(peer);
    prepared(gdb, 0);
    mode(gdb, 8);
    CHECK(tb_registers_edit_apply(registers, "42"));
    state(gdb, TB_REGISTER_EDIT_UNCERTAIN);
    CHECK(tb_context_write_blocked(context));
    CHECK(!tb_registers_edit_verify(registers));
    CHECK(!tb_registers_edit_begin(registers, 0));
    command(gdb, "-register-edit-stats", NULL);
    CHECK(writes == 1);
    close_session(gdb);
}


int main(int argc, char **argv)
{
    CHECK(argc == 2);
    normal(argv[1]);
    uncertain(argv[1], 5);
    uncertain(argv[1], 7);
    uncertain(argv[1], 6);
    lifecycle(argv[1]);
    puts("PASS F05 authored MI: sparse IDs, bounds, metadata refusal, stale drafts, dispatch recheck, exact single write, explicit verification/mismatch, shared quarantine and lost completion");
    return EXIT_SUCCESS;
}
