#define _POSIX_C_SOURCE 200809L
#include "native.h"
#include "../support/check.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

/* One live session exercises inspectors together, rather than assuming their
 * separate suites establish cross-model generation/lifecycle correctness. */
static uint64_t maximum_pump;
static void pump(TbNative *n)
{
    tb_registers_begin(tb_native_registers(n));
    tb_registers_want(tb_native_registers(n), 0, 24);
    tb_variables_begin(tb_native_variables(n));
    tb_variables_want(tb_native_variables(n), TB_VARIABLE_LOCALS, 0, 16);
    tb_variables_want(tb_native_variables(n), TB_VARIABLE_WATCHES, 0, 16);
    uint64_t before = tb_gdb_now_ms();
    tb_native_pump(n, 10);
    uint64_t elapsed = tb_gdb_now_ms() - before;
    if (elapsed > maximum_pump) { maximum_pump = elapsed; }
}
static void stopped(TbNative *n, uint64_t after)
{
    const TbContextSnapshot *s = tb_context_snapshot(tb_native_context(n));
    uint64_t deadline = tb_gdb_now_ms() + 8000;
    while (tb_gdb_now_ms() < deadline) {
        pump(n);
        if (tb_native_state(n) == TB_NATIVE_STOPPED && s->ready && !s->stale && s->generation.stop > after &&
            tb_break_ready(tb_native_breakpoints(n)) && !tb_break_busy(tb_native_breakpoints(n))) { return; }
    }
    fprintf(stderr, "M14 stop: %s\n", tb_native_message(n));
    for (size_t i = 0; i < tb_native_log_count(n); ++i) { fprintf(stderr, "%s\n", tb_native_log_line(n, i)); }
    CHECK(false);
}
static void run(TbNative *n, const char *fixture)
{
    TbNativeConfig config = {.gdb = "gdb", .executable = fixture};
    CHECK(tb_native_start(n, &config));
    uint64_t deadline = tb_gdb_now_ms() + 8000;
    while (tb_native_state(n) != TB_NATIVE_READY && tb_gdb_now_ms() < deadline) { pump(n); }
    CHECK(tb_native_state(n) == TB_NATIVE_READY);
    TbContext *context = tb_native_context(n); TbCode *code = tb_native_code(n);
    TbRegisters *registers = tb_native_registers(n); TbVariables *variables = tb_native_variables(n);
    TbMemory *memory = tb_native_memory(n);
    tb_context_visible(context, true, true); tb_code_visible(code, 0, true, true);
    tb_registers_visible(registers, true); tb_variables_visible(variables, true, true);
    tb_memory_visible(memory, 0, true); tb_memory_visible(memory, 1, true);
    /* Continue correctly runs until a real stop; it does not insert a main
     * breakpoint. Explicit start supplies the initial stop for this fixture. */
    CHECK(tb_native_console(n, "start")); stopped(n, 0);
    const TbContextSnapshot *s = tb_context_snapshot(context);
    uint64_t stop = s->generation.stop;
    CHECK(tb_native_run_to(n, "variables_ready")); stopped(n, stop);
    CHECK(strstr(s->selected.function, "variables_ready"));
    CHECK(!s->frames[0].arguments_read && strstr(s->frames[0].arguments, "<read required>"));
    CHECK(tb_context_read_arguments(context));
    deadline = tb_gdb_now_ms() + 5000;
    while (!s->frames[0].arguments_read && tb_gdb_now_ms() < deadline) { pump(n); }
    CHECK(s->frames[0].arguments_read && strstr(s->frames[0].arguments, "stage=17"));
    deadline = tb_gdb_now_ms() + 5000;
    bool added = false;
    while (!added && tb_gdb_now_ms() < deadline) { added = tb_variables_watch(variables, "variables_global"); if (!added) { pump(n); } }
    CHECK(added);
    deadline = tb_gdb_now_ms() + 5000;
    while (!tb_variables_count(variables, TB_VARIABLE_WATCHES) && tb_gdb_now_ms() < deadline) { pump(n); }
    CHECK(tb_variables_count(variables, TB_VARIABLE_WATCHES) == 1);
    CHECK(!tb_variables_row(variables, TB_VARIABLE_WATCHES, 0)->fresh);
    deadline = tb_gdb_now_ms() + 5000;
    bool requested = false;
    while (!requested && tb_gdb_now_ms() < deadline) { requested = tb_variables_read(variables, TB_VARIABLE_WATCHES, 0); if (!requested) { pump(n); } }
    CHECK(requested);
    CHECK(tb_memory_length(memory, 0, 16)); CHECK(tb_memory_length(memory, 1, 16));
    CHECK(tb_memory_address(memory, 0, "&variables_global"));
    CHECK(tb_memory_address(memory, 1, "&variables_sample"));
    deadline = tb_gdb_now_ms() + 8000;
    while (tb_gdb_now_ms() < deadline) {
        pump(n);
        const TbVariable *value = tb_variables_row(variables, TB_VARIABLE_WATCHES, 0);
        if (value && value->fresh && tb_memory_view(memory, 0)->ready && tb_memory_view(memory, 1)->ready &&
            tb_registers_count(registers) && tb_registers_row(registers, 0)->fresh &&
            tb_code_view(code, 0)->instruction_count && !tb_code_view(code, 0)->stale) { break; }
    }
    CHECK(tb_variables_row(variables, TB_VARIABLE_WATCHES, 0)->fresh);
    CHECK(strcmp(tb_variables_row(variables, TB_VARIABLE_WATCHES, 0)->value, "17") == 0);
    const TbMemoryView *a = tb_memory_view(memory, 0), *b = tb_memory_view(memory, 1);
    CHECK(a->ready && b->ready && a->readable == 16 && b->readable == 16 && a->address != b->address);
    CHECK(a->bytes[tb_memory_get_order(memory) == TB_MEMORY_LITTLE ? 0 : 3] == 17);
    CHECK(tb_registers_count(registers) && tb_registers_row(registers, 0)->fresh);
    CHECK(tb_code_view(code, 0)->instruction_count && !tb_code_view(code, 0)->stale);
    /* Drain late independent replies, then prove settled inspectors do not
     * refresh continuously. CPU/wall measurements are reported, not presented
     * as a substitute for terminal input-to-redraw latency measurements. */
    for (int i = 0; i < 50; ++i) { pump(n); }
    uint64_t revisions[] = {tb_context_revision(context), tb_code_revision(code),
        tb_registers_revision(registers), tb_variables_revision(variables), tb_memory_revision(memory)};
    clock_t cpu = clock(); CHECK(cpu != (clock_t)-1);
    uint64_t before = tb_gdb_now_ms(), until = before + 1000;
    while (tb_gdb_now_ms() < until) { pump(n); }
    clock_t cpu_after = clock(); CHECK(cpu_after != (clock_t)-1);
    CHECK(revisions[0] == tb_context_revision(context) && revisions[1] == tb_code_revision(code));
    CHECK(revisions[2] == tb_registers_revision(registers) && revisions[3] == tb_variables_revision(variables));
    CHECK(revisions[4] == tb_memory_revision(memory));
    printf("M14 idle: wall=%llu ms frontend CPU=%.2f ms max pump=%llu ms (10 ms requested wait)\n",
           (unsigned long long)(tb_gdb_now_ms() - before), 1000.0 * (double)(cpu_after - cpu) / CLOCKS_PER_SEC,
           (unsigned long long)maximum_pump);
    stop = s->generation.stop;
    CHECK(tb_native_step(n, TB_STEP_INTO, true)); stopped(n, stop);
    CHECK(!s->frames[0].arguments_read && tb_memory_stale(memory, 0) && tb_memory_stale(memory, 1));
    CHECK(!tb_variables_row(variables, TB_VARIABLE_WATCHES, 0)->fresh);
    char directory[] = "/tmp/the-bugger-m14-log-XXXXXX"; CHECK(mkdtemp(directory));
    char path[128]; (void)snprintf(path, sizeof path, "%s/session.log", directory);
    CHECK(tb_native_save_log(n, path)); FILE *log = fopen(path, "r"); CHECK(log);
    CHECK(fgetc(log) != EOF); CHECK(fclose(log) == 0); CHECK(unlink(path) == 0); CHECK(rmdir(directory) == 0);
    tb_native_close(n); deadline = tb_gdb_now_ms() + 8000;
    while (tb_native_state(n) != TB_NATIVE_CLOSED && tb_gdb_now_ms() < deadline) { pump(n); }
    CHECK(tb_native_state(n) == TB_NATIVE_CLOSED);
}
int main(int argc, char **argv)
{
    CHECK(argc == 3); struct utsname host; CHECK(uname(&host) == 0);
    printf("M14 host: %s %s %s\n", host.sysname, host.release, host.machine);
    TbNative *n = tb_native_create(); CHECK(n);
    run(n, argv[1]); run(n, argv[2]); run(n, argv[1]);
    CHECK(tb_native_destroy(n));
    puts("PASS M14 repeated C/C++ integrated context/code/registers/Memory/Watches, explicit args, idle stability, step invalidation, Save Log and cleanup");
    return 0;
}
