#include "native.h"
#include "../support/check.h"
#include <string.h>

static void pump(TbNative *n)
{
    TbVariables *v = tb_native_variables(n);
    tb_variables_begin(v); tb_variables_want(v, TB_VARIABLE_LOCALS, 0, TB_VARIABLE_NODES);
    tb_variables_want(v, TB_VARIABLE_WATCHES, 0, TB_VARIABLE_NODES); tb_native_pump(n, 2);
}
static size_t find(TbVariables *v, TbVariableView view, const char *name)
{
    for (size_t i = 0; i < tb_variables_count(v, view); ++i) {
        if (strcmp(tb_variables_row(v, view, i)->name, name) == 0) { return i; }
    }
    return SIZE_MAX;
}
static void stop(TbNative *n, uint64_t after)
{
    uint64_t end = tb_gdb_now_ms() + 5000;
    const TbContextSnapshot *s = tb_context_snapshot(tb_native_context(n));
    while (tb_gdb_now_ms() < end) {
        pump(n);
        if (tb_native_state(n) == TB_NATIVE_STOPPED && s->ready && !s->stale && s->generation.stop > after &&
            find(tb_native_variables(n), TB_VARIABLE_LOCALS, "stage") != SIZE_MAX) { return; }
    }
    fprintf(stderr, "variables stop: %s / %s\n", tb_native_message(n), tb_variables_error(tb_native_variables(n), TB_VARIABLE_LOCALS)); CHECK(false);
}
static void read_row(TbNative *n, TbVariableView view, size_t row)
{
    TbVariables *v = tb_native_variables(n); uint64_t end = tb_gdb_now_ms() + 5000;
    bool requested = false;
    while (tb_gdb_now_ms() < end) {
        if (!requested) { requested = tb_variables_read(v, view, row); }
        pump(n);
        const TbVariable *value = tb_variables_row(v, view, row);
        if (requested && value && value->fresh) { return; }
        if (requested && *tb_variables_error(v, view)) { break; }
    }
    fprintf(stderr, "variables read: %s\n", tb_variables_error(v, view)); CHECK(false);
}
static void expand(TbNative *n, size_t row)
{
    TbVariables *v = tb_native_variables(n); uint64_t end = tb_gdb_now_ms() + 5000;
    CHECK(tb_variables_toggle(v, TB_VARIABLE_WATCHES, row));
    while (tb_gdb_now_ms() < end) {
        pump(n); if (tb_variables_row(v, TB_VARIABLE_WATCHES, row)->expanded) { return; }
        if (*tb_variables_error(v, TB_VARIABLE_WATCHES)) { break; }
    }
    fprintf(stderr, "variables children: %s\n", tb_variables_error(v, TB_VARIABLE_WATCHES)); CHECK(false);
}
static void expand_members(TbNative *n, size_t root)
{
    TbVariables *v = tb_native_variables(n);
    unsigned depth = tb_variables_row(v, TB_VARIABLE_WATCHES, root)->depth;
    expand(n, root);
    /* GDB may place C++ fields below typeless access-group pseudochildren.
     * Expanding those is a separate explicit inspection, just as in the UI. */
    for (size_t row = root + 1; row < tb_variables_count(v, TB_VARIABLE_WATCHES); ++row) {
        const TbVariable *child = tb_variables_row(v, TB_VARIABLE_WATCHES, row);
        if (child->depth <= depth) { break; }
        if (child->depth != depth + 1 || *child->type || !child->children || child->expanded) { continue; }
        if (!strcmp(child->name, "public") || !strcmp(child->name, "protected") || !strcmp(child->name, "private")) {
            expand(n, row);
        }
    }
}
static void require_member(TbNative *n, const char *name)
{
    TbVariables *v = tb_native_variables(n);
    if (find(v, TB_VARIABLE_WATCHES, name) == SIZE_MAX) {
        fprintf(stderr, "Missing member %s; backend error: %s\n", name, tb_variables_error(v, TB_VARIABLE_WATCHES));
        for (size_t i = 0; i < tb_variables_count(v, TB_VARIABLE_WATCHES) && i < 16; ++i) {
            const TbVariable *row = tb_variables_row(v, TB_VARIABLE_WATCHES, i);
            fprintf(stderr, "  row %zu depth %u: %s | %s | children %u | %s\n", i, row->depth, row->name, row->type, row->children, row->value);
        }
        CHECK(false);
    }
}
int main(int argc, char **argv)
{
    CHECK(argc >= 2);
    for (int fixture = 1; fixture < argc; ++fixture) {
        fprintf(stderr, "M11 native fixture: %s\n", argv[fixture]);
        bool optimized = strstr(argv[fixture], "optimized") != NULL;
        TbNative *n = tb_native_create(); CHECK(n);
        TbVariables *v = tb_native_variables(n); tb_variables_visible(v, true, true);
        TbNativeConfig config = {.gdb = "gdb", .executable = argv[fixture]}; CHECK(tb_native_start(n, &config));
        uint64_t end = tb_gdb_now_ms() + 5000;
        while (tb_native_state(n) != TB_NATIVE_READY && tb_gdb_now_ms() < end) { pump(n); }
        CHECK(tb_native_state(n) == TB_NATIVE_READY); tb_variables_visible(v, true, true);
        CHECK(tb_native_console(n, "break variables_ready"));
        /* Console completion is asynchronous; wait for native readiness. */
        end = tb_gdb_now_ms() + 5000;
        bool sent = false;
        while (!sent && tb_gdb_now_ms() < end) { pump(n); sent = tb_native_console(n, "run"); }
        CHECK(sent); stop(n, 0);
        const TbContextSnapshot *s = tb_context_snapshot(tb_native_context(n));
        size_t stage = find(v, TB_VARIABLE_LOCALS, "stage");
        CHECK(stage != SIZE_MAX && !tb_variables_row(v, TB_VARIABLE_LOCALS, stage)->fresh);
        read_row(n, TB_VARIABLE_LOCALS, stage);
        CHECK(strstr(tb_variables_row(v, TB_VARIABLE_LOCALS, stage)->type, "int") ||
              (optimized && !tb_variables_row(v, TB_VARIABLE_LOCALS, stage)->available));
        CHECK(tb_variables_watch(v, "variables_global")); read_row(n, TB_VARIABLE_WATCHES, 0);
        if (!optimized) { CHECK(strcmp(tb_variables_row(v, TB_VARIABLE_WATCHES, 0)->value, "17") == 0); }
        CHECK(tb_variables_watch(v, "variables_sample")); read_row(n, TB_VARIABLE_WATCHES, 1); expand_members(n, 1);
        require_member(n, "count"); require_member(n, "values");
        size_t values = find(v, TB_VARIABLE_WATCHES, "values"); CHECK(values != SIZE_MAX); expand(n, values);
        CHECK(tb_variables_count(v, TB_VARIABLE_WATCHES) <= TB_VARIABLE_NODES);
        CHECK(tb_variables_page(v, TB_VARIABLE_WATCHES, values, 1));
        end = tb_gdb_now_ms() + 5000;
        while (find(v, TB_VARIABLE_WATCHES, "32") == SIZE_MAX && tb_gdb_now_ms() < end) { pump(n); }
        CHECK(find(v, TB_VARIABLE_WATCHES, "32") != SIZE_MAX);
        uint64_t before = s->generation.stop; CHECK(tb_native_continue(n)); stop(n, before);
        CHECK(tb_variables_stale(v, TB_VARIABLE_WATCHES));
        CHECK(!tb_variables_row(v, TB_VARIABLE_WATCHES, 0)->fresh); read_row(n, TB_VARIABLE_WATCHES, 0);
        if (!optimized) {
            CHECK(strcmp(tb_variables_row(v, TB_VARIABLE_WATCHES, 0)->value, "20") == 0);
            CHECK(tb_variables_row(v, TB_VARIABLE_WATCHES, 0)->changed);
        }
        CHECK(!tb_variables_watch(v, "variables_ready(0)")); CHECK(!tb_variables_watch(v, "variables_global=1"));
        tb_context_visible(tb_native_context(n), false, true); end = tb_gdb_now_ms() + 5000;
        while ((s->stale || s->frame_count < 2) && tb_gdb_now_ms() < end) { pump(n); }
        CHECK(s->frame_count >= 2 && !s->stale); CHECK(tb_context_select_frame(tb_native_context(n), 1));
        end = tb_gdb_now_ms() + 5000;
        while ((s->stale || (!optimized && find(v, TB_VARIABLE_LOCALS, "scalar") == SIZE_MAX)) && tb_gdb_now_ms() < end) { pump(n); }
        size_t scalar = find(v, TB_VARIABLE_LOCALS, "scalar");
        CHECK(!s->stale && (optimized || scalar != SIZE_MAX));
        if (scalar != SIZE_MAX) { read_row(n, TB_VARIABLE_LOCALS, scalar); CHECK(!tb_variables_row(v, TB_VARIABLE_LOCALS, scalar)->changed); }
        if (strstr(argv[fixture], "cpp")) {
            CHECK(tb_variables_watch(v, "object")); size_t object = find(v, TB_VARIABLE_WATCHES, "object"); CHECK(object != SIZE_MAX);
            read_row(n, TB_VARIABLE_WATCHES, object); expand_members(n, object);
            require_member(n, "field");
        }
        tb_native_close(n); end = tb_gdb_now_ms() + 6000;
        while (tb_native_active(n) && tb_gdb_now_ms() < end) { pump(n); }
        CHECK(!tb_native_active(n) && tb_native_destroy(n));
    }
    puts("PASS M11 actual GDB C/C++ Locals/Watches, explicit reads, aggregate/array paging, changed values, context reset and cleanup");
    return EXIT_SUCCESS;
}
