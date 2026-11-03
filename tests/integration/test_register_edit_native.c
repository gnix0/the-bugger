#include "session/session.h"
#include "../support/check.h"

#include <inttypes.h>
#include <string.h>

static TbSession_t *session;
static const char *log_file;

static void cleanup(void)
{
    if (!session)
        return;

    if (log_file)
        (void)tb_session_save_log(session, log_file);

    if (tb_session_kind(session) == TB_SESSION_LOCAL)
        tb_session_close(session);
    else
        tb_session_disconnect(session, false);

    uint64_t end = tb_gdb_now_ms() + 6000;
    while (tb_session_active(session) && tb_gdb_now_ms() < end)
        tb_session_pump(session, 5);

    CHECK(!tb_session_active(session));
    CHECK(tb_session_destroy(session));
    session = NULL;
}


static void context_ready(void)
{
    uint64_t end = tb_gdb_now_ms() + 7000;
    const TbContextSnapshot_t *s = tb_context_snapshot(tb_session_context(session));
    while (tb_gdb_now_ms() < end) {
        tb_session_pump(session, 5);
        if (tb_session_state(session) == TB_SESSION_STOPPED && s->ready && !s->stale)
            return;
    }

    fprintf(stderr, "Context: %s / %s\n", tb_session_message(session), s->error);
    CHECK(false);
}


static void sampled(TbRegisters_t *r, size_t row)
{
    uint64_t end = tb_gdb_now_ms() + 7000;
    while (tb_gdb_now_ms() < end) {
        tb_registers_want(r, row, 1);
        tb_session_pump(session, 5);
        const TbRegister_t *v = tb_registers_row(r, row);
        if (!tb_registers_stale(r) && v && v->fresh && v->available && tb_registers_settled(r)) {
            /* Context and other setup owners must also finish before Review. */
            for (int i = 0; i < 8; ++i)
                tb_session_pump(session, 5);
            return;
        }
    }

    fprintf(stderr, "Register: %s\n", tb_registers_error(r));
    CHECK(false);
}


static size_t find_register(TbRegisters_t *r, const char *name)
{
    uint64_t end = tb_gdb_now_ms() + 7000;
    while (!tb_registers_count(r) && tb_gdb_now_ms() < end)
        tb_session_pump(session, 5);

    for (size_t i = 0; i < tb_registers_count(r); ++i) {
        if (strcmp(tb_registers_row(r, i)->name, name) == 0)
            return i;
    }

    fprintf(stderr, "Register %s not discovered\n", name);
    CHECK(false);
    return 0;
}


static void prepared(TbRegisters_t *r, size_t row)
{
    CHECK(tb_registers_edit_begin(r, row));
    uint64_t end = tb_gdb_now_ms() + 7000;
    while (tb_registers_edit(r)->state == TB_REGISTER_EDIT_PROBING && tb_gdb_now_ms() < end)
        tb_session_pump(session, 5);

    if (tb_registers_edit(r)->state != TB_REGISTER_EDIT_READY)
        fprintf(stderr, "Edit: %s\n", tb_registers_edit(r)->message);

    CHECK(tb_registers_edit(r)->state == TB_REGISTER_EDIT_READY);
}


