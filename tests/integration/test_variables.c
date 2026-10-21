#define _POSIX_C_SOURCE 200809L
#include "variables.h"
#include "../support/check.h"
#include <string.h>

static TbContext *context;
static TbVariables *variables;
static TbMemory *memory;
static unsigned replies, reads, children, deletes;
static uint64_t proof_address = 0x1000;
static bool proof_complete;
static bool proof(void *unused, TbGdbGeneration generation, const char *expression,
                  TbVariableRead operation, unsigned first, unsigned count, TbVariableAccess *access)
{
    (void)unused; (void)generation; (void)first; (void)count;
    if (!proof_complete || strcmp(expression, "scalar") || operation != TB_VARIABLE_CREATE) { return false; }
    access->count = 1; access->spans[0].address = proof_address; access->spans[0].bytes = 4;
    return true;
}
static unsigned field(const TbMiRecord *record, const char *name)
{
    const TbMiValue *value = tb_mi_find(record->results, name);
    CHECK(value && value->type == TB_MI_STRING); return (unsigned)strtoul(value->string.data, NULL, 10);
}
static void emit(void *unused, const TbGdbEvent *event)
{
    (void)unused;
    /* Use the native owner's routing order, including the M10 module. */
    if (!tb_memory_event(memory, event) && !tb_variables_event(variables, event)) { (void)tb_context_event(context, event); }
    if (event->kind == TB_GDB_REPLY) {
        ++replies;
        if (event->cookie == 998) {
            reads = field(event->record, "reads"); children = field(event->record, "children"); deletes = field(event->record, "deletes");
        }
    }
}
static void pump(TbGdb *gdb)
{
    tb_variables_begin(variables);
    tb_variables_want(variables, TB_VARIABLE_LOCALS, 0, TB_VARIABLE_NODES);
    tb_variables_want(variables, TB_VARIABLE_WATCHES, 0, TB_VARIABLE_NODES);
    tb_gdb_pump(gdb, 1); tb_context_pump(context); tb_variables_pump(variables);
}
static void drain(TbGdb *gdb)
{
    unsigned stable = 0; uint64_t end = tb_gdb_now_ms() + 1500;
    while (stable < 4 && tb_gdb_now_ms() < end) {
        uint64_t revision = tb_variables_revision(variables) + tb_context_revision(context);
        pump(gdb);
        if (!tb_gdb_pending(gdb) && revision == tb_variables_revision(variables) + tb_context_revision(context)) { ++stable; }
        else { stable = 0; }
    }
    CHECK(stable == 4);
}
static void command_arg(TbGdb *gdb, const char *op, const char *arg, uint64_t cookie)
{
    const char *args[] = {arg};
    unsigned before = replies; CHECK(tb_gdb_request(gdb, op, args, arg ? 1 : 0, TB_GDB_SESSION, cookie, 1000, NULL));
    uint64_t end = tb_gdb_now_ms() + 1500;
    while (replies == before && tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(replies > before); drain(gdb);
}
static void command(TbGdb *gdb, const char *op, uint64_t cookie) { command_arg(gdb, op, NULL, cookie); }
static size_t find(TbVariableView view, const char *name)
{
    for (size_t row = 0; row < tb_variables_count(variables, view); ++row) {
        if (strcmp(tb_variables_row(variables, view, row)->name, name) == 0) { return row; }
    }
    CHECK(false); return 0;
}
int main(int argc, char **argv)
{
    CHECK(argc == 2); CHECK(setenv("TB_FAKE_MODE", "context-variables", 1) == 0);
    TbGdb *gdb = tb_gdb_create(emit, NULL); CHECK(gdb);
    context = tb_context_create(gdb); variables = tb_variables_create(gdb, context); memory = tb_memory_create(gdb, context); CHECK(context && variables && memory);
    CHECK(tb_gdb_start(gdb, argv[1], 1000));
    uint64_t end = tb_gdb_now_ms() + 1500;
    while (tb_gdb_state(gdb) != TB_GDB_READY && tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(tb_gdb_state(gdb) == TB_GDB_READY);
    tb_variables_visible(variables, true, true); command(gdb, "-stop", 999);
    CHECK(tb_variables_count(variables, TB_VARIABLE_LOCALS) == 4);
    command(gdb, "-variable-stats", 998); CHECK(reads == 0 && children == 0);
    tb_variables_authorizer(variables, proof, NULL);
    const TbVariableRegion safe[] = {{0x1000, 0x2000, TB_VARIABLE_ORDINARY}};
    tb_variables_regions(variables, safe, 1); drain(gdb);
    command(gdb, "-variable-stats", 998); CHECK(reads == 0); /* regions alone are not an expression proof */
    proof_complete = true; proof_address = 0x40000000; drain(gdb);
    command(gdb, "-variable-stats", 998); CHECK(reads == 0); /* unknown, not unsafe */
    proof_address = UINT64_MAX; drain(gdb); command(gdb, "-variable-stats", 998); CHECK(reads == 0);
    proof_address = 0x1ffe; drain(gdb); command(gdb, "-variable-stats", 998); CHECK(reads == 0); /* entire access, not base only */
    const TbVariableRegion overlay[] = {{0x1000, 0x2000, TB_VARIABLE_ORDINARY}, {0x1002, 0x1004, TB_VARIABLE_SIDE_EFFECT}};
    proof_address = 0x1000; tb_variables_regions(variables, overlay, 2); drain(gdb);
    command(gdb, "-variable-stats", 998); CHECK(reads == 0); /* deny takes precedence, including partial overlap */
    tb_variables_regions(variables, safe, 1); drain(gdb);
    command(gdb, "-variable-stats", 998); CHECK(reads == 1 && children == 0);
    CHECK(tb_variables_row(variables, TB_VARIABLE_LOCALS, find(TB_VARIABLE_LOCALS, "scalar"))->fresh);
    size_t scalar = find(TB_VARIABLE_LOCALS, "scalar");
    CHECK(tb_variables_format(variables, TB_VARIABLE_LOCALS, scalar)); drain(gdb);
    CHECK(!tb_variables_row(variables, TB_VARIABLE_LOCALS, scalar)->format_pending);
    command_arg(gdb, "-variable-value", "20", 999);
    command(gdb, "-variable-stats", 998); unsigned formatted_reads = reads;
    /* Native Continue calls this before sending the execution request. Context
     * and stop generation deltas therefore intentionally differ. */
    tb_context_refresh(context);
    command(gdb, "-run", 999); command(gdb, "-stop", 999);
    command(gdb, "-variable-stats", 998); CHECK(reads == formatted_reads + 1);
    CHECK(strcmp(tb_variables_row(variables, TB_VARIABLE_LOCALS, scalar)->value, "20") == 0);
    CHECK(tb_variables_row(variables, TB_VARIABLE_LOCALS, scalar)->changed);
    CHECK(tb_variables_row(variables, TB_VARIABLE_LOCALS, scalar)->format_pending); /* CREATE authorization is not EVALUATE authorization */
    tb_context_refresh(context); drain(gdb);
    CHECK(tb_variables_row(variables, TB_VARIABLE_LOCALS, scalar)->changed); /* same-stop refresh retains the prior-stop baseline */
    proof_complete = false;
    size_t aggregate = find(TB_VARIABLE_LOCALS, "aggregate");
    CHECK(tb_variables_toggle(variables, TB_VARIABLE_LOCALS, aggregate)); drain(gdb);
    CHECK(tb_variables_count(variables, TB_VARIABLE_LOCALS) == 36);
    CHECK(tb_variables_row(variables, TB_VARIABLE_LOCALS, aggregate)->expanded);
    CHECK(tb_variables_page(variables, TB_VARIABLE_LOCALS, aggregate, 1)); drain(gdb);
    CHECK(tb_variables_row(variables, TB_VARIABLE_LOCALS, aggregate)->first == 32);
    CHECK(find(TB_VARIABLE_LOCALS, "32") < 36);
    CHECK(tb_variables_page(variables, TB_VARIABLE_LOCALS, aggregate, -1)); drain(gdb);
    CHECK(find(TB_VARIABLE_LOCALS, "0") < 36);
    CHECK(tb_variables_toggle(variables, TB_VARIABLE_LOCALS, aggregate)); drain(gdb);
    CHECK(tb_variables_count(variables, TB_VARIABLE_LOCALS) == 4);
    CHECK(tb_variables_watch(variables, "pointer")); drain(gdb);
    command(gdb, "-variable-stats", 998); unsigned before = reads, before_children = children;
    drain(gdb); command(gdb, "-variable-stats", 998); CHECK(reads == before && children == before_children);
    CHECK(tb_variables_toggle(variables, TB_VARIABLE_WATCHES, 0)); drain(gdb);
    CHECK(tb_variables_count(variables, TB_VARIABLE_WATCHES) == 2); /* explicit pointer inspection remains available */
    CHECK(tb_variables_format(variables, TB_VARIABLE_WATCHES, 1)); drain(gdb);
    CHECK(strcmp(tb_variables_row(variables, TB_VARIABLE_WATCHES, 1)->value, "0x11") == 0);
    CHECK(tb_variables_watch(variables, "unavailable"));
    size_t unavailable = find(TB_VARIABLE_WATCHES, "unavailable");
    CHECK(tb_variables_read(variables, TB_VARIABLE_WATCHES, unavailable)); drain(gdb);
    CHECK(!tb_variables_row(variables, TB_VARIABLE_WATCHES, unavailable)->available);
    CHECK(strstr(tb_variables_row(variables, TB_VARIABLE_WATCHES, unavailable)->value, "optimized out"));
    CHECK(!tb_variables_watch(variables, "f()")); CHECK(!tb_variables_watch(variables, "x=1"));
    CHECK(!tb_variables_watch(variables, "p[func()]")); CHECK(!tb_variables_watch(variables, "a++"));
    CHECK(tb_variables_watch(variables, "invalid"));
    size_t invalid = find(TB_VARIABLE_WATCHES, "invalid");
    CHECK(tb_variables_read(variables, TB_VARIABLE_WATCHES, invalid)); drain(gdb);
    CHECK(strstr(tb_variables_error(variables, TB_VARIABLE_WATCHES), "Unknown symbol"));
    command(gdb, "-variable-stats", 998); before = reads; before_children = children;
    CHECK(tb_variables_row(variables, TB_VARIABLE_WATCHES, 0)->fresh);
    command(gdb, "-run", 999); CHECK(tb_variables_stale(variables, TB_VARIABLE_WATCHES));
    CHECK(!tb_variables_row(variables, TB_VARIABLE_WATCHES, 0)->fresh);
    CHECK(!tb_variables_read(variables, TB_VARIABLE_WATCHES, 0));
    command(gdb, "-stop", 999); command(gdb, "-variable-stats", 998);
    CHECK(reads == before && children == before_children && deletes > 0); /* no new-stop authorization inherited */
    command_arg(gdb, "-variable-mode", "1", 999);
    CHECK(tb_variables_read(variables, TB_VARIABLE_WATCHES, 0)); drain(gdb);
    CHECK(strstr(tb_variables_error(variables, TB_VARIABLE_WATCHES), "Unexpected"));
    command_arg(gdb, "-variable-mode", "3", 999);
    CHECK(tb_variables_read(variables, TB_VARIABLE_WATCHES, 0));
    tb_variables_want(variables, TB_VARIABLE_WATCHES, 0, TB_VARIABLE_NODES);
    tb_variables_pump(variables);
    CHECK(tb_gdb_request(gdb, "-run", NULL, 0, TB_GDB_SESSION, 999, 1000, NULL));
    CHECK(tb_gdb_request(gdb, "-variable-release", NULL, 0, TB_GDB_SESSION, 999, 1000, NULL));
    drain(gdb);
    CHECK(!strstr(tb_variables_row(variables, TB_VARIABLE_WATCHES, 0)->value, "OBSOLETE"));
    command_arg(gdb, "-variable-mode", "0", 999); command(gdb, "-stop", 999);
    for (unsigned i = 0; i < 8; ++i) {
        CHECK(tb_variables_toggle(variables, TB_VARIABLE_LOCALS, aggregate)); drain(gdb);
        CHECK(tb_variables_count(variables, TB_VARIABLE_LOCALS) <= TB_VARIABLE_NODES);
        CHECK(tb_variables_toggle(variables, TB_VARIABLE_LOCALS, aggregate)); drain(gdb);
    }
    for (unsigned i = 0; i < 9; ++i) {
        char name[32]; (void)snprintf(name, sizeof name, "aggregate%u", i);
        if (!tb_variables_watch(variables, name)) {
            CHECK(tb_variables_limited(variables, TB_VARIABLE_WATCHES)); break;
        }
        size_t root = find(TB_VARIABLE_WATCHES, name);
        CHECK(tb_variables_toggle(variables, TB_VARIABLE_WATCHES, root)); drain(gdb);
        CHECK(tb_variables_count(variables, TB_VARIABLE_WATCHES) <= TB_VARIABLE_NODES);
    }
    CHECK(tb_variables_limited(variables, TB_VARIABLE_WATCHES));
    tb_context_visible(context, false, true); drain(gdb);
    CHECK(tb_context_select_frame(context, 1)); drain(gdb);
    scalar = find(TB_VARIABLE_LOCALS, "scalar");
    CHECK(tb_variables_read(variables, TB_VARIABLE_LOCALS, scalar)); drain(gdb);
    CHECK(!tb_variables_row(variables, TB_VARIABLE_LOCALS, scalar)->changed);
    command_arg(gdb, "-variable-mode", "6", 999);
    CHECK(tb_variables_watch(variables, "aggregate_cpp"));
    size_t cpp = find(TB_VARIABLE_WATCHES, "aggregate_cpp");
    CHECK(tb_variables_toggle(variables, TB_VARIABLE_WATCHES, cpp)); drain(gdb);
    size_t access = find(TB_VARIABLE_WATCHES, "public");
    CHECK(tb_variables_row(variables, TB_VARIABLE_WATCHES, access)->depth == 1);
    CHECK(!*tb_variables_row(variables, TB_VARIABLE_WATCHES, access)->type);
    CHECK(!tb_variables_row(variables, TB_VARIABLE_WATCHES, access)->expanded);
    command(gdb, "-variable-stats", 998); before_children = children;
    drain(gdb); command(gdb, "-variable-stats", 998); CHECK(children == before_children);
    CHECK(tb_variables_toggle(variables, TB_VARIABLE_WATCHES, access)); drain(gdb);
    CHECK(tb_variables_row(variables, TB_VARIABLE_WATCHES, find(TB_VARIABLE_WATCHES, "count"))->depth == 2);
    command_arg(gdb, "-variable-mode", "4", 999); tb_context_refresh(context); drain(gdb);
    CHECK(tb_variables_count(variables, TB_VARIABLE_LOCALS) == TB_VARIABLE_ROOTS);
    CHECK(tb_variables_limited(variables, TB_VARIABLE_LOCALS));
    command_arg(gdb, "-variable-mode", "5", 999); tb_context_refresh(context); drain(gdb);
    CHECK(tb_variables_count(variables, TB_VARIABLE_LOCALS) == TB_VARIABLE_ROOTS);
    CHECK(strncmp(tb_variables_row(variables, TB_VARIABLE_LOCALS, 0)->name, "new", 3) == 0);
    CHECK(tb_variables_remove(variables, 0)); drain(gdb);
    tb_variables_visible(variables, false, false); before = replies; drain(gdb); CHECK(before == replies);
    tb_gdb_close(gdb); end = tb_gdb_now_ms() + 1500;
    while (tb_gdb_state(gdb) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < end) { pump(gdb); }
    CHECK(tb_gdb_destroy(gdb)); tb_variables_destroy(variables); tb_memory_destroy(memory); tb_context_destroy(context);
    puts("PASS M11 pre-dispatch region/complete-footprint authorization, explicit pointer inspection, lazy bounded children/pages, formats, unavailable values, rejected calls/mutations, stop cleanup and no hidden/running/unknown polling");
    return EXIT_SUCCESS;
}
