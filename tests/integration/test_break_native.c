#define _POSIX_C_SOURCE 200809L
#include "native.h"
#include "../support/check.h"
#include <inttypes.h>
#include <string.h>

static TbNative *start(const char *fixture, const char *library)
{
    TbNative *n = tb_native_create(); CHECK(n); TbNativeConfig config = {.executable = fixture, .gdb = "gdb"};
    TbNativeWords arguments = {0};
    if (library) { arguments.count = 1; (void)snprintf(arguments.values[0], sizeof arguments.values[0], "%s", library); config.arguments = &arguments; }
    CHECK(tb_native_start(n, &config)); uint64_t end = tb_gdb_now_ms() + 5000;
    while ((tb_native_state(n) != TB_NATIVE_READY || !tb_break_ready(tb_native_breakpoints(n))) && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    CHECK(tb_native_state(n) == TB_NATIVE_READY); CHECK(tb_native_console(n, "start"));
    tb_code_visible(tb_native_code(n), 0, true, true);
    end = tb_gdb_now_ms() + 5000;
    while ((tb_native_state(n) != TB_NATIVE_STOPPED || !tb_context_snapshot(tb_native_context(n))->ready ||
            tb_context_snapshot(tb_native_context(n))->stale || !tb_break_ready(tb_native_breakpoints(n)) ||
            tb_code_view(tb_native_code(n), 0)->stale || !tb_code_view(tb_native_code(n), 0)->assembly_revision) && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    CHECK(tb_native_state(n) == TB_NATIVE_STOPPED); return n;
}
static void idle(TbNative *n)
{
    TbBreakpoints *b = tb_native_breakpoints(n); uint64_t end = tb_gdb_now_ms() + 5000;
    while ((!tb_break_ready(b) || tb_break_busy(b)) && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    CHECK(tb_break_ready(b) && !tb_break_busy(b));
}
static void stop(TbNative *n, uint64_t previous)
{
    uint64_t end = tb_gdb_now_ms() + 5000; const TbContextSnapshot *s = tb_context_snapshot(tb_native_context(n));
    while ((tb_native_state(n) != TB_NATIVE_STOPPED || s->generation.stop <= previous || s->stale || !s->ready) && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    if (!(tb_native_state(n) == TB_NATIVE_STOPPED && s->generation.stop > previous && !s->stale && s->ready)) {
        fprintf(stderr, "stop timeout state=%s previous=%llu stop=%llu stale=%d ready=%d\n", tb_native_state_name(n), (unsigned long long)previous, (unsigned long long)s->generation.stop, s->stale, s->ready);
        for (size_t i = 0; i < tb_native_log_count(n); ++i) { fprintf(stderr, "%s\n", tb_native_log_line(n, i)); }
    }
    CHECK(tb_native_state(n) == TB_NATIVE_STOPPED && s->generation.stop > previous && !s->stale && s->ready); idle(n);
}
static void close_session(TbNative *n)
{
    tb_native_close(n); uint64_t end = tb_gdb_now_ms() + 6000;
    while (tb_native_active(n) && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    CHECK(!tb_native_active(n) && tb_native_destroy(n));
}
static void code_ready(TbNative *n, uint64_t revision)
{
    const TbCodeView *v = tb_code_view(tb_native_code(n), 0); uint64_t end = tb_gdb_now_ms() + 8000;
    while ((v->stale || v->assembly_revision <= revision) && tb_gdb_now_ms() < end) { tb_native_pump(n, 5); }
    if (v->stale || v->assembly_revision <= revision) { fprintf(stderr, "code timeout %s / %s\n", v->assembly_error, tb_code_message(tb_native_code(n))); }
    CHECK(!v->stale && v->assembly_revision > revision);
}
static void browsing(TbNative *n)
{
    TbCode *code = tb_native_code(n); const TbCodeView *v = tb_code_view(code, 0);
    uint64_t revision = v->assembly_revision; CHECK(tb_code_address(code, 0, "_init")); code_ready(n, revision);
    CHECK(v->instruction_count && !*tb_code_assembly_notice(code, 0, 0, v->instruction_count));
    uint64_t init = v->instructions[0].numeric, gap = v->next_address;
    revision = v->assembly_revision; CHECK(tb_code_assembly_page(code, 0, 1)); code_ready(n, revision);
    uint64_t next = 0;
    for (size_t i = 0; i < v->instruction_count; ++i) {
        if (v->instructions[i].numeric >= gap) { next = v->instructions[i].numeric; break; }
    }
    CHECK(next > gap); /* The linker places padding between .init and .plt. */
    char location[48]; (void)snprintf(location, sizeof location, "0x%" PRIx64, next);
    revision = v->assembly_revision; CHECK(tb_code_address(code, 0, location)); code_ready(n, revision);
    revision = v->assembly_revision; CHECK(tb_code_assembly_page(code, 0, -1)); code_ready(n, revision);
    CHECK(v->instructions[0].numeric == init && !*tb_code_assembly_notice(code, 0, 0, v->instruction_count));
    for (size_t i = 0; i < v->instruction_count; ++i) { CHECK(v->instructions[i].numeric < gap || v->instructions[i].numeric >= next); }
    TbCodeView *saved = malloc(sizeof *saved); CHECK(saved); *saved = *v;
    for (int repeat = 0; repeat < 5; ++repeat) {
        tb_code_visible(code, 0, true, false); for (int i = 0; i < 20; ++i) { tb_native_pump(n, 5); }
        tb_code_visible(code, 0, true, true); for (int i = 0; i < 20; ++i) { tb_native_pump(n, 5); }
        CHECK(v->assembly_revision == saved->assembly_revision && v->instruction_count == saved->instruction_count);
        CHECK(memcmp(v->instructions, saved->instructions, sizeof v->instructions) == 0);
    }
    free(saved);
    (void)snprintf(location, sizeof location, "0x%" PRIx64, gap);
    revision = v->assembly_revision; CHECK(tb_code_address(code, 0, location)); code_ready(n, revision);
    CHECK(*tb_code_assembly_notice(code, 0, 0, 1));
    bool known_slice = false;
    for (size_t i = 1; i < v->instruction_count; ++i) {
        if (!*tb_code_assembly_notice(code, 0, i, 1)) { known_slice = true; break; }
    }
    CHECK(known_slice); /* Scrolling to known rows clears an offscreen warning. */
    revision = v->assembly_revision; CHECK(tb_code_address(code, 0, "m08_zeros")); code_ready(n, revision);
    CHECK(!v->instruction_count && strstr(v->assembly_error, "Non-code data"));
    revision = v->assembly_revision; tb_code_raw(code, 0); code_ready(n, revision);
    CHECK(v->instruction_count && strstr(v->instructions[0].opcodes, "00 00") && *tb_code_assembly_notice(code, 0, 0, v->instruction_count));
    tb_code_raw(code, 0); revision = v->assembly_revision; CHECK(tb_code_address(code, 0, "m08_long")); code_ready(n, revision);
    CHECK(v->instruction_count); uint64_t first = v->instructions[0].numeric;
    /* Overlapping windows retain half the previous rows. Exercise the same
     * >2000-byte traversal with enough bounded expansions to cross it. */
    for (int i = 0; i < 40; ++i) {
        revision = v->assembly_revision; CHECK(tb_code_assembly_page(code, 0, 1)); code_ready(n, revision);
        CHECK(v->instruction_count <= TB_CODE_INSNS);
        for (size_t j = 1; j < v->instruction_count; ++j) { CHECK(v->instructions[j].numeric > v->instructions[j - 1].numeric); }
    }
    CHECK(v->instructions[0].numeric > first + 2000);
    uint64_t later = v->instructions[0].numeric;
    revision = v->assembly_revision; CHECK(tb_code_assembly_page(code, 0, -1)); code_ready(n, revision);
    CHECK(v->instructions[0].numeric < later && v->instruction_count > 10);
    revision = v->assembly_revision; tb_code_return(code, 0); code_ready(n, revision);
    CHECK(v->assembly_follow);
    char stopped[48]; (void)snprintf(stopped, sizeof stopped, "%s", tb_context_snapshot(tb_native_context(n))->stopped.address);
    uint32_t random = 0xA55E;
    for (int i = 0; i < 100; ++i) {
        random = random * 1664525U + 1013904223U;
        char address[48]; (void)snprintf(address, sizeof address, "0x%" PRIx64, first + 32 + random % 3900);
        revision = v->assembly_revision; CHECK(tb_code_address(code, 0, address)); code_ready(n, revision);
        if (i % 3 == 0) { revision = v->assembly_revision; CHECK(tb_code_assembly_page(code, 0, -1)); code_ready(n, revision); }
        CHECK(v->instruction_count && v->instruction_count <= TB_CODE_INSNS);
        CHECK(strcmp(stopped, tb_context_snapshot(tb_native_context(n))->stopped.address) == 0);
    }
    revision = v->assembly_revision; tb_code_return(code, 0); code_ready(n, revision);
    puts("PASS M08 code/data boundary, explicit raw zeros, long-function forward/backward scans, sorted bounded cache and return-to-stop");
}
int main(int argc, char *argv[])
{
    CHECK(argc == 4); TbNative *n = start(argv[1], NULL); TbBreakpoints *b = tb_native_breakpoints(n);
    browsing(n); idle(n);
    CHECK(tb_break_add(b, "m08_tick", false)); idle(n); CHECK(tb_break_count(b) == 1);
    char id[32]; (void)snprintf(id, sizeof id, "%s", tb_break_row(b, 0)->id);
    CHECK(tb_break_condition(b, id, "value == 3")); idle(n); CHECK(strcmp(tb_break_row(b, 0)->condition, "value == 3") == 0);
    CHECK(tb_break_ignore(b, id, "2")); idle(n); CHECK(tb_break_row(b, 0)->ignore == 2);
    CHECK(!tb_break_ignore(b, id, "-1") && !tb_break_ignore(b, id, "4294967296"));
    CHECK(tb_break_ignore(b, id, "0")); idle(n);
    CHECK(tb_break_enable(b, id, false)); idle(n); CHECK(!tb_break_row(b, 0)->enabled);
    CHECK(tb_break_enable(b, id, true)); idle(n); CHECK(tb_break_row(b, 0)->enabled);
    uint64_t previous = tb_context_snapshot(tb_native_context(n))->generation.stop;
    CHECK(tb_native_continue(n)); stop(n, previous);
    CHECK(strstr(tb_context_snapshot(tb_native_context(n))->selected.function, "m08_tick"));
    CHECK(tb_break_row(b, 0)->hits >= 1);
    CHECK(tb_break_condition(b, id, "")); idle(n); CHECK(!*tb_break_row(b, 0)->condition);
    CHECK(tb_break_ignore(b, id, "2")); idle(n);
    previous = tb_context_snapshot(tb_native_context(n))->generation.stop;
    CHECK(tb_native_continue(n)); stop(n, previous); CHECK(tb_break_row(b, 0)->ignore == 0);
    CHECK(tb_break_condition(b, id, "value +")); idle(n); CHECK(!*tb_break_row(b, 0)->condition && *tb_break_message(b));
    CHECK(tb_break_condition(b, id, "value >= 8")); idle(n); CHECK(tb_break_ignore(b, id, "2")); idle(n);
    previous = tb_context_snapshot(tb_native_context(n))->generation.stop;
    CHECK(tb_native_continue(n)); stop(n, previous); CHECK(tb_break_row(b, 0)->ignore == 0);
    CHECK(tb_break_condition(b, id, "")); idle(n);
    CHECK(tb_break_add(b, "m08_not_loaded_yet", false)); idle(n); CHECK(tb_break_count(b) == 2 && tb_break_row(b, 1)->pending);
    char pending[32]; (void)snprintf(pending, sizeof pending, "%s", tb_break_row(b, 1)->id);
    CHECK(tb_break_remove(b, pending)); idle(n); CHECK(tb_break_count(b) == 1);
    previous = tb_context_snapshot(tb_native_context(n))->generation.stop;
    CHECK(tb_native_run_to(n, "main")); stop(n, previous); /* existing tick breakpoint intervenes */
    CHECK(tb_break_count(b) == 1 && strcmp(tb_break_row(b, 0)->id, id) == 0);
    CHECK(tb_break_remove(b, id)); idle(n); CHECK(tb_break_count(b) == 0);
    const TbCodeView *view = tb_code_view(tb_native_code(n), 0); uint64_t revision = view->assembly_revision;
    tb_code_return(tb_native_code(n), 0); code_ready(n, revision); CHECK(view->instruction_count > 1);
    char location[64]; (void)snprintf(location, sizeof location, "*%s", view->instructions[1].address);
    previous = tb_context_snapshot(tb_native_context(n))->generation.stop;
    CHECK(tb_native_run_to(n, location)); stop(n, previous); CHECK(tb_break_count(b) == 0);
    CHECK(tb_break_add(b, "m08_tick", true)); idle(n);
    if (tb_break_count(b)) {
        CHECK(tb_break_row(b, 0)->hardware);
        CHECK(tb_break_add(b, "m08_long", true)); idle(n); CHECK(tb_break_add(b, "main", true)); idle(n);
        for (size_t i = 0; i < 2; ++i) {
            (void)snprintf(location, sizeof location, "*%s", view->instructions[i].address);
            CHECK(tb_break_add(b, location, true)); idle(n);
        }
        CHECK(tb_break_count(b) == 5);
        CHECK(tb_native_continue(n)); bool exhausted = false; uint64_t end = tb_gdb_now_ms() + 4000;
        while ((!exhausted || tb_native_state(n) != TB_NATIVE_STOPPED) && tb_gdb_now_ms() < end) {
            tb_native_pump(n, 5);
            for (size_t i = 0; i < tb_native_log_count(n); ++i) {
                const char *log = tb_native_log_line(n, i);
                if (strstr(log, "hardware breakpoint") && (strstr(log, "Cannot") || strstr(log, "Could not"))) { exhausted = true; }
            }
        }
        if (!(exhausted && tb_native_state(n) == TB_NATIVE_STOPPED)) {
            fprintf(stderr, "hardware exhaustion state=%s\n", tb_native_state_name(n));
            for (size_t i = 0; i < tb_break_count(b); ++i) { fprintf(stderr, "hardware %s %s\n", tb_break_row(b, i)->id, tb_break_row(b, i)->address); }
            for (size_t i = 0; i < tb_native_log_count(n); ++i) { fprintf(stderr, "%s\n", tb_native_log_line(n, i)); }
        }
        CHECK(exhausted && tb_native_state(n) == TB_NATIVE_STOPPED); idle(n);
        while (tb_break_count(b)) {
            CHECK(tb_break_row(b, 0)->hardware);
            (void)snprintf(id, sizeof id, "%s", tb_break_row(b, 0)->id); CHECK(tb_break_remove(b, id)); idle(n);
        }
    } else { CHECK(*tb_break_message(b)); }
    for (int cycle = 0; cycle < 100; ++cycle) {
        CHECK(tb_break_add(b, "m08_tick", false)); idle(n); CHECK(tb_break_count(b) == 1);
        (void)snprintf(id, sizeof id, "%s", tb_break_row(b, 0)->id);
        CHECK(tb_break_enable(b, id, false)); idle(n);
        CHECK(tb_break_condition(b, id, "value > 0")); idle(n);
        CHECK(tb_break_ignore(b, id, "1")); idle(n);
        CHECK(tb_break_remove(b, id)); idle(n); CHECK(tb_break_count(b) == 0);
    }
    close_session(n);
    n = start(argv[2], NULL); b = tb_native_breakpoints(n); CHECK(tb_break_add(b, "m08_overload", false)); idle(n);
    CHECK(tb_break_count(b) == 3 && tb_break_row(b, 0)->multiple && tb_break_row(b, 1)->child && tb_break_row(b, 2)->child);
    CHECK(tb_break_enable(b, tb_break_row(b, 1)->id, false)); idle(n); CHECK(!tb_break_row(b, 1)->enabled);
    (void)snprintf(id, sizeof id, "%s", tb_break_row(b, 0)->id); CHECK(tb_break_remove(b, id)); idle(n); CHECK(tb_break_count(b) == 0); close_session(n);
    n = start(argv[1], argv[3]); b = tb_native_breakpoints(n); CHECK(tb_break_add(b, "m08_future", false)); idle(n); CHECK(tb_break_row(b, 0)->pending);
    previous = tb_context_snapshot(tb_native_context(n))->generation.stop;
    CHECK(tb_native_continue(n)); stop(n, previous);
    CHECK(!tb_break_row(b, 0)->pending && strstr(tb_context_snapshot(tb_native_context(n))->selected.function, "m08_future")); close_session(n);
    puts("PASS M08 actual GDB breakpoint CRUD, conditions/errors, ignore counts, pending locations, hardware policy and run-to cleanup after hit/intervening stop");
    return EXIT_SUCCESS;
}
