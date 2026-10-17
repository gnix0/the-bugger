#include "controller.h"
#include "../support/check.h"

#include <string.h>

typedef struct { bool ready, features, async, all_stop, exit_reply; size_t diagnostics, exits; } Evidence;

static void emit(void *context, const TbGdbEvent *event)
{
    Evidence *evidence = context;
    if (event->kind == TB_GDB_REPLY && event->cookie == 1) {
        const TbMiValue *features = tb_mi_find(event->record->results, "features");
        CHECK(strcmp(event->record->klass, "done") == 0 && features != NULL && features->type == TB_MI_LIST);
        evidence->features = true;
    } else if (event->kind == TB_GDB_REPLY && event->cookie == 2) {
        CHECK(strcmp(event->record->klass, "exit") == 0);
        evidence->exit_reply = true;
    } else if (event->kind == TB_GDB_REPLY && (event->cookie == 3 || event->cookie == 4)) {
        const TbMiValue *value = tb_mi_find(event->record->results, "value");
        CHECK(value && value->type == TB_MI_STRING);
        CHECK(strcmp(value->string.data, event->cookie == 3 ? "on" : "off") == 0);
        if (event->cookie == 3) { evidence->async = true; }
        else { evidence->all_stop = true; }
    } else if (event->kind == TB_GDB_DIAGNOSTIC) {
        ++evidence->diagnostics;
        fprintf(stderr, "%.*s (errno %d)\n", (int)event->length, event->text, event->status);
    } else if (event->kind == TB_GDB_CHILD_EXIT) { ++evidence->exits; }
}

int main(int argc, char *argv[])
{
    CHECK(argc == 2);
    Evidence evidence = {0};
    TbGdb *gdb = tb_gdb_create(emit, &evidence);
    CHECK(gdb != NULL && tb_gdb_start(gdb, argv[1], 2000));
    uint64_t deadline = tb_gdb_now_ms() + 5000;
    bool submitted_exit = false, submitted_queries = false;
    while (tb_gdb_state(gdb) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < deadline) {
        tb_gdb_pump(gdb, 20);
        if (tb_gdb_state(gdb) == TB_GDB_READY && !evidence.ready) {
            evidence.ready = true;
            CHECK(tb_gdb_request(gdb, "-list-features", NULL, 0, TB_GDB_SESSION, 1, 1500, NULL));
        }
        if (evidence.features && !submitted_queries) {
            const char *const async[] = {"mi-async"}, *const all_stop[] = {"non-stop"};
            CHECK(tb_gdb_request(gdb, "-gdb-show", async, 1, TB_GDB_SESSION, 3, 1500, NULL));
            CHECK(tb_gdb_request(gdb, "-gdb-show", all_stop, 1, TB_GDB_SESSION, 4, 1500, NULL));
            submitted_queries = true;
        }
        if (evidence.async && evidence.all_stop && !submitted_exit) {
            CHECK(tb_gdb_request(gdb, "-gdb-exit", NULL, 0, TB_GDB_SESSION, 2, 1500, NULL));
            submitted_exit = true;
        }
    }
    if (tb_gdb_state(gdb) != TB_GDB_DISCONNECTED) {
        tb_gdb_close(gdb);
        uint64_t cleanup = tb_gdb_now_ms() + 2000;
        while (tb_gdb_state(gdb) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < cleanup) { tb_gdb_pump(gdb, 20); }
    }
    CHECK(evidence.ready && evidence.features && evidence.async && evidence.all_stop && evidence.exit_reply && evidence.exits == 1);
    CHECK(evidence.diagnostics == 0 && tb_gdb_pid(gdb) == 0 && tb_gdb_destroy(gdb));
    printf("PASS M04 actual %s startup, async/all-stop negotiation, features and reaped MI exit; no target\n", argv[1]);
    return 0;
}
