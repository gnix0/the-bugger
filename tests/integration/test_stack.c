#define _POSIX_C_SOURCE 200809L
#include "stack.h"
#include "../support/check.h"
#include <inttypes.h>
#include <string.h>
#include <unistd.h>

static void reply_bytes(unsigned long long token, uint64_t address, unsigned size, bool partial)
{
    unsigned count = partial ? size / 2 : size;
    printf("%llu^done,memory=[{begin=\"0x%" PRIx64 "\",end=\"0x%" PRIx64 "\",contents=\"", token, address, address + count);
    for (unsigned i = 0; i < count; ++i) { printf("%02x", i & 255U); }
    puts("\"}]");
}
static int peer(void)
{
    char input[4096]; unsigned mode = 0, reads = 0, values = 0, anchors = 0, guards = 0;
    unsigned thread = 1, frame = 0; bool calls = true; unsigned long long held = 0;
    uint64_t held_address = 0; unsigned held_size = 0; bool held_value = false;
    CHECK(setvbuf(stdout, NULL, _IOLBF, 0) == 0); puts("(gdb)");
    while (fgets(input, sizeof input, stdin)) {
        char *op; unsigned long long token = strtoull(input, &op, 10);
        if (strstr(op, "-gdb-set may-call-functions off")) { calls = false; printf("%llu^done\n", token); }
        else if (strstr(op, "-gdb-set")) { printf("%llu^done\n", token); }
        else if (strstr(op, "-gdb-show may-call-functions")) { ++guards; printf("%llu^done,value=\"%s\"\n", token, calls ? "on" : "off"); }
        else if (strstr(op, "-gdb-show")) { printf("%llu^done,value=\"%s\"\n", token, strstr(op, "non-stop") ? "off" : "on"); }
        else if (strstr(op, "-fstop")) { printf("%llu^done\n*stopped,thread-id=\"1\",frame={addr=\"0x9000\",func=\"checkpoint\",arch=\"i386:x86-64\"}\n", token); }
        else if (strstr(op, "-fmode")) { mode = (unsigned)strtoul(op + 6, NULL, 10); printf("%llu^done\n", token); }
        else if (strstr(op, "-freuse")) { puts("=thread-exited,id=\"1\"\n=thread-created,id=\"1\""); printf("%llu^done\n", token); }
        else if (strstr(op, "-fstats")) { printf("%llu^done,reads=\"%u\",values=\"%u\",anchors=\"%u\",guards=\"%u\"\n", token, reads, values, anchors, guards); }
        else if (strstr(op, "-frelease")) {
            printf("%llu^done\n", token);
            if (held) { if (held_value) { printf("%llu^done,value=\"4224\"\n", held); } else { reply_bytes(held, held_address, held_size, false); } held = 0; held_value = false; }
        } else if (strstr(op, "-thread-info")) {
            printf("%llu^done,threads=[{id=\"1\",target-id=\"Thread one\",state=\"stopped\"},{id=\"2\",target-id=\"Thread two\",state=\"stopped\"}],current-thread-id=\"%u\"\n", token, thread);
        } else if (strstr(op, "-thread-select")) { thread = (unsigned)strtoul(op + 14, NULL, 10); frame = 0; printf("%llu^done\n", token); }
        else if (strstr(op, "-stack-select-frame")) { frame = (unsigned)strtoul(op + 19, NULL, 10); printf("%llu^done\n", token); }
        else if (strstr(op, "-stack-info-frame")) { printf("%llu^done,frame={level=\"%u\",addr=\"0x9000\",func=\"checkpoint\",arch=\"i386:x86-64\"}\n", token, frame); }
        else if (strstr(op, "-stack-list-frames")) { printf("%llu^done,stack=[frame={level=\"0\",addr=\"0x9000\"},frame={level=\"1\",addr=\"0x9010\"}]\n", token); }
        else if (strstr(op, "-stack-list-arguments")) { CHECK(strstr(op, "--no-values")); printf("%llu^done,stack-args=[]\n", token); }
        else if (strstr(op, "-data-list-register-names")) { printf("%llu^done,register-names=[\"rax\",\"\",\"rsp\",\"rip\"]\n", token); }
        else if (strstr(op, "-data-list-register-values")) {
            CHECK(strstr(op, "--thread") && strstr(op, "--frame")); ++anchors;
            if (mode == 3) { printf("%llu^done,register-values=[{number=\"2\",value=\"<unavailable>\"}]\n", token); }
            else { printf("%llu^done,register-values=[{number=\"2\",value=\"0x%x\"}]\n", token, (strstr(op, "--thread 2") ? 0x2000U : 0x1000U) + (strstr(op, "--frame 1") ? 0x100U : 0x80U)); }
        } else if (strstr(op, "-stack-list-variables")) {
            CHECK(strstr(op, "--no-values") && strstr(op, "--no-frame-filters") && strstr(op, "--thread") && strstr(op, "--frame"));
            printf("%llu^done,variables=[{name=\"local\"},{name=\"reference\"},{name=\"regonly\"},{name=\"optimized\"},{name=\"pieces\"},{name=\"ambig\"},{name=\"ambig\"},{name=\"bad()\"},{name=\"MACRO\"}]\n", token);
        } else if (strstr(op, "-data-evaluate-expression")) {
            CHECK(!calls && strstr(op, "--language c") && strstr(op, "--thread") && strstr(op, "--frame") && strstr(op, "&'")); ++values;
            if (mode == 5) { CHECK(!held); held = token; held_value = true; }
            else if (strstr(op, "regonly") || strstr(op, "optimized") || strstr(op, "pieces")) { printf("%llu^error,msg=\"Unsupported register-only/optimized/piecewise location\"\n", token); }
            else { printf("%llu^done,value=\"4224\"\n", token); }
        } else if (strstr(op, "-data-read-memory-bytes")) {
            ++reads; char *at = strstr(op, "0x"); CHECK(at); uint64_t address = strtoull(at, &at, 16); unsigned size = (unsigned)strtoul(at, NULL, 10);
            CHECK(size && size <= 1024); if (strstr(op, "--thread")) { CHECK(strstr(op, "--frame 0")); }
            if (mode == 2 || mode == 4) { CHECK(!held); held = token; held_address = address; held_size = size; }
            else { reply_bytes(token, address, size, mode == 1); }
        } else if (strstr(op, "-exec-continue")) { printf("%llu^running\n*running,thread-id=\"all\"\n", token); }
        else { fprintf(stderr, "Unexpected synthetic Stack command: %s", op); return 2; }
    }
    return 0;
}
static TbGdb *g;
static TbContext *c;
static TbMemory *m;
static TbStack *s;
static unsigned completed, reads, values, anchors, guards;
static void event(void *unused, const TbGdbEvent *e)
{
    (void)unused;
    if (!tb_stack_event(s, e) && !tb_memory_event(m, e)) { (void)tb_context_event(c, e); }
    if (e->kind == TB_GDB_REPLY && e->cookie == 999) {
        ++completed;
        const TbMiValue *v = tb_mi_find(e->record->results, "reads");
        if (v) {
            reads = (unsigned)strtoul(v->string.data, NULL, 10);
            values = (unsigned)strtoul(tb_mi_find(e->record->results, "values")->string.data, NULL, 10);
            anchors = (unsigned)strtoul(tb_mi_find(e->record->results, "anchors")->string.data, NULL, 10);
            guards = (unsigned)strtoul(tb_mi_find(e->record->results, "guards")->string.data, NULL, 10);
        }
    }
}
static void pump(void) { tb_gdb_pump(g, 1); tb_context_pump(c); tb_stack_pump(s); tb_memory_pump(m); }
static void command(const char *op, const char *argument)
{
    unsigned before = completed; const char *args[] = {argument};
    CHECK(tb_gdb_request(g, op, args, argument ? 1 : 0, TB_GDB_SESSION, 999, 1000, NULL));
    uint64_t until = tb_gdb_now_ms() + 2000;
    while (completed == before && tb_gdb_now_ms() < until) { pump(); }
    CHECK(completed > before);
}
static void settle(void)
{
    uint64_t until = tb_gdb_now_ms() + 5000;
    while (tb_gdb_now_ms() < until) {
        pump(); if (!tb_gdb_pending(g) && tb_context_snapshot(c)->ready && !tb_context_snapshot(c)->stale) { pump(); if (!tb_gdb_pending(g)) { return; } }
    }
    CHECK(false);
}
static void idle(void)
{
    command("-fstats", NULL); unsigned old_reads = reads, old_values = values, old_anchors = anchors;
    for (int i = 0; i < 10; ++i) { pump(); }
    command("-fstats", NULL); CHECK(reads == old_reads && values == old_values && anchors == old_anchors);
}
static void annotations(void)
{
    const TbStackView *v = tb_stack_view(s, 0); char text[24576];
    CHECK(tb_stack_names(s, 0)); settle(); CHECK(v->names_ready && v->name_count == 9);
    CHECK(!v->names[5].eligible && !v->names[6].eligible && !v->names[7].eligible);
    CHECK(!tb_stack_variable_point(s, 0, 5)); idle();
    CHECK(tb_stack_variable_point(s, 0, 0)); settle(); CHECK(v->guard_confirmation && !v->point_count);
    command("-fstats", NULL); CHECK(values == 0 && guards == 1);
    CHECK(tb_stack_disable_calls(s, 0)); settle(); CHECK(v->point_count == 1 && !v->guard_confirmation && !*v->annotation_error);
    command("-fstats", NULL); CHECK(values == 1 && guards == 2);
    CHECK(tb_stack_variable_point(s, 0, 1)); settle(); CHECK(v->point_count == 2);
    CHECK(tb_stack_variable_point(s, 0, 8)); settle(); CHECK(v->point_count == 3); /* Quoted macro name. */
    for (size_t name = 2; name < 5; ++name) {
        CHECK(tb_stack_variable_point(s, 0, name)); settle(); CHECK(v->point_count == 3 && *v->annotation_error);
    }
    command("-fmode", "5"); command("-fstats", NULL); unsigned before = values;
    CHECK(tb_stack_variable_point(s, 0, 0));
    for (int i = 0; i < 30; ++i) { pump(); }
    command("-fstats", NULL); CHECK(values == before + 1); CHECK(!tb_stack_frame_point(s, 0));
    tb_stack_cancel(s, 0); CHECK(v->cancel_requested); command("-frelease", NULL); settle();
    CHECK(v->point_count == 3 && !v->cancel_requested && v->sample.sequence);
    CHECK(tb_stack_variable_point(s, 0, 0));
    uint64_t timeout = tb_gdb_now_ms() + 3500;
    while (tb_gdb_now_ms() < timeout) { pump(); }
    CHECK(v->point_count == 3 && strstr(v->annotation_error, "timed out"));
    command("-frelease", NULL); settle(); CHECK(v->point_count == 3); idle();
    command("-fmode", "0");
    while (v->point_count < TB_STACK_POINTS) { CHECK(tb_stack_frame_point(s, 0)); settle(); }
    CHECK(!tb_stack_frame_point(s, 0)); CHECK(tb_stack_remove_point(s, 0, 0));
    CHECK(tb_stack_frame_point(s, 0)); settle(); CHECK(v->point_count == 32);
    tb_stack_details(s, 0, (size_t)((v->sp - v->start) / v->grouping), text, sizeof text);
    CHECK(strstr(text, "Sample provenance") && strstr(text, "GDB-reported selected-frame SP"));
    CHECK(strstr(text, "no extent") || strstr(text, "no pointer"));
}
int main(int argc, char **argv)
{
    if (argc > 1 && strcmp(argv[1], "--quiet") == 0) { return peer(); }
    CHECK(argc == 1);
    g = tb_gdb_create(event, NULL); CHECK(g); c = tb_context_create(g); CHECK(c); m = tb_memory_create(g, c); CHECK(m);
    TbBrowseCache *cache = tb_browse_create(); CHECK(cache); tb_memory_cache(m, cache); s = tb_stack_create(g, c, m, cache); CHECK(s);
    tb_memory_target(m, true, 64); tb_memory_order(m, TB_MEMORY_LITTLE); tb_stack_target(s, true, 64, TB_MEMORY_LITTLE, "i386:x86-64");
    CHECK(tb_gdb_start(g, argv[0], 1000)); uint64_t until = tb_gdb_now_ms() + 2000;
    while (tb_gdb_state(g) != TB_GDB_READY && tb_gdb_now_ms() < until) { pump(); }
    CHECK(tb_gdb_state(g) == TB_GDB_READY); command("-fstop", NULL);
    tb_context_visible(c, true, true); tb_stack_visible(s, 0, true); tb_stack_visible(s, 1, true); settle();
    const TbStackView *v = tb_stack_view(s, 0), *other = tb_stack_view(s, 1);
    CHECK(v->anchor_valid && v->sp == 0x1080 && v->start == 0x1040 && v->size == 128 && v->grouping == 8);
    char unread[512]; size_t value_column = 0;
    for (size_t row = 0; row < tb_stack_rows(s, 0); ++row) {
        tb_stack_line(s, 0, row, unread, sizeof unread);
        const char *at = strstr(unread, "UNREAD"); CHECK(at);
        if (!row) { value_column = (size_t)(at - unread); }
        CHECK((size_t)(at - unread) == value_column);
    }
    CHECK(other->anchor_valid && other->sp == v->sp); command("-fstats", NULL); CHECK(reads == 0 && anchors == 1);
    uint64_t lifetime = v->lifetime; CHECK(lifetime);
    CHECK(tb_stack_read(s, 0)); settle(); CHECK(v->sample.sequence && !tb_stack_stale(s, 0));
    char text[24576]; tb_stack_line(s, 0, 0, text, sizeof text); CHECK(strstr(text, "0x0706050403020100"));
    CHECK(tb_stack_declare(s, 0, 0x1000, 0x1080, false)); tb_stack_bounds(s, 0, text, sizeof text); CHECK(strstr(text, "start: 0 bytes"));
    CHECK(tb_stack_declare(s, 0, 0x1080, 0x1100, true)); tb_stack_bounds(s, 0, text, sizeof text); CHECK(strstr(text, "start: 0 bytes"));
    CHECK(!tb_stack_declaration(s, 1)); CHECK(tb_stack_declare(s, 0, 0x2000, 0x2100, false));
    tb_stack_bounds(s, 0, text, sizeof text); CHECK(strstr(text, "below") && !strstr(text, "displacement"));
    annotations(); CHECK(tb_stack_view(s, 0)->sample.sequence); /* Annotation errors preserve bytes. */
    CHECK(tb_stack_navigate(s, 0, "+0")); CHECK(v->start == 0x1080 && !v->follow); idle();
    CHECK(!tb_stack_navigate(s, 0, "-18446744073709551615")); CHECK(!tb_stack_navigate(s, 0, "0xffffffffffffffff"));
    CHECK(!tb_stack_navigate(s, 0, "$sp")); CHECK(!tb_stack_navigate(s, 0, "foo()"));
    CHECK(tb_stack_size(s, 0, 3)); CHECK(tb_stack_group(s, 0, 2)); command("-fmode", "1"); CHECK(tb_stack_read(s, 0)); settle();
    tb_stack_line(s, 0, 0, text, sizeof text); CHECK(strstr(text, "missing bytes")); tb_stack_line(s, 0, 1, text, sizeof text); CHECK(strstr(text, "incomplete group"));
    CHECK(tb_stack_follow(s, 0)); CHECK(tb_stack_size(s, 0, 128)); command("-fmode", "0");
    CHECK(tb_context_select_frame(c, 1)); settle(); CHECK(v->sp == 0x1080 && v->start == 0x1040); CHECK(v->point_count == 0);
    CHECK(tb_stack_frame_point(s, 0)); settle(); CHECK(v->points[0].address == 0x1100 && v->sp == 0x1080);
    CHECK(tb_context_select_thread(c, "2")); settle(); CHECK(v->sp == 0x2080 && !v->sample.sequence && v->follow && !v->point_count);
    CHECK(tb_stack_cached(s, 0, -1)); CHECK(v->sample.lifetime == lifetime && v->historical);
    tb_stack_line(s, 0, 0, text, sizeof text); CHECK(strstr(text, "offset unavailable")); CHECK(!tb_stack_read(s, 0));
    CHECK(tb_context_select_thread(c, "1")); settle(); CHECK(v->lifetime == lifetime && tb_stack_declaration(s, 0));
    command("-freuse", NULL); settle(); CHECK(v->lifetime != lifetime && !tb_stack_declaration(s, 0) && !v->sample.sequence);
    CHECK(tb_stack_follow(s, 0)); command("-fmode", "3"); tb_context_refresh(c); settle();
    CHECK(!v->anchor_valid && !v->positioned); idle(); /* Never retry unavailable metadata at idle. */
    CHECK(tb_stack_navigate(s, 0, "0x1040")); CHECK(!tb_stack_navigate(s, 0, "+0"));
    command("-fmode", "0"); CHECK(tb_stack_follow(s, 0)); settle(); CHECK(v->anchor_valid);
    command("-fmode", "2"); CHECK(tb_stack_read(s, 0)); pump(); pump();
    CHECK(tb_memory_client_busy(m, tb_memory_stack_client(0))); tb_stack_cancel(s, 0); CHECK(v->cancel_requested);
    command("-frelease", NULL); settle(); CHECK(!v->reading && !v->cancel_requested && !v->sample.sequence);
    CHECK(tb_stack_read(s, 0)); until = tb_gdb_now_ms() + 3500; while (tb_gdb_now_ms() < until && v->reading) { pump(); }
    CHECK(!v->reading && strstr(v->message, "timed out")); command("-frelease", NULL); settle(); CHECK(!v->sample.sequence);
    command("-fmode", "0"); CHECK(tb_stack_read(s, 0)); settle(); CHECK(v->sample.sequence);
    CHECK(tb_memory_range_set(m, tb_memory_stack_client(0), 0, 0x1040, 0x10c0, TB_MEMORY_MMIO, 32, "Unknown effects"));
    CHECK(tb_stack_read(s, 0)); settle(); CHECK(strstr(v->message, "generic byte reads are blocked")); CHECK(v->sample.sequence);
    CHECK(tb_memory_range(m, 0, 0)->used == false); /* Independent policy clients. */
    CHECK(tb_stack_navigate(s, 0, "0x3000")); command("-exec-continue", NULL); pump(); CHECK(tb_stack_stale(s, 0));
    command("-fstop", NULL); settle(); CHECK(v->start == 0x3000 && !v->follow && v->anchor_valid); idle();
    tb_stack_target(s, true, 32, TB_MEMORY_BIG, "arm"); CHECK(!tb_stack_navigate(s, 0, "0xffffffff"));
    CHECK(tb_stack_navigate(s, 0, "0x3000")); CHECK(tb_stack_size(s, 0, 8));
    CHECK(tb_stack_read(s, 0)); settle(); tb_stack_line(s, 0, 0, text, sizeof text); CHECK(strstr(text, "0x00010203"));
    tb_stack_target(s, true, 32, TB_MEMORY_UNKNOWN, "arm"); CHECK(v->grouping == 1 && !tb_stack_group(s, 0, 2));
    tb_stack_clear(s, 0); CHECK(!v->sample.sequence && !v->point_count && !tb_stack_cached(s, 0, -1));
    CHECK(tb_browse_allocated(cache) <= TB_BROWSE_LIMIT);
    tb_gdb_close(g); until = tb_gdb_now_ms() + 3000; while (tb_gdb_state(g) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < until) { pump(); }
    CHECK(tb_gdb_state(g) == TB_GDB_DISCONNECTED); tb_stack_destroy(s); tb_memory_destroy(m); tb_context_destroy(c); tb_browse_release(cache); CHECK(tb_gdb_destroy(g));
    puts("PASS F02 synthetic MI: shared frame-0 SP, explicit bounded bytes, read-free navigation, partial groups, annotations/no-call guard, thread lifetimes/history, declarations, cancellation/timeout and independent Memory policy");
    return 0;
}
