#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier): POSIX feature selection. */
#include "session.h"
#include "../support/check.h"
#include <inttypes.h>
#include <poll.h>
#include <string.h>

/* Explicit hardware exerciser. No programming, inferred memory ranges or
 * deletion of breakpoints not created by this process. The server is external. */
static TbSession *session;
static TbOpenOcdPlan *server_plan;
static char owned_break[32], owned_watch[32];
static const char *log_path(void)
{
    const char *path = getenv("TB_BOARD_LOG"); return path ? path : "build/m17/board-session.log";
}
static bool logged(const char *text)
{
    for (size_t i = 0; i < tb_session_log_count(session); ++i) {
        if (strstr(tb_session_log_line(session, i), text)) { return true; }
    }
    return false;
}
static void cleanup(void)
{
    if (!session) { return; }
    TbBreakpoints *b = tb_session_breakpoints(session);
    if (tb_session_state(session) == TB_SESSION_RUNNING) {
        (void)tb_session_halt(session);
        uint64_t until = tb_gdb_now_ms() + 2000;
        while (tb_session_state(session) == TB_SESSION_RUNNING && tb_gdb_now_ms() < until) { tb_session_pump(session, 5); }
    }
    const char *ids[] = {owned_break, owned_watch};
    for (size_t i = 0; i < sizeof ids / sizeof ids[0]; ++i) {
        if (!*ids[i] || tb_session_state(session) != TB_SESSION_STOPPED) { continue; }
        uint64_t until = tb_gdb_now_ms() + 1000;
        while (tb_break_busy(b) && tb_gdb_now_ms() < until) { tb_session_pump(session, 5); }
        if (tb_break_remove(b, ids[i])) {
            until = tb_gdb_now_ms() + 1000;
            while (tb_break_busy(b) && tb_gdb_now_ms() < until) { tb_session_pump(session, 5); }
        }
    }
    (void)tb_session_save_log(session, log_path());
    tb_session_disconnect(session, false);
    uint64_t end = tb_gdb_now_ms() + 6000;
    while (tb_session_active(session) && tb_gdb_now_ms() < end) { tb_session_pump(session, 5); }
    (void)tb_session_destroy(session); session = NULL;
    free(server_plan); server_plan = NULL;
}
static void pump(void) { tb_session_pump(session, 5); }
static const TbContextSnapshot *context(void) { return tb_context_snapshot(tb_session_context(session)); }
static void stop(uint64_t previous)
{
    uint64_t end = tb_gdb_now_ms() + 10000;
    while ((tb_session_state(session) != TB_SESSION_STOPPED || !context()->ready || context()->stale ||
            context()->generation.stop <= previous) && tb_gdb_now_ms() < end) { pump(); }
    if (tb_session_state(session) != TB_SESSION_STOPPED || !context()->ready || context()->stale || context()->generation.stop <= previous) {
        fprintf(stderr, "Board stop: %s / %s / %s\n", tb_session_state_name(session), tb_session_message(session), context()->error);
        CHECK(false);
    }
    printf("STOP T%s %s %s:%d PC %s\n", context()->stopped_thread, context()->selected.function,
           context()->selected.file, context()->selected.line, context()->stopped.address);
}
static void breaks(void)
{
    TbBreakpoints *b = tb_session_breakpoints(session); uint64_t end = tb_gdb_now_ms() + 5000;
    while ((!tb_break_ready(b) || tb_break_busy(b)) && tb_gdb_now_ms() < end) { pump(); }
    CHECK(tb_break_ready(b) && !tb_break_busy(b));
}
static void identifier(char out[32], bool watch)
{
    TbBreakpoints *b = tb_session_breakpoints(session);
    for (size_t i = 0; i < tb_break_count(b); ++i) {
        const TbBreakpoint *row = tb_break_row(b, i);
        if (row->watchpoint == watch && strstr(row->location, watch ? "fixture_counter" : "fixture_checkpoint")) {
            CHECK(row->hardware && row->enabled && !row->pending);
            (void)snprintf(out, 32, "%s", row->id); return;
        }
    }
    fprintf(stderr, "Break/watch creation: %s\n", tb_break_message(b)); CHECK(false);
}
static void memory(int view, const char *expression, size_t size, const char *expected)
{
    TbMemory *m = tb_session_memory(session); tb_memory_visible(m, view, true);
    CHECK(tb_memory_length(m, view, 16) && tb_memory_address(m, view, expression));
    const TbMemoryView *v = tb_memory_view(m, view); uint64_t end = tb_gdb_now_ms() + 5000;
    while ((!v->ready || v->pending) && tb_gdb_now_ms() < end) { pump(); }
    if (!v->ready || v->readable != 16) { fprintf(stderr, "Memory %s: %s\n", expression, v->error); CHECK(false); }
    for (size_t i = 0; i < 16; ++i) { CHECK(v->valid[i]); }
    CHECK(!memcmp(v->bytes + (view == 0 ? 1 : 0), expected, size));
    char line[256]; tb_memory_line(m, view, 0, line, sizeof line); puts(line);
    CHECK(!tb_memory_stale(m, view));
}
static void registers(void)
{
    TbRegisters *r = tb_session_registers(session); tb_registers_visible(r, true);
    uint64_t end = tb_gdb_now_ms() + 5000;
    const TbRegister *pc = NULL, *sp = NULL;
    do {
        tb_registers_begin(r); tb_registers_want(r, 0, 32);
        for (size_t i = 0; i < tb_registers_count(r); ++i) {
            const TbRegister *row = tb_registers_row(r, i);
            if (!strcmp(row->name, "pc") || !strcmp(row->name, "r15")) {
                pc = row; tb_registers_want(r, i, 1);
            }
            const char *architecture = tb_session_capabilities(session)->architecture;
            if (!strcmp(row->name, "sp") ||
                (strstr(architecture, "arm") && !strcmp(row->name, "r13")) ||
                (strstr(architecture, "xtensa") && !strcmp(row->name, "a1"))) {
                sp = row; tb_registers_want(r, i, 1);
            }
        }
        pump();
    } while ((!pc || !pc->available || !pc->fresh || !sp || !sp->available || !sp->fresh || tb_registers_stale(r)) && tb_gdb_now_ms() < end);
    CHECK(pc && pc->available && pc->fresh && !tb_registers_stale(r));
    CHECK(sp && sp->available && sp->fresh && strtoull(sp->value, NULL, 0) != 0);
    uint64_t value = strtoull(pc->value, NULL, 0), stopped = strtoull(context()->stopped.address, NULL, 0);
    CHECK((value & ~UINT64_C(1)) == (stopped & ~UINT64_C(1)));
    printf("REGISTER %s %s agrees with stopped PC\n", pc->name, pc->value);
}
static void frame(int level)
{
    CHECK(tb_context_select_frame(tb_session_context(session), level));
    uint64_t end = tb_gdb_now_ms() + 5000;
    while ((context()->stale || context()->selected.level != level) && tb_gdb_now_ms() < end) { pump(); }
    CHECK(!context()->stale && context()->selected.level == level);
}
static void locals(void)
{
    TbVariables *v = tb_session_variables(session); uint64_t end = tb_gdb_now_ms() + 5000;
    size_t selected = SIZE_MAX;
    while (selected == SIZE_MAX && tb_gdb_now_ms() < end) {
        pump();
        for (size_t i = 0; i < tb_variables_count(v, TB_VARIABLE_LOCALS); ++i) {
            if (!strcmp(tb_variables_row(v, TB_VARIABLE_LOCALS, i)->name, "stage")) { selected = i; break; }
        }
    }
    CHECK(selected != SIZE_MAX && !tb_variables_row(v, TB_VARIABLE_LOCALS, selected)->fresh);
    CHECK(tb_variables_read(v, TB_VARIABLE_LOCALS, selected)); end = tb_gdb_now_ms() + 5000;
    while (!tb_variables_row(v, TB_VARIABLE_LOCALS, selected)->fresh && tb_gdb_now_ms() < end) { pump(); }
    CHECK(tb_variables_row(v, TB_VARIABLE_LOCALS, selected)->fresh && tb_variables_row(v, TB_VARIABLE_LOCALS, selected)->available);
}
static void watch_value(void)
{
    TbVariables *v = tb_session_variables(session); tb_variables_visible(v, true, true);
    CHECK(tb_variables_watch(v, "fixture_sample")); uint64_t end = tb_gdb_now_ms() + 5000;
    bool sent = false;
    while (tb_gdb_now_ms() < end) {
        pump();
        if (!sent) { sent = tb_variables_read(v, TB_VARIABLE_WATCHES, 0); }
        const TbVariable *row = tb_variables_row(v, TB_VARIABLE_WATCHES, 0);
        if (sent && row && row->fresh) { CHECK(row->available && row->children >= 3); return; }
    }
    fprintf(stderr, "Variables: %s\n", tb_variables_error(v, TB_VARIABLE_WATCHES)); CHECK(false);
}
static uint32_t counter(void)
{
    TbMemory *m = tb_session_memory(session); tb_memory_visible(m, 0, true);
    CHECK(tb_memory_length(m, 0, 4) && tb_memory_address(m, 0, "&fixture_counter"));
    const TbMemoryView *v = tb_memory_view(m, 0); uint64_t end = tb_gdb_now_ms() + 5000;
    while ((!v->ready || v->pending) && tb_gdb_now_ms() < end) { pump(); }
    CHECK(v->ready && v->readable == 4 && !tb_memory_stale(m, 0));
    return (uint32_t)v->bytes[0] | (uint32_t)v->bytes[1] << 8U | (uint32_t)v->bytes[2] << 16U | (uint32_t)v->bytes[3] << 24U;
}
static void disconnect(bool resume)
{
    tb_session_disconnect(session, resume); uint64_t end = tb_gdb_now_ms() + 6000;
    while (tb_session_active(session) && tb_gdb_now_ms() < end) { pump(); }
    CHECK(tb_session_state(session) == TB_SESSION_CLOSED && !tb_session_active(session));
}
int main(int argc, char **argv)
{
    if (argc != 5 && argc != 6) {
        fprintf(stderr, "Usage: %s GDB HOST:PORT ELF ARCH [--reset-halt]\nRuns already-programmed M17 fixture; explicit halt/continue/step/break/watch and leave-halted close. Never flashes.\n", argv[0]); return 2;
    }
    CHECK(argc == 5 || !strcmp(argv[5], "--reset-halt"));
    session = tb_session_create(); CHECK(session && atexit(cleanup) == 0);
    server_plan = calloc(1, sizeof *server_plan); CHECK(server_plan);
    CHECK(strlen(argv[2]) < sizeof server_plan->endpoint); (void)snprintf(server_plan->endpoint, sizeof server_plan->endpoint, "%s", argv[2]);
    TbSessionConfig config = {.kind = TB_SESSION_REMOTE, .endpoint = argv[2], .gdb = argv[1], .executable = argv[3], .openocd = server_plan};
    CHECK(tb_session_start(session, &config));
    tb_context_visible(tb_session_context(session), true, true); stop(0);
    printf("TARGET architecture %s, address bits %u, byte order %d\n", tb_session_capabilities(session)->architecture,
           tb_session_capabilities(session)->address_bits, tb_session_capabilities(session)->byte_order);
    CHECK(strstr(tb_session_capabilities(session)->architecture, argv[4]));
    CHECK(!tb_session_capabilities(session)->server_owned && tb_session_target_pid(session) == 0);
    breaks(); TbBreakpoints *b = tb_session_breakpoints(session);
    for (size_t i = 0; i < tb_break_count(b); ++i) {
        const TbBreakpoint *row = tb_break_row(b, i);
        CHECK(!strstr(row->location, "fixture_checkpoint") && !strstr(row->location, "fixture_counter"));
    }
    CHECK(tb_break_add(b, "fixture_checkpoint", true)); breaks(); identifier(owned_break, false);
    uint64_t before = context()->generation.stop;
    CHECK(tb_session_continue(session)); stop(before);
    CHECK(strstr(context()->selected.function, "fixture_checkpoint"));
    CHECK(context()->selected.line > 0 && context()->frame_count >= 2);
    TbCode *code = tb_session_code(session); tb_code_visible(code, 0, true, true);
    uint64_t code_end = tb_gdb_now_ms() + 5000;
    const TbCodeView *listing = tb_code_view(code, 0);
    while ((listing->stale || !listing->line_count || !listing->instruction_count) && tb_gdb_now_ms() < code_end) { pump(); }
    CHECK(!listing->stale && listing->line_count && listing->instruction_count);
    CHECK(strstr(listing->path, "workload.c"));
    registers();
    memory(0, "&fixture_buffer", 15, "17 SRAM: hex and");
    memory(1, "&fixture_flash", 14, "THE BUGGER M17");
    CHECK(tb_memory_view(tb_session_memory(session), 0)->address != tb_memory_view(tb_session_memory(session), 1)->address);
    watch_value(); locals();
    char stopped_address[48]; (void)snprintf(stopped_address, sizeof stopped_address, "%s", context()->stopped.address);
    frame(1); CHECK(!strcmp(stopped_address, context()->stopped.address));
    frame(0); CHECK(!strcmp(stopped_address, context()->selected.address));
    CHECK(tb_break_enable(b, owned_break, false)); breaks();
    before = context()->generation.stop; CHECK(tb_session_step(session, TB_STEP_INTO, false)); stop(before);
    before = context()->generation.stop; CHECK(tb_session_step(session, TB_STEP_INTO, true)); stop(before);
    before = context()->generation.stop; CHECK(tb_session_step(session, TB_STEP_OUT, false)); stop(before);
    CHECK(!strstr(context()->selected.function, "fixture_checkpoint"));
    CHECK(tb_break_remove(b, owned_break)); breaks(); owned_break[0] = 0;
    CHECK(tb_break_watch(b, "fixture_counter", TB_WATCH_CHANGE)); breaks(); identifier(owned_watch, true);
    before = context()->generation.stop; CHECK(tb_session_continue(session)); stop(before);
    breaks();
    CHECK(logged("watchpoint-trigger"));
    bool hit = false;
    for (size_t i = 0; i < tb_break_count(b); ++i) {
        const TbBreakpoint *row = tb_break_row(b, i);
        if (!strcmp(row->id, owned_watch)) { hit = row->hits > 0; }
    }
    CHECK(hit);
    CHECK(tb_break_remove(b, owned_watch)); breaks(); owned_watch[0] = 0;
    uint32_t value = counter(); disconnect(true); CHECK(poll(NULL, 0, 500) == 0);
    CHECK(tb_session_start(session, &config)); tb_context_visible(tb_session_context(session), true, true); stop(0);
    uint32_t resumed = counter(); CHECK(resumed != value);
    disconnect(false); CHECK(poll(NULL, 0, 500) == 0);
    CHECK(tb_session_start(session, &config)); tb_context_visible(tb_session_context(session), true, true); stop(0);
    CHECK(counter() == resumed);
    printf("DISCONNECT resume advanced counter %" PRIu32 " -> %" PRIu32 "; leave-halted retained it\n", value, resumed);
    if (argc == 6) {
        before = context()->generation.stop; CHECK(tb_session_reset_halt(session)); stop(before);
        CHECK(tb_session_capabilities(session)->connected && *context()->stopped_thread && *context()->stopped.address);
    }
    CHECK(tb_session_save_log(session, log_path()));
    disconnect(false);
    CHECK(tb_session_destroy(session)); session = NULL; free(server_plan); server_plan = NULL;
    puts("PASS M17 actual board shared session, source context, PC/register agreement, independent SRAM/Flash reads, explicit locals/aggregate and caller selection, source/instruction step/Step Out, hardware break/watch, proven resume/leave-halted disconnect");
    return 0;
}
