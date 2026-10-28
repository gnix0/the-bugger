#define _POSIX_C_SOURCE 200809L
#include "setup_restore.h"
#include "../support/check.h"
#include <string.h>
#include <unistd.h>

static TbSession *owned;
static void cleanup(void)
{
    if (!owned) { return; }
    tb_session_close(owned); uint64_t end = tb_gdb_now_ms() + 6000;
    while (tb_session_active(owned) && tb_gdb_now_ms() < end) { tb_session_pump(owned, 5); }
    if (!tb_session_active(owned)) { CHECK(tb_session_destroy(owned)); owned = NULL; }
}
static void wait_state(TbSessionState state)
{
    uint64_t end = tb_gdb_now_ms() + 6000;
    while (tb_gdb_now_ms() < end) {
        tb_session_pump(owned, 5);
        if (tb_session_state(owned) == state) { return; }
    }
    fprintf(stderr, "Session: %s\n", tb_session_message(owned)); CHECK(false);
}
static void settle(TbSetupRestore *r, TbSetupCapture *c)
{
    uint64_t end = tb_gdb_now_ms() + 6000;
    while (tb_setup_restore_active(r) && tb_gdb_now_ms() < end) {
        tb_session_pump(owned, 5); tb_setup_restore_pump(r, c, owned);
    }
    fprintf(stderr, "Restoration: %s\n", tb_setup_restore_status(r)); CHECK(!tb_setup_restore_active(r));
}
int main(int argc, char **argv)
{
    CHECK(argc == 3 && atexit(cleanup) == 0); owned = tb_session_create(); CHECK(owned);
    TbSessionConfig config = {.gdb = "gdb", .executable = argv[1]}; CHECK(tb_session_start(owned, &config));
    TbLayout layout; tb_layout_init(&layout); layout.overlays = true; TbDemo workspace; tb_demo_init(&workspace);
    const char *expressions[TB_MAX_PANES]; for (int i = 0; i < TB_MAX_PANES; ++i) { expressions[i] = ""; }
    TbCode *code = tb_session_code(owned); TbMemory *memory = tb_session_memory(owned); TbVariables *variables = tb_session_variables(owned);
    TbSetupCapture *c = tb_setup_capture_create(NULL); CHECK(c);
#define CAPTURE() tb_setup_capture_update(c, &layout, &workspace, code, memory, variables, tb_session_breakpoints(owned), expressions)
    CHECK(CAPTURE()); TbSetup *saved = malloc(sizeof *saved); CHECK(saved); *saved = *tb_setup_capture_current(c);
    CHECK(tb_layout_arrange(&layout, (TbRect){0, 0, 240, 100}, 0)); int added = tb_layout_split(&layout, 0, TB_SPLIT_RIGHT); CHECK(added >= 0);
    workspace.panes[added] = (TbDemoPane){.tabs = {TB_MEMORY, TB_WATCHES}, .count = 2, .active = 1};
    CHECK(CAPTURE()); *saved = *tb_setup_capture_current(c);
    saved->panes[0].source_count = 2;
    saved->panes[0].sources[0].id = saved->next_id++;
    (void)snprintf(saved->panes[0].sources[0].file, TB_SETUP_PATH, "tests/fixtures/native/session.c");
    (void)snprintf(saved->panes[0].sources[0].full, TB_SETUP_PATH, "/former/source/session.c");
    saved->panes[0].sources[1].id = saved->next_id++;
    (void)snprintf(saved->panes[0].sources[1].file, TB_SETUP_PATH, "missing-saved-source.c");
    (void)snprintf(saved->panes[0].sources[1].full, TB_SETUP_PATH, "/missing-saved-source.c");
    saved->panes[0].source_selected = saved->panes[0].sources[1].id;
    saved->panes[added].memory_bytes = 48; saved->panes[added].memory_group = 8;
    (void)snprintf(saved->panes[added].memory_expression, 256, "&fixture_buffer"); expressions[added] = saved->panes[added].memory_expression;
    saved->watch_count = 2; saved->watches[0] = (TbSetupWatch){.id = saved->next_id++, .expression = "fixture_counter", .format = TB_VARIABLE_HEX};
    saved->watches[1] = (TbSetupWatch){.id = saved->next_id++, .expression = "unsafe_call()", .format = TB_VARIABLE_DECIMAL};
    saved->break_count = 1; saved->breaks[0] = (TbSetupBreak){.id = saved->next_id++, .location = "fixture_tick", .enabled = true, .ignore_set = true, .ignore = 2};
    CHECK(tb_setup_valid(saved)); tb_setup_capture_destroy(c); c = tb_setup_capture_create(saved); CHECK(c);
    CHECK(tb_setup_capture_restore(c, saved, &layout, &workspace, code, memory, variables));
    CHECK(layout.overlays && layout.count == 7 && !layout.lower_open[0]);
    CHECK(workspace.panes[added].tabs[workspace.panes[added].active] == TB_WATCHES);
    CHECK(tb_code_tab_active(code, 0) == 2 && tb_code_tab_unavailable(code, 0, 1) && tb_code_tab_unavailable(code, 0, 2));
    CHECK(tb_code_view(code, 0)->line_count == 0 && strstr(tb_code_view(code, 0)->source_error, "Saved Source unavailable"));
    CHECK(CAPTURE() && !tb_setup_capture_dirty(c) && tb_setup_equal(saved, tb_setup_capture_current(c)));
    CHECK(!tb_memory_view(memory, added)->configured && !tb_memory_view(memory, added)->ready && !tb_memory_view(memory, added)->pending);
    CHECK(tb_memory_view(memory, added)->length == 48 && tb_memory_view(memory, added)->grouping == 8);
    CHECK(tb_variables_definitions(variables) == 2 && tb_variables_row(variables, TB_VARIABLE_WATCHES, 0)->format == TB_VARIABLE_HEX);
    TbSetupRestore *r = tb_setup_restore_create(); CHECK(r); tb_setup_restore_begin(r); tb_session_setup_interlock(owned, true, false);
    CHECK(!tb_session_continue(owned) && !tb_session_console(owned, "start"));
    CHECK(!tb_context_select_thread(tb_session_context(owned), "1"));
    CHECK(!tb_break_add(tb_session_breakpoints(owned), "main", false));
    CHECK(!tb_code_add_rule(code, "/old", "/new"));
    settle(r, c); CHECK(tb_session_state(owned) == TB_SESSION_READY);
    CHECK(!tb_code_tab_unavailable(code, 0, 1) && tb_code_tab_unavailable(code, 0, 2));
    CHECK(tb_code_tab_active(code, 0) == 2 && tb_code_view(code, 0)->line_count == 0);
    CHECK(tb_setup_restore_acknowledgment(r) && !tb_session_continue(owned) && !tb_session_console(owned, "start"));
    size_t known = 0; while (known < tb_code_file_count(code) && strcmp(tb_code_file(code, known)->full, argv[2])) { ++known; }
    CHECK(known < tb_code_file_count(code) && tb_code_open(code, 0, known));
    CHECK(tb_code_tab_count(code, 0) == 3 && !strcmp(tb_code_tab_file(code, 0, 1)->full, "/former/source/session.c"));
    CHECK(tb_code_tab_select(code, 0, 2));
    CHECK(tb_break_definition_count(tb_session_breakpoints(owned)) == 0 && tb_setup_capture_retained(c) == 1);
    CHECK(CAPTURE() && !tb_setup_capture_dirty(c));
    tb_break_definition_reserve(tb_session_breakpoints(owned), TB_SETUP_BREAKS);
    CHECK(!tb_break_add(tb_session_breakpoints(owned), "main", false));
    tb_break_definition_reserve(tb_session_breakpoints(owned), tb_setup_capture_retained(c));
    tb_setup_restore_acknowledge(r); tb_setup_restore_pump(r, c, owned); CHECK(!tb_setup_restore_acknowledgment(r));
    CHECK(tb_session_console(owned, "start")); wait_state(TB_SESSION_STOPPED);
    CHECK(!tb_variables_read(variables, TB_VARIABLE_WATCHES, 1));
    CHECK(!tb_memory_view(memory, added)->configured && !tb_memory_view(memory, added)->pending);
    tb_setup_restore_pump(r, c, owned); CHECK(!tb_setup_restore_acknowledgment(r));
    CHECK(tb_code_tab_close(code, 0, 2) && tb_setup_capture_remove_break(c, saved->breaks[0].id));
    CHECK(CAPTURE() && tb_setup_capture_dirty(c) && tb_setup_capture_current(c)->break_count == 0);
    CHECK(tb_setup_capture_current(c)->panes[0].sources[0].id == saved->panes[0].sources[0].id);
    TbWatchDefinition unsupported; CHECK(tb_variables_definition(variables, 1, &unsupported));
    CHECK(tb_variables_remove_definition(variables, unsupported.identity));
    CHECK(CAPTURE() && tb_setup_capture_current(c)->watch_count == 1 && tb_setup_capture_current(c)->watches[0].id == saved->watches[0].id);
    CHECK(tb_setup_capture_saved(c)); CHECK(CAPTURE() && !tb_setup_capture_dirty(c));
    /* An actual request remains in flight until the next controller pump.
     * Cancel must keep the interlock until that completion is consumed. */
    tb_setup_restore_begin(r); tb_code_request_files(code);
    uint64_t end = tb_gdb_now_ms() + 4000;
    while (!tb_code_catalog_pending(code) && tb_gdb_now_ms() < end) { tb_session_pump(owned, 5); }
    CHECK(tb_code_catalog_pending(code)); tb_setup_restore_cancel(r, owned);
    CHECK(tb_setup_restore_active(r) && strstr(tb_setup_restore_status(r), "Cancel requested"));
    CHECK(!tb_session_console(owned, "show version")); settle(r, c);
    CHECK(strstr(tb_setup_restore_status(r), "canceled"));
    /* Reconnection can leave a target running. Local definitions must not halt
     * it; an explicit Pause is still usable while restoration awaits a stop. */
    CHECK(tb_session_continue(owned)); wait_state(TB_SESSION_RUNNING);
    tb_setup_restore_begin(r); tb_setup_restore_pump(r, c, owned);
    CHECK(!tb_setup_restore_active(r) && strstr(tb_setup_restore_status(r), "Awaiting stop"));
    for (int i = 0; i < 20; ++i) { tb_session_pump(owned, 5); tb_setup_restore_pump(r, c, owned); }
    CHECK(tb_session_state(owned) == TB_SESSION_RUNNING && tb_session_pause(owned)); wait_state(TB_SESSION_STOPPED);
    tb_setup_restore_pump(r, c, owned); CHECK(tb_setup_restore_active(r));
    CHECK(!tb_session_continue(owned)); settle(r, c);
    tb_setup_restore_destroy(r); tb_setup_capture_destroy(c); free(saved); cleanup();
    puts("PASS F01 local/split/tab/Source identity restoration, missing files, retained breakpoint intent, no value permissions, partial saves, interlocks, acknowledgment, in-flight cancellation and running deferral");
    return EXIT_SUCCESS;
}
