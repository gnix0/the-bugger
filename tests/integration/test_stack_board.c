/* Opt-in physical F02 runner. Requires an existing, disposable STM32 session
 * and already-programmed matching fixture. Never part of test-ci. */
#include "session.h"
#include "../support/check.h"
#include <inttypes.h>
#include <string.h>

static TbSession *session;
static TbStack *stack;
static void dump(void)
{
    for (size_t i = 0; i < tb_session_log_count(session); ++i) { fprintf(stderr, "%s\n", tb_session_log_line(session, i)); }
}
static void settled(void)
{
    uint64_t end = tb_gdb_now_ms() + 8000;
    while (tb_gdb_now_ms() < end) {
        tb_session_pump(session, 5); const TbStackView *v = tb_stack_view(stack, 0);
        const TbContextSnapshot *c = tb_context_snapshot(tb_session_context(session));
        if (tb_session_state(session) == TB_SESSION_STOPPED && c->ready && !c->stale && v->anchor_valid && !v->reading) {
            uint64_t before = tb_session_revision(session);
            for (int i = 0; i < 12; ++i) { tb_session_pump(session, 5); }
            if (before == tb_session_revision(session)) { return; }
        }
    }
    dump(); CHECK(false);
}
int main(int argc, char **argv)
{
    CHECK(argc == 4); const char *log = getenv("TB_STACK_BOARD_LOG"); CHECK(log && *log);
    session = tb_session_create(); CHECK(session); stack = tb_session_stack(session);
    TbOpenOcdPlan *external = calloc(1, sizeof *external); CHECK(external);
    (void)snprintf(external->endpoint, sizeof external->endpoint, "%s", argv[2]);
    TbSessionConfig config = {.kind = TB_SESSION_EXTENDED_REMOTE, .gdb = argv[1], .endpoint = argv[2], .executable = argv[3], .openocd = external};
    CHECK(tb_session_start(session, &config));
    tb_context_visible(tb_session_context(session), true, true); tb_stack_visible(stack, 0, true);
    uint64_t end = tb_gdb_now_ms() + 10000;
    while (tb_session_state(session) == TB_SESSION_STARTING && tb_gdb_now_ms() < end) { tb_session_pump(session, 5); }
    if (tb_session_state(session) == TB_SESSION_RUNNING) { CHECK(tb_session_halt(session)); }
    settled();
    CHECK(tb_session_capabilities(session)->address_bits == 32 && strstr(tb_session_capabilities(session)->architecture, "arm"));
    CHECK(tb_session_console(session, "tbreak fixture_checkpoint")); settled();
    uint64_t stop = tb_context_snapshot(tb_session_context(session))->generation.stop;
    CHECK(tb_session_continue(session)); end = tb_gdb_now_ms() + 8000;
    while (tb_context_snapshot(tb_session_context(session))->generation.stop == stop && tb_gdb_now_ms() < end) { tb_session_pump(session, 5); }
    settled();
    const TbStackView *v = tb_stack_view(stack, 0); const TbContextSnapshot *c = tb_context_snapshot(tb_session_context(session));
    CHECK(strstr(c->selected.function, "fixture_checkpoint"));
    CHECK(v->size == 128 && v->grouping == 4 && !v->sample.sequence);
    printf("Physical ARM: T%s lifetime %" PRIu64 " frame-0 SP 0x%" PRIx64 ", requested unread 128-byte window 0x%" PRIx64 "\n", v->thread, v->lifetime, v->sp, v->start);
    CHECK(tb_stack_size(stack, 0, 32));
    /* Only the reviewed fixture SRAM is sampled. These numeric bounds are
     * a runner precondition, not verified per-thread ownership in the model. */
    CHECK(v->start >= 0x20000000 && v->start <= 0x20020000 - 32);
    CHECK(tb_stack_read(stack, 0)); settled(); CHECK(v->sample.sequence && !tb_stack_stale(stack, 0));
    for (unsigned i = 0; i < 32; ++i) { CHECK(v->sample.valid[i]); }
    uint64_t anchor = v->sp, sequence = v->sample.sequence;
    CHECK(c->frame_count > 1); CHECK(tb_context_select_frame(tb_session_context(session), 1)); settled();
    CHECK(v->sp == anchor && v->sample.sequence == sequence);
    CHECK(tb_stack_frame_point(stack, 0)); settled(); CHECK(v->point_count == 1 && v->points[0].frame == 1);
    CHECK(tb_stack_names(stack, 0)); settled(); CHECK(v->names_ready);
    size_t eligible = 0; while (eligible < v->name_count && !v->names[eligible].eligible) { ++eligible; }
    CHECK(eligible < v->name_count);
    CHECK(tb_stack_variable_point(stack, 0, eligible)); settled();
    if (v->guard_confirmation) { CHECK(tb_stack_disable_calls(stack, 0)); settled(); }
    CHECK(v->point_count == 2 || *v->annotation_error);
    printf("Physical variable point: %s | %s\n", v->point_count == 2 ? "reported address" : "unsupported backend location", v->annotation_error);
    CHECK(v->sample.sequence == sequence);
    CHECK(tb_stack_declare(stack, 0, 0x20000000, 0x20020000, false)); char text[1024];
    tb_stack_bounds(stack, 0, text, sizeof text); puts(text); CHECK(strstr(text, "Verified bounds: unknown") && strstr(text, "SP displacement from declared start"));
    CHECK(tb_stack_navigate(stack, 0, "+0")); uint64_t manual = v->start;
    CHECK(tb_context_select_frame(tb_session_context(session), 0)); settled();
    CHECK(tb_session_step(session, TB_STEP_INTO, true)); settled(); CHECK(v->start == manual && !v->follow && v->sample.sequence == sequence);
    CHECK(tb_stack_follow(stack, 0)); CHECK(v->start == v->sp - 16 && v->sample.sequence == sequence);
    CHECK(tb_stack_stale(stack, 0)); CHECK(tb_stack_read(stack, 0)); settled(); CHECK(v->sample.sequence > sequence);
    tb_session_disconnect(session, false); end = tb_gdb_now_ms() + 8000;
    while (tb_session_state(session) != TB_SESSION_CLOSED && tb_gdb_now_ms() < end) { tb_session_pump(session, 5); }
    if (tb_session_state(session) != TB_SESSION_CLOSED) { dump(); CHECK(false); }
    CHECK(tb_session_save_log(session, log)); CHECK(tb_session_destroy(session)); free(external);
    puts("PASS F02 physical STM32: frame-0/caller SP, exact SRAM bytes, read-free browsing/refollow, guarded address query, unknown bounds/declared displacement and leave-halted disconnect; no flash/reset");
    return 0;
}
