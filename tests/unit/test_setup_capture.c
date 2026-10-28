#include "setup_capture.h"
#include "profiles.h"
#include "../support/check.h"
#include <string.h>
static void event(void *user, const TbGdbEvent *record) { (void)user; (void)record; }

int main(void)
{
    TbGdb *gdb = tb_gdb_create(event, NULL); CHECK(gdb);
    TbContext *context = tb_context_create(gdb); CHECK(context);
    TbCode *code = tb_code_create(gdb, context); TbMemory *memory = tb_memory_create(gdb, context);
    TbVariables *variables = tb_variables_create(gdb, context); TbBreakpoints *breaks = tb_break_create(gdb);
    CHECK(code && memory && variables && breaks);
    TbLayout layout; tb_layout_init(&layout); TbDemo workspace; tb_demo_init(&workspace);
    const char *expressions[TB_MAX_PANES]; for (size_t i = 0; i < TB_MAX_PANES; ++i) { expressions[i] = ""; }
    TbSetupCapture *c = tb_setup_capture_create(NULL); CHECK(c);
#define CAPTURE() tb_setup_capture_update(c, &layout, &workspace, code, memory, variables, breaks, expressions)
    CHECK(CAPTURE() && !tb_setup_capture_dirty(c));
    uint64_t original = tb_setup_capture_current(c)->panes[0].id;
    layout.overlays = true; layout.lower_open[0] = true; layout.lower_height[0] = 31;
    workspace.pane = 3; workspace.panes[0].scroll[TB_SOURCE] = 25;
    CHECK(CAPTURE() && !tb_setup_capture_dirty(c));
    expressions[3] = "&mmio_word"; CHECK(CAPTURE() && tb_setup_capture_dirty(c));
    CHECK(strcmp(tb_setup_capture_current(c)->panes[3].memory_expression, "&mmio_word") == 0);
    CHECK(!tb_memory_view(memory, 3)->ready && !tb_memory_view(memory, 3)->pending);
    CHECK(tb_variables_watch(variables, "watched.value")); CHECK(CAPTURE());
    const TbSetup *s = tb_setup_capture_current(c); CHECK(s->watch_count == 1);
    uint64_t watch_id = s->watches[0].id; CHECK(s->panes[0].id == original);
    CHECK(strcmp(tb_variables_row(variables, TB_VARIABLE_WATCHES, 0)->value, "Read required") == 0);
    CHECK(tb_setup_capture_saved(c) && !tb_setup_capture_dirty(c));
    CHECK(CAPTURE() && s->watch_count == 1 && !tb_setup_capture_dirty(c));
    CHECK(tb_variables_watch(variables, "another")); CHECK(CAPTURE());
    uint64_t retained = tb_setup_capture_current(c)->watches[1].id;
    CHECK(tb_variables_remove(variables, 0)); CHECK(CAPTURE());
    CHECK(tb_setup_capture_current(c)->watches[0].id == retained);
    CHECK(tb_variables_watch(variables, "watched.value")); CHECK(CAPTURE());
    CHECK(tb_setup_capture_current(c)->watches[1].id != watch_id);
    CHECK(tb_layout_arrange(&layout, (TbRect){0, 0, 250, 100}, 0));
    int added = tb_layout_split(&layout, 0, TB_SPLIT_RIGHT); CHECK(added >= 0);
    workspace.panes[added] = (TbDemoPane){.tabs = {TB_MEMORY}, .count = 1};
    CHECK(CAPTURE()); uint64_t pane_id = tb_setup_capture_current(c)->panes[added].id;
    CHECK(tb_layout_close(&layout, added) >= 0);
    CHECK(tb_layout_split(&layout, 0, TB_SPLIT_RIGHT) == added);
    CHECK(CAPTURE() && tb_setup_capture_current(c)->panes[added].id != pane_id);
    CHECK(tb_setup_capture_current(c)->panes[0].id == original);
    TbSetupCapture *skipped = tb_setup_capture_create(tb_setup_capture_current(c)); CHECK(skipped);
    while (tb_variables_definitions(variables)) { CHECK(tb_variables_remove(variables, 0)); }
    CHECK(tb_setup_capture_update(skipped, &layout, &workspace, code, memory, variables, breaks, expressions));
    CHECK(tb_setup_capture_current(skipped)->watch_count == 0 && tb_setup_capture_dirty(skipped));
    tb_setup_capture_destroy(skipped);
    char oversized[257]; memset(oversized, 'x', sizeof oversized - 1); oversized[sizeof oversized - 1] = 0;
    expressions[3] = oversized; CHECK(!CAPTURE());
    CHECK(strcmp(tb_setup_capture_current(c)->panes[3].memory_expression, "&mmio_word") == 0);
    CHECK(tb_gdb_state(gdb) == TB_GDB_DISCONNECTED);
    TbProfile *a = malloc(sizeof *a), *b = malloc(sizeof *b); CHECK(a && b);
    tb_profile_defaults(a, 0); *b = *a;
    (void)snprintf(b->name, sizeof b->name, "cosmetic name"); (void)snprintf(b->endpoint, sizeof b->endpoint, "inactive");
    CHECK(!tb_profile_differences(a, b));
    (void)snprintf(b->gdb, sizeof b->gdb, "another-gdb"); CHECK(tb_profile_differences(a, b) == TB_PROFILE_TOOLS);
    tb_profile_defaults(a, 1); *b = *a;
    (void)snprintf(b->adapter, sizeof b->adapter, "inactive.cfg"); CHECK(!tb_profile_differences(a, b));
    b->scripts.count = 1; (void)snprintf(b->scripts.values[0], TB_PROFILE_WORD, "board.cfg"); CHECK(tb_profile_differences(a, b) == TB_PROFILE_SCRIPTS);
    a->start_server = b->start_server = false; CHECK(!tb_profile_differences(a, b));
    (void)snprintf(b->endpoint, sizeof b->endpoint, "127.0.0.1:3333"); CHECK(tb_profile_differences(a, b) == TB_PROFILE_ENDPOINT);
    free(a); free(b); tb_setup_capture_destroy(c); tb_break_destroy(breaks); tb_variables_destroy(variables);
    tb_memory_destroy(memory); tb_code_destroy(code); tb_context_destroy(context); CHECK(tb_gdb_destroy(gdb));
    puts("PASS setup capture identities, removed/reused entries, transient exclusions, skipped definitions, bounds, active connection differences and zero target reads");
    return EXIT_SUCCESS;
}
