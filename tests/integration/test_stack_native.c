#include "session.h"
#include "../support/check.h"
#include <string.h>

static TbSession *session;
static TbStack *stack;
static void settle(void)
{
    uint64_t until = tb_gdb_now_ms() + 6000;
    while (tb_gdb_now_ms() < until) {
        tb_session_pump(session, 2);
        const TbContextSnapshot *c = tb_context_snapshot(tb_session_context(session));
        const TbStackView *v = tb_stack_view(stack, 0);
        if (c->ready && !c->stale && v->anchor_valid && !v->reading) {
            uint64_t before = tb_session_revision(session);
            for (int i = 0; i < 10; ++i) { tb_session_pump(session, 2); }
            if (before == tb_session_revision(session)) { return; }
        }
    }
    for (size_t i = 0; i < tb_session_log_count(session); ++i) { fprintf(stderr, "%s\n", tb_session_log_line(session, i)); }
    CHECK(false);
}
static void state(TbSessionState wanted)
{
    uint64_t until = tb_gdb_now_ms() + 6000;
    while (tb_session_state(session) != wanted && tb_gdb_now_ms() < until) { tb_session_pump(session, 2); }
    CHECK(tb_session_state(session) == wanted);
}
static void names(void)
{
    CHECK(tb_stack_names(stack, 0)); settle(); CHECK(tb_stack_view(stack, 0)->names_ready);
}
static size_t name(const char *wanted)
{
    const TbStackView *v = tb_stack_view(stack, 0);
    for (size_t i = 0; i < v->name_count; ++i) { if (strcmp(v->names[i].name, wanted) == 0) { return i; } }
    return v->name_count;
}
static void run(const char *fixture, bool symbols, bool optimized)
{
    session = tb_session_create(); CHECK(session); stack = tb_session_stack(session);
    TbSessionConfig config = {.gdb = "gdb", .executable = fixture}; CHECK(tb_session_start(session, &config)); state(TB_SESSION_READY);
    tb_stack_visible(stack, 0, true); tb_stack_visible(stack, 1, true);
    tb_context_visible(tb_session_context(session), true, true);
    CHECK(tb_session_console(session, symbols ? "break stack_checkpoint" : "starti"));
    if (symbols) {
        for (int i = 0; i < 50; ++i) { tb_session_pump(session, 2); }
        CHECK(tb_session_continue(session));
    }
    state(TB_SESSION_STOPPED); settle();
    const TbStackView *v = tb_stack_view(stack, 0), *other = tb_stack_view(stack, 1);
    CHECK(v->anchor_valid && v->sp && v->sp == other->sp && !v->sample.sequence && !other->sample.sequence);
    uint64_t anchor = v->sp; CHECK(tb_stack_read(stack, 0)); settle(); CHECK(v->sample.sequence && !tb_stack_stale(stack, 0));
    char bounds[1024]; tb_stack_bounds(stack, 0, bounds, sizeof bounds); CHECK(strstr(bounds, "Verified bounds: unknown"));
    CHECK(tb_stack_navigate(stack, 0, "+0")); uint64_t manual = v->start;
    CHECK(tb_stack_follow(stack, 1)); CHECK(!other->sample.sequence);
    if (symbols) {
        CHECK(tb_context_select_frame(tb_session_context(session), 1)); settle(); CHECK(v->sp == anchor && v->start == manual && !v->follow);
        CHECK(tb_stack_frame_point(stack, 0)); settle(); CHECK(v->point_count == 1 && v->points[0].frame == 1);
        names(); size_t object = name("object"), reference = name("reference"), local = name("local");
        CHECK(object < v->name_count && reference < v->name_count && local < v->name_count);
        CHECK(tb_stack_variable_point(stack, 0, object)); settle();
        CHECK(v->guard_confirmation); CHECK(tb_stack_disable_calls(stack, 0)); settle();
        if (v->point_count != 2) { fprintf(stderr, "address point: %s\n", v->annotation_error); CHECK(false); }
        CHECK(v->points[1].address != 0); /* C++ operator& returns null if invoked. */
        CHECK(tb_stack_variable_point(stack, 0, reference)); settle(); CHECK(v->point_count == 3 || (optimized && *v->annotation_error));
        CHECK(tb_stack_variable_point(stack, 0, local)); settle(); CHECK(v->point_count >= 3 || (optimized && *v->annotation_error));
        CHECK(tb_stack_declare(stack, 0, anchor - 128, anchor + 128, false)); tb_stack_bounds(stack, 0, bounds, sizeof bounds);
        CHECK(strstr(bounds, "SP displacement from declared start: 128 bytes"));
        CHECK(tb_context_select_frame(tb_session_context(session), 0)); settle(); CHECK(v->point_count == 0 && v->sp == anchor);
        CHECK(tb_session_step(session, TB_STEP_INTO, true)); state(TB_SESSION_STOPPED); settle(); CHECK(v->start == manual && !v->follow);
        CHECK(tb_stack_follow(stack, 0)); CHECK(v->start == v->sp - v->size / 2 && tb_stack_stale(stack, 0));
    } else {
        names(); CHECK(v->name_count == 0); CHECK(v->anchor_valid);
    }
    tb_session_close(session); state(TB_SESSION_CLOSED); CHECK(tb_session_destroy(session));
}
static void threads(const char *fixture)
{
    session = tb_session_create(); CHECK(session); stack = tb_session_stack(session);
    TbSessionConfig config = {.gdb = "gdb", .executable = fixture}; CHECK(tb_session_start(session, &config)); state(TB_SESSION_READY);
    tb_context_visible(tb_session_context(session), true, true); tb_stack_visible(stack, 0, true);
    CHECK(tb_session_continue(session)); state(TB_SESSION_STOPPED); settle();
    const TbContextSnapshot *c = tb_context_snapshot(tb_session_context(session)); const TbStackView *v = tb_stack_view(stack, 0);
    CHECK(c->thread_count == 3 && c->frame_count >= 3); uint64_t main_lifetime = v->lifetime;
    char main_thread[32], worker[32]; (void)snprintf(main_thread, sizeof main_thread, "%s", c->selected_thread);
    (void)snprintf(worker, sizeof worker, "%s", c->threads[1].id);
    CHECK(tb_stack_read(stack, 0)); settle(); CHECK(v->sample.sequence);
    CHECK(tb_context_select_frame(tb_session_context(session), 2)); settle(); CHECK(v->anchor_valid && v->lifetime == main_lifetime);
    CHECK(tb_context_select_thread(tb_session_context(session), worker)); settle();
    CHECK(v->lifetime != main_lifetime && !v->sample.sequence && v->follow);
    uint64_t worker_lifetime = v->lifetime; CHECK(tb_stack_declare(stack, 0, v->sp - 128, v->sp + 128, false));
    CHECK(tb_stack_cached(stack, 0, -1)); CHECK(v->sample.lifetime == main_lifetime);
    CHECK(tb_context_select_thread(tb_session_context(session), main_thread)); settle(); CHECK(v->lifetime == main_lifetime);
    CHECK(tb_session_continue(session)); state(TB_SESSION_STOPPED); settle();
    CHECK(c->thread_count == 1 && !tb_context_lifetime_live(tb_session_context(session), worker_lifetime));
    tb_session_close(session); state(TB_SESSION_CLOSED); CHECK(tb_session_destroy(session));
}
int main(int argc, char **argv)
{
    CHECK(argc == 5); run(argv[1], true, false); run(argv[2], false, false); run(argv[3], true, true); threads(argv[4]);
    puts("PASS F02 actual native GDB: frame-0/caller SP separation, C++ operator& avoidance, reference/local address points, stripped metadata, explicit bytes, manual/refollow navigation, declarations and cleanup");
    return 0;
}