static void written(TbRegisters_t *r, const char *value)
{
    CHECK(tb_registers_edit_value(r, value));
    CHECK(tb_registers_edit_apply(r, value));
    CHECK(!tb_registers_edit_apply(r, value));

    uint64_t end = tb_gdb_now_ms() + 7000;
    while ((tb_registers_edit(r)->state == TB_REGISTER_EDIT_CHECKING ||
            tb_registers_edit(r)->state == TB_REGISTER_EDIT_PENDING) && tb_gdb_now_ms() < end) {
        tb_session_pump(session, 5);
    }

    if (tb_registers_edit(r)->state != TB_REGISTER_EDIT_ACKNOWLEDGED)
        fprintf(stderr, "Write: %s\n", tb_registers_edit(r)->message);

    CHECK(tb_registers_edit(r)->state == TB_REGISTER_EDIT_ACKNOWLEDGED);
    context_ready();
    for (int i = 0; i < 20; ++i)
        tb_session_pump(session, 5);

    CHECK(!tb_registers_edit(r)->verified);
    CHECK(tb_registers_edit_verify(r));
    end = tb_gdb_now_ms() + 7000;
    while (!tb_registers_edit(r)->verified && tb_gdb_now_ms() < end)
        tb_session_pump(session, 5);

    CHECK(tb_registers_edit(r)->verified);
    CHECK(tb_registers_edit(r)->matches);
    printf("VERIFIED %s ID %u requested %s observed %s\n", tb_registers_edit(r)->name,
           tb_registers_edit(r)->number, tb_registers_edit(r)->new_value,
           tb_registers_edit(r)->observed_value);
    (void)fflush(stdout);
}


int main(int argc, char **argv)
{
    bool board = argc == 6 && strcmp(argv[1], "--board") == 0;
    CHECK(argc == 2 || board);
    log_file = getenv("TB_REGISTER_EDIT_LOG");
    session = tb_session_create();
    CHECK(session && atexit(cleanup) == 0);

    TbSessionConfig_t config = {
        .kind       = board ? TB_SESSION_REMOTE : TB_SESSION_LOCAL,
        .gdb        = board ? argv[2] : "gdb",
        .endpoint   = board ? argv[3] : NULL,
        .executable = board ? NULL : argv[1]
    };

    CHECK(tb_session_start(session, &config));
    if (!board) {
        uint64_t end = tb_gdb_now_ms() + 7000;
        while (tb_session_state(session) != TB_SESSION_READY && tb_gdb_now_ms() < end)
            tb_session_pump(session, 5);

        CHECK(tb_session_state(session) == TB_SESSION_READY);
        CHECK(tb_session_console(session, "start"));
    }

    TbRegisters_t *r = tb_session_registers(session);
    tb_registers_visible(r, true);
    context_ready();
    CHECK(strstr(tb_session_capabilities(session)->architecture, board ? argv[5] : "x86-64"));

    size_t row = find_register(r, board ? argv[4] : "rax");
    sampled(r, row);
    prepared(r, row);

    char original[19];
    memcpy(original, tb_registers_edit(r)->old_value, sizeof(original));
    printf("TARGET %s T%s register %s ID %u width %u ORIGINAL %s\n",
           tb_session_capabilities(session)->architecture, tb_registers_edit(r)->thread,
           tb_registers_edit(r)->name, tb_registers_edit(r)->number, tb_registers_edit(r)->bits, original);
    (void)fflush(stdout);

    tb_registers_edit_cancel(r);
    for (int i = 0; i < 20; ++i)
        tb_session_pump(session, 5);

    sampled(r, row);
    prepared(r, row);
    CHECK(strcmp(tb_registers_edit(r)->old_value, original) == 0);
    CHECK(!tb_registers_edit_value(r, "1+1"));
    CHECK(!tb_registers_edit_value(r, "0x10000000000000000"));
    CHECK(!tb_registers_edit_value(r, "-1"));

    const TbContextSnapshot_t *s = tb_context_snapshot(tb_session_context(session));
    char pc[sizeof(s->selected.address)];
    memcpy(pc, s->selected.address, sizeof(pc));

    char changed[19];
    (void)snprintf(changed, sizeof(changed), "0x%" PRIx64, (uint64_t)strtoull(original, NULL, 16) ^ 1U);
    written(r, changed);
    CHECK(strcmp(pc, s->selected.address) == 0);
    sampled(r, row);
    prepared(r, row);
    written(r, original);
    CHECK(strcmp(pc, s->selected.address) == 0);
    printf("RESTORED %s; PC unchanged at %s\n", original, pc);

    cleanup();
    puts("PASS F05 real GDB CPU register metadata, Review/Cancel, bounded hex, single Apply, explicit Verify and restoration");
    return EXIT_SUCCESS;
}
