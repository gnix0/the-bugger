#define _POSIX_C_SOURCE 200809L
#include "controller.h"
#include "monitor.h"
#include "../support/check.h"
#include <string.h>
#include <unistd.h>

/* Opt-in physical transport test: connects (which may halt firmware), emits
 * only Tcl constants/metadata/sleep/error, then disconnects. No memory read,
 * flash, reset, target write or resume command is sent by this test. */
typedef struct {
    TbMonitorFrame frame;
    uint64_t token;
    bool reply, done, timeout, late, framing;
    unsigned requests;
} Probe;
static void emit(void *context, const TbGdbEvent *event)
{
    Probe *p = context;
    if (p->framing && event->kind == TB_GDB_RECORD && event->record && event->record->kind == TB_MI_TARGET) {
        const TbMiString *s = &event->record->stream;
        for (size_t i = 0; i < s->length; ++i) { tb_monitor_feed(&p->frame, s->data + i, 1); }
    }
    if (event->cookie == 19901 && event->token == p->token) {
        if (event->kind == TB_GDB_REPLY) {
            p->reply = true; p->done = event->record && strcmp(event->record->klass, "done") == 0;
            if (event->record && strcmp(event->record->klass, "connected") == 0) { p->done = true; }
        } else if (event->kind == TB_GDB_TIMEOUT) { p->timeout = true; }
    }
    if (p->timeout && event->kind == TB_GDB_STALE && event->record && event->record->kind == TB_MI_RESULT && event->record->token) {
        char token[32]; (void)snprintf(token, sizeof token, "%llu", (unsigned long long)p->token);
        if (strcmp(event->record->token, token) == 0) {
            p->late = true; p->done = strcmp(event->record->klass, "done") == 0;
        }
    }
}
static void request(TbGdb *gdb, Probe *p, const char *operation, const char *const args[], size_t count, unsigned timeout)
{
    CHECK(!tb_gdb_pending(gdb)); p->reply = p->done = p->timeout = p->late = false;
    CHECK(tb_gdb_request(gdb, operation, args, count, TB_GDB_SESSION, 19901, timeout, &p->token)); ++p->requests;
    uint64_t end = tb_gdb_now_ms() + 4000;
    while (!p->reply && !p->timeout && tb_gdb_now_ms() < end) { tb_gdb_pump(gdb, 2); }
    CHECK(p->reply || p->timeout);
}
static void framing(TbGdb *gdb, Probe *p, unsigned variant)
{
    char id[64], script[1024], command[1100];
    (void)snprintf(id, sizeof id, "live-%lu-%u", (unsigned long)getpid(), variant);
    CHECK(tb_monitor_start(&p->frame, id, "stm32f4x.cpu", 0x20000000, 32)); p->framing = true;
    const char *address = variant == 4 ? "20000004" : "20000000";
    unsigned width = variant == 5 ? 16 : 32, count = variant == 6 ? 2 : 1;
    const char *value = variant == 7 ? "100000000" : "12345678";
    if (variant == 2) {
        (void)snprintf(script, sizeof script,
            "echo {TBM1|%s|stm32f4x.cpu|20000000|32|1|12345678|END}; echo {TBM1|%s|stm32f4x.cpu|20000000|32|1|12345678|END}", id, id);
    } else if (variant == 8) { (void)snprintf(script, sizeof script, "error TB_EXPECTED_ERROR"); }
    else {
        (void)snprintf(script, sizeof script, "%sformat {TBM1|%s|%%s|%s|%u|%u|%s|END} %s",
            variant == 1 ? "echo TB_UNRELATED; " : variant == 10 ? "sleep 200; " : "", id, address, width, count, value, variant == 3 ? "other.cpu" : "[target current]");
    }
    (void)snprintf(command, sizeof command, "monitor %s", script);
    const char *args[] = {"console", command};
    request(gdb, p, "-interpreter-exec", args, 2, variant == 10 ? 20 : 2000);
    uint64_t result = 77;
    if (variant == 10) {
        CHECK(p->timeout && !p->reply && !tb_monitor_finish(&p->frame, false, &result));
        /* No retry or next request before the exact late token reconciles. */
        unsigned before = p->requests; uint64_t end = tb_gdb_now_ms() + 3000;
        while (!p->late && tb_gdb_now_ms() < end) { tb_gdb_pump(gdb, 2); }
        CHECK(p->late && p->requests == before && tb_monitor_finish(&p->frame, p->done, &result));
    } else {
        bool expected = variant == 0 || variant == 1 || variant == 9;
        CHECK(!p->timeout && tb_monitor_finish(&p->frame, p->done, &result) == expected);
        CHECK(result == (expected ? 0x12345678 : 77));
    }
    printf("PASS real controller/OpenOCD framing case %u, MI token %llu%s\n", variant, (unsigned long long)p->token,
           variant == 10 ? ", timed out then reconciled without another request" : "");
    p->framing = false;
}
int main(int argc, char **argv)
{
    CHECK(argc == 3 && *argv[2] && strspn(argv[2], "0123456789.:[]") == strlen(argv[2]));
    Probe p = {0}; TbGdb *gdb = tb_gdb_create(emit, &p); CHECK(gdb);
    CHECK(tb_gdb_start(gdb, argv[1], 3000)); uint64_t end = tb_gdb_now_ms() + 4000;
    while (tb_gdb_state(gdb) != TB_GDB_READY && tb_gdb_now_ms() < end) { tb_gdb_pump(gdb, 2); }
    CHECK(tb_gdb_state(gdb) == TB_GDB_READY);
    const char *connect[] = {"extended-remote", argv[2]}; request(gdb, &p, "-target-select", connect, 2, 3000); CHECK(p.done);
    for (unsigned i = 0; i <= 10; ++i) { framing(gdb, &p, i); }
    framing(gdb, &p, 9); /* Recovery after timeout, only after reconciliation. */
    request(gdb, &p, "-target-disconnect", NULL, 0, 2000); CHECK(p.done);
    tb_gdb_close(gdb); end = tb_gdb_now_ms() + 4000;
    while (tb_gdb_state(gdb) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < end) { tb_gdb_pump(gdb, 2); }
    CHECK(tb_gdb_state(gdb) == TB_GDB_DISCONNECTED && tb_gdb_destroy(gdb));
    puts("PASS physical text-only protocol probe; no peripheral-memory access or width validation claimed"); return 0;
}
