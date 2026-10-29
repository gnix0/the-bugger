#define _POSIX_C_SOURCE 200809L
#include "setup_restore.h"
#include "profiles.h"
#include "../support/check.h"
#include <poll.h>
#include <string.h>
#include <unistd.h>

/* Opt-in, already-programmed physical target. No flash or implicit value reads.
 * Arguments name the reviewed firmware, code location and explicit RAM object. */
static TbSession *session;
static TbOpenOcdPlan *plan;
static TbSessionConfig config;
static const char *source, *watch_expression, *memory_expression, *code_location;
static unsigned log_serial;
static char settings[] = "/tmp/the-bugger-setup-board-XXXXXX";
static void pump(void) { tb_session_pump(session, 5); }
static void dump(void)
{
    fprintf(stderr, "Session: %s / %s\n", tb_session_state_name(session), tb_session_message(session));
    for (size_t i = 0; i < tb_session_log_count(session); ++i) { fprintf(stderr, "%s\n", tb_session_log_line(session, i)); }
}
static void close_session(void)
{
    if (!session) { return; }
    if (tb_session_state(session) == TB_SESSION_RUNNING) {
        (void)tb_session_pause(session); uint64_t end = tb_gdb_now_ms() + 3000;
        while (tb_session_state(session) == TB_SESSION_RUNNING && tb_gdb_now_ms() < end) { pump(); }
    }
    const char *log = getenv("TB_SETUP_BOARD_LOG");
    if (log) { char path[4096]; (void)snprintf(path, sizeof path, "%s.%u.log", log, log_serial++); (void)tb_session_save_log(session, path); }
    tb_session_disconnect(session, false); uint64_t end = tb_gdb_now_ms() + 6000;
    while (tb_session_active(session) && tb_gdb_now_ms() < end) { pump(); }
    if (!tb_session_active(session)) { CHECK(tb_session_destroy(session)); session = NULL; }
}
static void cleanup(void) { close_session(); free(plan); }
static void stopped(uint64_t previous)
{
    uint64_t end = tb_gdb_now_ms() + 12000;
    const TbContextSnapshot *ctx = tb_context_snapshot(tb_session_context(session));
    while ((tb_session_state(session) != TB_SESSION_STOPPED || ctx->stale || !ctx->ready || ctx->generation.stop <= previous) && tb_gdb_now_ms() < end) { pump(); }
    if (tb_session_state(session) != TB_SESSION_STOPPED || ctx->stale || !ctx->ready || ctx->generation.stop <= previous) { dump(); CHECK(false); }
    CHECK(*ctx->stopped.address && strtoull(ctx->stopped.address, NULL, 0));
}
static void idle(void)
{
    TbBreakpoints *b = tb_session_breakpoints(session); uint64_t end = tb_gdb_now_ms() + 6000;
    while ((!tb_break_ready(b) || tb_break_busy(b)) && tb_gdb_now_ms() < end) { pump(); }
    if (!tb_break_ready(b) || tb_break_busy(b)) { dump(); CHECK(false); }
}
static void start(void)
{
    session = tb_session_create(); CHECK(session && tb_session_start(session, &config));
    tb_context_visible(tb_session_context(session), true, true);
}
static void disconnect(bool resume)
{
    tb_session_disconnect(session, resume); uint64_t end = tb_gdb_now_ms() + 6000;
    while (tb_session_active(session) && tb_gdb_now_ms() < end) { pump(); }
    CHECK(tb_session_state(session) == TB_SESSION_CLOSED && tb_session_destroy(session)); session = NULL;
}
static uint32_t read_word(const char *expression)
{
    TbMemory *m = tb_session_memory(session); tb_memory_visible(m, 3, true);
    CHECK(tb_memory_length(m, 3, 4) && tb_memory_address(m, 3, expression));
    const TbMemoryView *v = tb_memory_view(m, 3); uint64_t end = tb_gdb_now_ms() + 5000;
    while ((!v->ready || v->pending) && tb_gdb_now_ms() < end) { pump(); }
    if (!v->ready || v->readable != 4) { fprintf(stderr, "Explicit RAM read: %s\n", v->error); CHECK(false); }
    return (uint32_t)v->bytes[0] | (uint32_t)v->bytes[1] << 8U | (uint32_t)v->bytes[2] << 16U | (uint32_t)v->bytes[3] << 24U;
}
static void settle(TbSetupRestore *r, TbSetupCapture *c)
{
    uint64_t end = tb_gdb_now_ms() + 12000;
    while (tb_setup_restore_active(r) && tb_gdb_now_ms() < end) { pump(); tb_setup_restore_pump(r, c, session); }
    if (tb_setup_restore_active(r)) { fprintf(stderr, "Restoration: %s\n", tb_setup_restore_status(r)); dump(); CHECK(false); }
    idle();
}
static void no_values(int pane)
{
    const TbMemoryView *m = tb_memory_view(tb_session_memory(session), pane);
    CHECK(!m->configured && !m->ready && !m->pending);
    const TbVariable *v = tb_variables_row(tb_session_variables(session), TB_VARIABLE_WATCHES, 0);
    CHECK(v && strstr(v->value, "Read required"));
}
static void arm_core(void)
{
    const char *step_location = getenv("TB_SETUP_BOARD_STEP_LOCATION"); CHECK(step_location && *step_location);
    TbBreakpoints *b = tb_session_breakpoints(session); CHECK(tb_break_add(b, step_location, true)); idle();
    uint64_t entry_stop = tb_context_snapshot(tb_session_context(session))->generation.stop;
    CHECK(tb_session_continue(session)); stopped(entry_stop); idle(); char entry_id[32];
    (void)snprintf(entry_id, sizeof entry_id, "%s", tb_break_row(b, 0)->id); CHECK(tb_break_remove(b, entry_id)); idle();
    TbCode *code = tb_session_code(session); CHECK(tb_code_tab_select(code, 0, 0)); tb_code_visible(code, 0, true, true);
    const TbCodeView *v = tb_code_view(code, 0); uint64_t end = tb_gdb_now_ms() + 6000;
    while ((v->stale || !v->instruction_count || !v->line_count) && tb_gdb_now_ms() < end) { pump(); }
    CHECK(!v->stale && v->instruction_count && v->line_count);
    uint64_t pc = strtoull(tb_context_snapshot(tb_session_context(session))->stopped.address, NULL, 0); bool decoded = false;
    for (size_t i = 0; i < v->instruction_count; ++i) {
        const TbInstruction *in = &v->instructions[i];
        if (in->numeric == pc) { decoded = in->bytes && *in->opcodes && *in->text && *in->text != '.' && !strstr(in->text, "(bad)"); }
    }
    CHECK(decoded);
    TbRegisters *r = tb_session_registers(session); tb_registers_visible(r, true); bool have_pc = false, have_sp = false;
    end = tb_gdb_now_ms() + 5000;
    do {
        tb_registers_begin(r); tb_registers_want(r, 0, 32); pump();
        for (size_t i = 0; i < tb_registers_count(r); ++i) {
            const TbRegister *reg = tb_registers_row(r, i);
            if (!reg->available || !reg->fresh) { continue; }
            if (!strcmp(reg->name, "pc")) { have_pc = strtoull(reg->value, NULL, 0) == pc; }
            if (!strcmp(reg->name, "sp")) { have_sp = strtoull(reg->value, NULL, 0) != 0; }
        }
    } while ((!have_pc || !have_sp) && tb_gdb_now_ms() < end);
    CHECK(have_pc && have_sp);
    uint64_t before = tb_context_snapshot(tb_session_context(session))->generation.stop;
    CHECK(tb_session_step(session, TB_STEP_INTO, false)); stopped(before);
    before = tb_context_snapshot(tb_session_context(session))->generation.stop;
    CHECK(tb_session_step(session, TB_STEP_INTO, true)); stopped(before);
    CHECK(tb_break_add(b, code_location, true)); idle(); before = tb_context_snapshot(tb_session_context(session))->generation.stop;
    CHECK(tb_session_continue(session)); stopped(before); idle(); char scope_break[32];
    (void)snprintf(scope_break, sizeof scope_break, "%s", tb_break_row(b, 0)->id); CHECK(tb_break_remove(b, scope_break)); idle();
    CHECK(tb_break_watch(b, watch_expression, TB_WATCH_CHANGE)); idle(); char id[32] = {0};
    for (size_t i = 0; i < tb_break_count(b); ++i) { const TbBreakpoint *bp = tb_break_row(b, i); if (bp->watchpoint) { CHECK(bp->hardware); (void)snprintf(id, sizeof id, "%s", bp->id); } }
    CHECK(*id); before = tb_context_snapshot(tb_session_context(session))->generation.stop;
    CHECK(tb_session_continue(session)); stopped(before); idle(); bool triggered = false;
    for (size_t i = 0; i < tb_session_log_count(session); ++i) { triggered = triggered || strstr(tb_session_log_line(session, i), "watchpoint-trigger"); }
    CHECK(triggered && tb_break_remove(b, id)); idle();
    uint32_t old = read_word(memory_expression); uint64_t address = tb_memory_view(tb_session_memory(session), 3)->address;
    char literal[48]; (void)snprintf(literal, sizeof literal, "0x%llx", (unsigned long long)address);
    disconnect(true); CHECK(poll(NULL, 0, 350) == 0); start(); stopped(0);
    uint32_t advanced = read_word(literal); CHECK(advanced != old);
    disconnect(false); CHECK(poll(NULL, 0, 200) == 0); start(); stopped(0); CHECK(read_word(literal) == advanced);
    before = tb_context_snapshot(tb_session_context(session))->generation.stop;
    CHECK(tb_session_reset_halt(session)); stopped(before);
    printf("PASS ARM existing-image decoded PC/registers, source/instruction steps, explicit SRAM, hardware local watchpoint, resume/leave-halted verification and reset\n");
}
int main(int argc, char **argv)
{
    CHECK(argc == 9 && atexit(cleanup) == 0); (void)setvbuf(stdout, NULL, _IONBF, 0); CHECK(mkdtemp(settings));
    CHECK(setenv("XDG_CONFIG_HOME", settings, 1) == 0);
    source = argv[7]; code_location = argv[5]; watch_expression = argv[6]; memory_expression = argv[8];
    plan = calloc(1, sizeof *plan); CHECK(plan); (void)snprintf(plan->endpoint, sizeof plan->endpoint, "%s", argv[2]);
    config = (TbSessionConfig){.kind = TB_SESSION_REMOTE, .endpoint = argv[2], .gdb = argv[1], .executable = argv[3], .openocd = plan};
    start(); stopped(0); idle(); CHECK(strstr(tb_session_capabilities(session)->architecture, argv[4]));
    CHECK(!tb_session_capabilities(session)->server_owned && tb_session_target_pid(session) == 0);
    TbLayout layout; tb_layout_init(&layout); TbDemo workspace; tb_demo_init(&workspace);
    const char *expressions[TB_MAX_PANES]; for (int i = 0; i < TB_MAX_PANES; ++i) { expressions[i] = ""; }
    TbSetupCapture *c = tb_setup_capture_create(NULL); CHECK(c);
#define CAPTURE() tb_setup_capture_update(c, &layout, &workspace, tb_session_code(session), tb_session_memory(session), tb_session_variables(session), tb_session_breakpoints(session), expressions)
    CHECK(tb_layout_arrange(&layout, (TbRect){0, 0, 240, 100}, 0)); int pane = tb_layout_split(&layout, 0, TB_SPLIT_RIGHT); CHECK(pane >= 0);
    workspace.panes[pane] = (TbDemoPane){.tabs = {TB_MEMORY, TB_WATCHES}, .count = 2, .active = 1};
    expressions[pane] = memory_expression;
    CHECK(tb_memory_definition(tb_session_memory(session), pane, 4, 4));
    CHECK(tb_variables_watch(tb_session_variables(session), watch_expression));
    CHECK(CAPTURE()); TbSetup *saved = malloc(sizeof *saved); CHECK(saved); *saved = *tb_setup_capture_current(c);
    saved->panes[0].source_count = 2;
    saved->panes[0].sources[0].id = saved->next_id++; (void)snprintf(saved->panes[0].sources[0].file, TB_SETUP_PATH, "%s", source); (void)snprintf(saved->panes[0].sources[0].full, TB_SETUP_PATH, "%s", source);
    saved->panes[0].sources[1] = (TbSetupSource){.id = saved->next_id++, .file = "missing-f01-board.c", .full = "/missing-f01-board.c"};
    saved->panes[0].source_selected = saved->panes[0].sources[1].id;
    saved->break_count = 3;
    saved->breaks[0] = (TbSetupBreak){.id = saved->next_id++, .enabled = true, .hardware = true, .condition = "1", .ignore_set = true, .ignore = 2};
    (void)snprintf(saved->breaks[0].location, TB_SETUP_LOCATION, "%s", argv[5]);
    saved->breaks[1] = saved->breaks[0]; saved->breaks[1].id = saved->next_id++; (void)snprintf(saved->breaks[1].condition, 512, "(");
    saved->breaks[2] = (TbSetupBreak){.id = saved->next_id++, .enabled = false, .hardware = true};
    (void)snprintf(saved->breaks[2].location, TB_SETUP_LOCATION, "*%s", tb_context_snapshot(tb_session_context(session))->stopped.address);
    CHECK(tb_setup_valid(saved));
    TbProfiles *store = tb_profiles_create(); CHECK(store && tb_profiles_load(store)); TbProfile profile; tb_profile_defaults(&profile, 1);
    profile.start_server = false; (void)snprintf(profile.name, TB_PROFILE_NAME, "physical-f01"); (void)snprintf(profile.executable, TB_PROFILE_PATH, "%s", argv[3]); (void)snprintf(profile.gdb, TB_PROFILE_PATH, "%s", argv[1]); (void)snprintf(profile.endpoint, sizeof profile.endpoint, "%s", argv[2]);
    char directory[TB_SETUP_PATH]; CHECK(getcwd(directory, sizeof directory));
    CHECK(tb_profiles_put(store, &profile, false) && tb_profiles_save_setup(store, profile.name, saved, &profile, directory));
    tb_setup_capture_destroy(c); close_session(); CHECK(!session);
    CHECK(tb_profiles_load(store)); free(saved); saved = tb_profiles_setup(store, profile.name); CHECK(saved);
    start(); c = tb_setup_capture_create(saved); CHECK(c);
    CHECK(tb_setup_capture_restore(c, saved, &layout, &workspace, tb_session_code(session), tb_session_memory(session), tb_session_variables(session)));
    CHECK(!layout.lower_open[0] && layout.count == 7); no_values(pane);
    TbSetupRestore *restore = tb_setup_restore_create(); CHECK(restore); tb_setup_restore_begin(restore); tb_session_setup_interlock(session, true, false);
    CHECK(!tb_session_continue(session) && !tb_session_console(session, "show version")); settle(restore, c);
    TbBreakpoints *b = tb_session_breakpoints(session);
    const TbBreakRestoration *good = tb_break_restoration(b, saved->breaks[0].id), *bad = tb_break_restoration(b, saved->breaks[1].id), *address = tb_break_restoration(b, saved->breaks[2].id);
    CHECK(good && good->state == TB_RESTORE_INSTALLED && bad && bad->state == TB_RESTORE_UNRESOLVED && address && address->state == TB_RESTORE_ADDRESS);
    char installed[32], partial[32]; (void)snprintf(installed, sizeof installed, "%s", good->backend); (void)snprintf(partial, sizeof partial, "%s", bad->backend);
    bool disabled_failure = false; for (size_t i = 0; i < tb_break_count(b); ++i) { const TbBreakpoint *bp = tb_break_row(b, i); if (!strcmp(bp->id, partial)) { disabled_failure = !bp->enabled; } }
    CHECK(disabled_failure && tb_setup_capture_retained(c) == 2 && tb_setup_restore_acknowledgment(restore));
    CHECK(!tb_session_continue(session)); CHECK(CAPTURE() && !tb_setup_capture_dirty(c)); no_values(pane);
    CHECK(tb_code_tab_active(tb_session_code(session), 0) == 2 && tb_code_tab_unavailable(tb_session_code(session), 0, 2));
    tb_setup_restore_acknowledge(restore); tb_setup_restore_pump(restore, c, session); CHECK(!tb_setup_restore_acknowledgment(restore));
    CHECK(tb_break_restore(b, tb_setup_capture_retained_break(c, 0), false)); tb_setup_restore_pump(restore, c, session); settle(restore, c);
    CHECK(tb_break_count(b) == 2); /* Retry reused the same disabled partial location. */
    if (tb_setup_restore_acknowledgment(restore)) { tb_setup_restore_acknowledge(restore); tb_setup_restore_pump(restore, c, session); }
    uint64_t before = tb_context_snapshot(tb_session_context(session))->generation.stop;
    CHECK(tb_session_continue(session)); stopped(before); tb_setup_restore_pump(restore, c, session); idle();
    CHECK(tb_break_definition(b, 0)->ignore == 2 && tb_break_definition(b, 0)->ignore_set && CAPTURE() && !tb_setup_capture_dirty(c)); no_values(pane);
    CHECK(tb_break_enable(b, installed, false)); idle(); CHECK(tb_break_remove(b, partial)); idle();
    CHECK(tb_setup_capture_remove_break(c, saved->breaks[1].id)); tb_break_restore_forget(b, saved->breaks[1].id);
    CHECK(CAPTURE() && tb_setup_capture_dirty(c));
    CHECK(tb_session_continue(session)); uint64_t end = tb_gdb_now_ms() + 3000;
    while (tb_session_state(session) != TB_SESSION_RUNNING && tb_gdb_now_ms() < end) { pump(); }
    CHECK(tb_session_state(session) == TB_SESSION_RUNNING); tb_setup_restore_begin(restore); tb_setup_restore_pump(restore, c, session);
    CHECK(!tb_setup_restore_active(restore) && strstr(tb_setup_restore_status(restore), "Awaiting stop"));
    for (int i = 0; i < 20; ++i) { pump(); tb_setup_restore_pump(restore, c, session); }
    CHECK(tb_session_state(session) == TB_SESSION_RUNNING && tb_session_pause(session));
    stopped(0); tb_setup_restore_pump(restore, c, session); CHECK(tb_setup_restore_active(restore) && !tb_session_continue(session)); settle(restore, c); no_values(pane);
    TbBreakDefinition cancel = {.enabled = true, .hardware = true}; (void)snprintf(cancel.location, sizeof cancel.location, "%s", argv[5]);
    uint64_t cancel_id = tb_setup_capture_import_break(c, &cancel); CHECK(cancel_id && CAPTURE());
    const TbSetupBreak *entry = NULL; for (size_t i = 0; i < tb_setup_capture_retained(c); ++i) { const TbSetupBreak *d = tb_setup_capture_retained_break(c, i); if (d->id == cancel_id) { entry = d; } }
    CHECK(entry && tb_break_restore(b, entry, false)); tb_break_pump(b); tb_setup_restore_cancel(restore, session);
    CHECK(tb_setup_restore_active(restore) && strstr(tb_setup_restore_status(restore), "Cancel requested")); settle(restore, c);
    CHECK(strstr(tb_setup_restore_status(restore), "canceled") && tb_break_restoration(b, cancel_id)->state == TB_RESTORE_UNRESOLVED);
    tb_setup_restore_acknowledge(restore); tb_setup_restore_pump(restore, c, session);
    for (size_t i = 0; i < tb_break_count(b);) { const TbBreakpoint *bp = tb_break_row(b, i); char id[32]; (void)snprintf(id, sizeof id, "%s", bp->id); CHECK(tb_break_remove(b, id)); idle(); }
    tb_setup_restore_destroy(restore); tb_setup_capture_destroy(c); free(saved);
    if (!strcmp(argv[4], "arm")) {
        /* Stop on the reviewed local's scope before explicit ARM core checks. */
        CHECK(tb_break_add(b, argv[5], true)); idle(); before = tb_context_snapshot(tb_session_context(session))->generation.stop;
        CHECK(tb_session_continue(session)); stopped(before); idle(); char id[32]; (void)snprintf(id, sizeof id, "%s", tb_break_row(b, 0)->id); CHECK(tb_break_remove(b, id)); idle(); arm_core();
    }
    close_session(); CHECK(!session); CHECK(tb_profiles_remove(store, profile.name)); char path[TB_PROFILE_PATH * 2]; (void)snprintf(path, sizeof path, "%s", tb_profiles_path(store)); tb_profiles_destroy(store);
    CHECK(unlink(path) == 0); (void)snprintf(path, sizeof path, "%s/the-bugger/.lock", settings); CHECK(unlink(path) == 0);
    (void)snprintf(path, sizeof path, "%s/the-bugger", settings); CHECK(rmdir(path) == 0 && rmdir(settings) == 0);
    puts("PASS F01 physical saved-profile reopen, split/tab/Source identity, missing pin, no restored Memory/Watch reads, disabled-first attributes/failure, hardware policy, remote address approval, retry/removal/dirty state, running deferral and cancellation");
    return EXIT_SUCCESS;
}
