#define _POSIX_C_SOURCE 200809L
#include "context.h"
#include "../support/check.h"
#include <string.h>

static TbContext *context;
static unsigned completions;
static void emit(void *unused, const TbGdbEvent *event)
{
    (void)unused;
    if (context) { (void)tb_context_event(context, event); }
    if (event->kind == TB_GDB_REPLY) { ++completions; }
}
static void pump(TbGdb *gdb) { tb_gdb_pump(gdb, 5); tb_context_pump(context); }
static void ready(TbGdb *gdb)
{
    uint64_t until = tb_gdb_now_ms() + 4000;
    while (tb_gdb_state(gdb) == TB_GDB_STARTING && tb_gdb_now_ms() < until) { pump(gdb); }
    CHECK(tb_gdb_state(gdb) == TB_GDB_READY);
}
static void fresh(TbGdb *gdb)
{
    uint64_t until = tb_gdb_now_ms() + 4000;
    while ((!tb_context_snapshot(context)->ready || tb_context_snapshot(context)->stale) && tb_gdb_now_ms() < until) { pump(gdb); }
    CHECK(tb_context_snapshot(context)->ready && !tb_context_snapshot(context)->stale);
}
static void command(TbGdb *gdb, const char *operation)
{
    CHECK(tb_gdb_request(gdb, operation, NULL, 0, TB_GDB_SESSION, 999, 1000, NULL));
}
static void stop(TbGdb *gdb)
{
    command(gdb, "-stop");
    uint64_t until = tb_gdb_now_ms() + 3000;
    while (tb_gdb_state(gdb) != TB_GDB_STOPPED && tb_gdb_now_ms() < until) { pump(gdb); }
    CHECK(tb_gdb_state(gdb) == TB_GDB_STOPPED);
}
static void run_case(const char *binary, const char *mode)
{
    CHECK(setenv("TB_FAKE_MODE", mode, 1) == 0);
    TbGdb *gdb = tb_gdb_create(emit, NULL); CHECK(gdb);
    context = tb_context_create(gdb); CHECK(context);
    CHECK(tb_gdb_start(gdb, binary, 1000)); ready(gdb); stop(gdb);
    for (int i = 0; i < 10; ++i) { pump(gdb); }
    CHECK(!tb_context_snapshot(context)->ready); /* Invisible views do no reads. */
    CHECK(!tb_context_read_arguments(context));
    tb_context_visible(context, true, true);
    if (strcmp(mode, "context-stale") == 0) {
        uint64_t until = tb_gdb_now_ms() + 1500;
        while (tb_gdb_pending(gdb) == 0 && tb_gdb_now_ms() < until) { pump(gdb); }
        /* Wait until the intentionally delayed stack query has reached the peer. */
        for (int i = 0; i < 30; ++i) { pump(gdb); }
        tb_context_refresh(context); command(gdb, "-ctx-release");
    }
    fresh(gdb);
    const TbContextSnapshot *snapshot = tb_context_snapshot(context);
    CHECK(snapshot->thread_count == (strcmp(mode, "context-many") == 0 ? TB_CONTEXT_THREADS : 2));
    CHECK(snapshot->threads_limited == (strcmp(mode, "context-many") == 0));
    CHECK(strcmp(snapshot->threads[1].id, "42") == 0);
    CHECK(strcmp(snapshot->selected_thread, "7") == 0);
    if (strcmp(mode, "context-unwind") == 0 || strcmp(mode, "context-malformed") == 0) {
        CHECK(snapshot->frame_count == 0 && *snapshot->stack_error);
        CHECK(snapshot->selected.level == 0); /* Partial unwind stays honest. */
    } else {
        CHECK(snapshot->frame_count == TB_CONTEXT_FRAMES && snapshot->more_frames);
        CHECK(strcmp(snapshot->frames[0].function, "main_0") == 0);
        CHECK(strstr(snapshot->frames[0].arguments, "depth=<read required>"));
        CHECK(strstr(snapshot->frames[1].arguments, "depth=<read required>"));
        CHECK(!snapshot->frames[1].arguments_read);
        unsigned before_selection = completions;
        CHECK(tb_context_select_frame(context, 1)); fresh(gdb);
        CHECK(completions == before_selection + 2); /* Select + current metadata; no inventory/frame-list refresh. */
        CHECK(tb_context_read_arguments(context));
        CHECK(!tb_context_read_arguments(context)); /* No overlapping reads. */
        if (strcmp(mode, "context-args-stale") == 0 || strcmp(mode, "context-args-resume") == 0) {
            for (int i = 0; i < 20; ++i) { pump(gdb); }
            if (strcmp(mode, "context-args-resume") == 0) {
                command(gdb, "-run");
                for (int i = 0; i < 20; ++i) { pump(gdb); }
                command(gdb, "-ctx-release");
                for (int i = 0; i < 20; ++i) { pump(gdb); }
                CHECK(snapshot->stale && !snapshot->refreshing && !snapshot->frames[1].arguments_read);
                stop(gdb); fresh(gdb);
            } else { tb_context_refresh(context); command(gdb, "-ctx-release"); fresh(gdb); }
            CHECK(!snapshot->frames[1].arguments_read);
            CHECK(!strstr(snapshot->frames[1].arguments, "OBSOLETE"));
        } else {
            uint64_t until = tb_gdb_now_ms() + 2000;
            while (!snapshot->frames[1].arguments_read && !*snapshot->arguments_error && tb_gdb_now_ms() < until) { pump(gdb); }
            if (strcmp(mode, "context-args-error") == 0 || strcmp(mode, "context-args-missing") == 0) {
                CHECK(strstr(snapshot->arguments_error, "unavailable"));
                CHECK(!snapshot->frames[1].arguments_read);
            } else {
                CHECK(snapshot->frames[1].arguments_read);
                CHECK(strstr(snapshot->frames[1].arguments, "<optimized out>"));
                CHECK(!snapshot->frames[0].arguments_read);
            }
        }
        /* Selecting the current context preserves values and freshness. */
        uint64_t revision = tb_context_revision(context);
        TbGdbGeneration generation = tb_gdb_generation(gdb);
        bool arguments_read = snapshot->frames[1].arguments_read;
        CHECK(tb_context_select_frame(context, snapshot->selected.level));
        CHECK(tb_context_select_thread(context, snapshot->selected_thread));
        CHECK(tb_context_revision(context) == revision && !snapshot->stale);
        CHECK(tb_gdb_generation(gdb).context == generation.context);
        CHECK(snapshot->frames[1].arguments_read == arguments_read);
        tb_context_visible(context, false, false);
        CHECK(!tb_context_read_arguments(context));
        tb_context_visible(context, true, true); fresh(gdb);
        CHECK(!snapshot->frames[1].arguments_read);
        CHECK(tb_context_page(context, 1)); fresh(gdb);
        CHECK(snapshot->first_frame == 32 && snapshot->frame_count == 8 && !snapshot->more_frames);
        CHECK(tb_context_select_frame(context, 37)); fresh(gdb); CHECK(snapshot->selected.level == 37);
        CHECK(tb_context_page(context, -1)); fresh(gdb);
        CHECK(!tb_context_select_frame(context, 900));
        CHECK(tb_context_select_thread(context, "42"));
        if (strcmp(mode, "context-selection-stale") == 0) {
            for (int i = 0; i < 20; ++i) { pump(gdb); }
        }
        CHECK(tb_context_select_thread(context, "7")); /* Latest rapid choice wins. */
        if (strcmp(mode, "context-selection-stale") == 0) { command(gdb, "-ctx-release"); }
        fresh(gdb); CHECK(strcmp(snapshot->selected_thread, "7") == 0);
        CHECK(tb_context_select_thread(context, "42"));
        pump(gdb); pump(gdb);
        CHECK(!tb_context_select_frame(context, 0)); /* Old thread's rows are stale. */
        fresh(gdb);
        CHECK(strcmp(snapshot->selected_thread, "42") == 0 && snapshot->selected.level == 0);
        CHECK(strstr(snapshot->frames[0].function, "worker"));
        command(gdb, "-ctx-exit");
        for (int i = 0; i < 30; ++i) { pump(gdb); }
        fresh(gdb); CHECK(snapshot->thread_count == 1 && strcmp(snapshot->selected_thread, "7") == 0);
        CHECK(!tb_context_select_thread(context, "42"));
    }
    unsigned idle = completions;
    for (int i = 0; i < 20; ++i) { pump(gdb); }
    CHECK(completions == idle); /* No inspection busy loop at idle. */
    command(gdb, "-run");
    for (int i = 0; i < 20; ++i) { pump(gdb); }
    CHECK(snapshot->stale && !snapshot->refreshing);
    CHECK(!tb_context_read_arguments(context));
    CHECK(!tb_context_select_thread(context, "7") && !tb_context_select_frame(context, 0));
    tb_context_refresh(context);
    idle = completions;
    for (int i = 0; i < 20; ++i) { pump(gdb); }
    CHECK(completions == idle); /* No reads while running. */
    stop(gdb); fresh(gdb);
    CHECK(snapshot->generation.stop == tb_gdb_generation(gdb).stop);
    tb_gdb_close(gdb);
    uint64_t until = tb_gdb_now_ms() + 3000;
    while (tb_gdb_state(gdb) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < until) { pump(gdb); }
    CHECK(tb_gdb_state(gdb) == TB_GDB_DISCONNECTED);
    uint64_t revision = tb_context_revision(context);
    tb_context_reset(context);
    CHECK(tb_context_revision(context) > revision);
    CHECK(!snapshot->ready && snapshot->thread_count == 0 && snapshot->frame_count == 0);
    CHECK(snapshot->selected.level == -1 && snapshot->stopped.level == -1);
    CHECK(tb_gdb_start(gdb, binary, 1000)); ready(gdb);
    CHECK(!snapshot->ready); /* No old session data becomes live after reconnect. */
    tb_gdb_close(gdb);
    until = tb_gdb_now_ms() + 3000;
    while (tb_gdb_state(gdb) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < until) { pump(gdb); }
    CHECK(tb_gdb_state(gdb) == TB_GDB_DISCONNECTED);
    tb_context_destroy(context); context = NULL; CHECK(tb_gdb_destroy(gdb));
}
int main(int argc, char *argv[])
{
    CHECK(argc == 2);
    run_case(argv[1], "context"); run_case(argv[1], "context-stale");
    run_case(argv[1], "context-unwind"); run_case(argv[1], "context-malformed");
    run_case(argv[1], "context-args-stale"); run_case(argv[1], "context-args-resume"); run_case(argv[1], "context-args-error"); run_case(argv[1], "context-args-missing");
    run_case(argv[1], "context-selection-stale"); run_case(argv[1], "context-many");
    CHECK(unsetenv("TB_FAKE_MODE") == 0);
    puts("PASS M06 neutral context, names-only automatic args, selected-frame explicit reads/errors/stale denial, paging, rapid selection, stale replies, thread exit, unwind errors and bounded idle/running work");
    return 0;
}
