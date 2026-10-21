#include "native.h"
#include "../support/check.h"
#include <string.h>

static void dump(TbNative *native)
{
    for (size_t i = 0; i < tb_native_log_count(native); ++i) { fprintf(stderr, "%s\n", tb_native_log_line(native, i)); }
    const TbContextSnapshot *s = tb_context_snapshot(tb_native_context(native));
    fprintf(stderr, "context: ready=%d stale=%d T%s frames=%zu errors=%s / %s / %s\n", s->ready, s->stale, s->selected_thread, s->frame_count, s->error, s->stack_error, s->arguments_error);
    fprintf(stderr, "selected #%d / stopped T%s #%d PC %s\n", s->selected.level, s->stopped_thread, s->stopped.level, s->stopped.address);
}
static void wait_state(TbNative *native, TbNativeState state)
{
    uint64_t until = tb_gdb_now_ms() + 8000;
    while (tb_native_state(native) != state && tb_gdb_now_ms() < until) { tb_native_pump(native, 5); }
    if (tb_native_state(native) != state) { dump(native); CHECK(false); }
}
static void fresh(TbNative *native)
{
    const TbContextSnapshot *s = tb_context_snapshot(tb_native_context(native));
    uint64_t until = tb_gdb_now_ms() + 8000;
    while ((!s->ready || s->stale) && tb_gdb_now_ms() < until) { tb_native_pump(native, 5); }
    if (!s->ready || s->stale) { dump(native); CHECK(false); }
}
static bool function(const TbContextSnapshot *snapshot, const char *name)
{
    for (size_t i = 0; i < snapshot->frame_count; ++i) {
        if (strstr(snapshot->frames[i].function, name)) { return true; }
    }
    return false;
}
static void run(const char *fixture, bool symbols, bool deep)
{
    TbNative *native = tb_native_create(); CHECK(native);
    TbNativeWords arguments = {0}; char error[128];
    CHECK(tb_native_words(deep ? "--deep" : "", &arguments, error, sizeof error));
    TbNativeConfig config = {.gdb = "gdb", .executable = fixture, .arguments = &arguments};
    CHECK(tb_native_start(native, &config)); wait_state(native, TB_NATIVE_READY);
    TbContext *context = tb_native_context(native);
    tb_context_visible(context, true, true);
    CHECK(tb_native_continue(native)); wait_state(native, TB_NATIVE_STOPPED); fresh(native);
    const TbContextSnapshot *s = tb_context_snapshot(context);
    CHECK(s->thread_count == 3 && *s->selected_thread && s->selected.level == 0);
    CHECK(strcmp(s->selected_thread, s->stopped_thread) == 0 && s->stopped.level == 0);
    CHECK(*s->stopped.address && *s->selected.address);
    if (*s->error || *s->stack_error) { dump(native); CHECK(false); }
    if (symbols) { CHECK(function(s, "recursive")); }
    printf("M06 actual GDB %s: stopped T%s PC %s, %zu threads\n", fixture, s->stopped_thread, s->stopped.address, s->thread_count);
    for (size_t i = 0; i < s->frame_count; ++i) {
        printf("  #%d %s(%s) %s:%d\n", s->frames[i].level, s->frames[i].function,
               s->frames[i].arguments, s->frames[i].file, s->frames[i].line);
    }
    if (deep) {
        CHECK(s->frame_count == TB_CONTEXT_FRAMES && s->more_frames);
        CHECK(tb_context_page(context, 1)); fresh(native); CHECK(s->first_frame == 32);
        CHECK(tb_context_page(context, -1)); fresh(native);
    }
    for (size_t i = 0; i < s->frame_count; ++i) { CHECK(!s->frames[i].arguments_read); }
    if (symbols) {
        size_t argument_frame = 0;
        /* Optimizers can omit both recursive's and tail_bridge's formal
         * parameters. Keep depth mandatory for the unoptimized deep fixture;
         * otherwise use main's reported argc if depth is absent in raw MI. */
        const char *required = "depth=<read required>", *name = "depth=";
        while (argument_frame < s->frame_count && !strstr(s->frames[argument_frame].arguments, required)) { ++argument_frame; }
        if (argument_frame == s->frame_count && !deep) {
            required = "argc=<read required>"; name = "argc="; argument_frame = 0;
            while (argument_frame < s->frame_count && !strstr(s->frames[argument_frame].arguments, required)) { ++argument_frame; }
        }
        CHECK(argument_frame < s->frame_count);
        CHECK(strstr(s->frames[argument_frame].arguments, required));
        CHECK(tb_context_select_frame(context, s->frames[argument_frame].level)); fresh(native);
        CHECK(tb_context_read_arguments(context));
        uint64_t argument_deadline = tb_gdb_now_ms() + 5000;
        while (!s->frames[argument_frame].arguments_read && !*s->arguments_error && tb_gdb_now_ms() < argument_deadline) { tb_native_pump(native, 5); }
        CHECK(s->frames[argument_frame].arguments_read);
        CHECK(strstr(s->frames[argument_frame].arguments, name));
        CHECK(!strstr(s->frames[argument_frame].arguments, "<read required>"));
        tb_context_refresh(context); fresh(native);
        CHECK(!s->frames[argument_frame].arguments_read);
    }
    CHECK(s->frame_count > 2);
    int caller = s->frames[2].level;
    char stopped_address[48]; (void)snprintf(stopped_address, sizeof stopped_address, "%s", s->stopped.address);
    CHECK(tb_context_select_frame(context, caller)); fresh(native);
    CHECK(s->selected.level == caller && strcmp(stopped_address, s->stopped.address) == 0);
    CHECK(tb_native_console(native, "echo context-console-ok\\n"));
    for (int i = 0; i < 50; ++i) { tb_native_pump(native, 5); }
    fresh(native); CHECK(s->selected.level == caller);
    char main_thread[32], worker[32];
    (void)snprintf(main_thread, sizeof main_thread, "%s", s->selected_thread);
    (void)snprintf(worker, sizeof worker, "%s", s->threads[1].id);
    CHECK(strcmp(worker, main_thread) != 0);
    CHECK(tb_context_select_thread(context, worker)); fresh(native);
    CHECK(strcmp(s->selected_thread, worker) == 0 && s->selected.level == 0);
    CHECK(strcmp(s->stopped_thread, main_thread) == 0);
    if (symbols) { CHECK(function(s, "worker_main")); }
    CHECK(tb_context_select_thread(context, main_thread)); fresh(native);
    CHECK(tb_native_continue(native));
    wait_state(native, TB_NATIVE_STOPPED); fresh(native);
    CHECK(s->thread_count == 1); CHECK(!tb_context_select_thread(context, worker));
    CHECK(!tb_context_select_frame(context, 999999));
    CHECK(tb_native_continue(native)); wait_state(native, TB_NATIVE_RUNNING);
    CHECK(s->stale && !tb_context_select_frame(context, 0));
    CHECK(tb_native_pause(native)); wait_state(native, TB_NATIVE_STOPPED); fresh(native);
    tb_native_close(native); wait_state(native, TB_NATIVE_CLOSED);
    CHECK(s->stale); CHECK(tb_native_destroy(native));
}
int main(int argc, char *argv[])
{
    CHECK(argc == 4);
    run(argv[1], true, true); run(argv[2], true, false); run(argv[3], false, false);
    puts("PASS M06 real GDB recursive/paged and optimized/stripped stacks, arguments, thread/frame selection, stop-PC preservation, thread exit and resume invalidation");
    return 0;
}
