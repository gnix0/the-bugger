#include "gdb/controller.h"
#include "symbol/helper.h"
#include "../support/check.h"

#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    unsigned helper_failures;
    unsigned replies;
    unsigned diagnostics;
    unsigned debugger_exits;
} Evidence_t;

static bool helper_emit(void *context, const TbSymbolWorkIdentity_t *identity, TbSymbolHelperResult_t result)
{
    Evidence_t *evidence = context;
    CHECK(identity->request == 2 && identity->consumer == 7 && identity->revision == 17 &&
          identity->generation == 5);
    CHECK(result.outcome == TB_SYMBOL_OPERATIONAL_FAILURE && !result.length && result.descriptor < 0);
    ++evidence->helper_failures;
    return true;
}


static void gdb_emit(void *context, const TbGdbEvent_t *event)
{
    Evidence_t *evidence = context;
    if (event->kind == TB_GDB_REPLY && event->cookie == 44) {
        CHECK(event->record && strcmp(event->record->klass, "done") == 0);
        const TbMiValue_t *features = tb_mi_find(event->record->results, "features");
        CHECK(features && features->type == TB_MI_LIST);
        ++evidence->replies;
    } else if (event->kind == TB_GDB_DIAGNOSTIC) {
        ++evidence->diagnostics;
    } else if (event->kind == TB_GDB_CHILD_EXIT) {
        ++evidence->debugger_exits;
    }
}


static void pump(TbGdb_t *gdb, TbSymbolHelper_t *helper, Evidence_t *evidence)
{
    tb_gdb_pump(gdb, 0);
    tb_symbol_helper_pump(helper, helper_emit, evidence);
    struct pollfd fds[5];
    size_t count = tb_gdb_pollfds(gdb, fds);
    count += tb_symbol_helper_pollfds(helper, fds + count);
    int timeout = tb_symbol_helper_timeout(helper, tb_gdb_timeout(gdb, 10));
    CHECK(poll(fds, (nfds_t)count, timeout) >= 0 || errno == EINTR);
}


int main(int argc, char *argv[])
{
    CHECK(argc == 3);
    Evidence_t evidence = {0};
    TbGdb_t *gdb = tb_gdb_create(gdb_emit, &evidence);
    CHECK(gdb && tb_gdb_start(gdb, argv[1], 2000));
    TbSymbolHelperConfig_t config;
    tb_symbol_helper_defaults(&config);
    TbSymbolHelper_t *helper = tb_symbol_helper_create(&config, NULL);
    CHECK(setenv("TB_TEST_SYMBOL_HELPER", "crash", 1) == 0);
    CHECK(helper && tb_symbol_helper_start(helper, argv[2]));
    uint64_t deadline = tb_gdb_now_ms() + 10000;
    while ((tb_gdb_state(gdb) != TB_GDB_READY ||
            tb_symbol_helper_state(helper) != TB_SYMBOL_HELPER_READY) && tb_gdb_now_ms() < deadline) {
        pump(gdb, helper, &evidence);
    }
    CHECK(tb_gdb_state(gdb) == TB_GDB_READY && tb_symbol_helper_state(helper) == TB_SYMBOL_HELPER_READY);
    pid_t debugger_pid = tb_gdb_pid(gdb);
    TbSymbolHelperRequest_t request = {
        .identity = {
            .request    = 2,
            .consumer   = 7,
            .revision   = 17,
            .generation = 5
        },
        .operation = 900,
        .priority  = TB_SYMBOL_CONSTRUCTION,
        .heavy     = true
    };
    CHECK(tb_symbol_helper_submit(helper, &request) == TB_SYMBOL_ADMITTED);
    while (!evidence.helper_failures && tb_gdb_now_ms() < deadline)
        pump(gdb, helper, &evidence);
    CHECK(evidence.helper_failures == 1 && tb_symbol_helper_state(helper) == TB_SYMBOL_HELPER_UNAVAILABLE);
    CHECK(tb_gdb_pid(gdb) == debugger_pid && tb_gdb_state(gdb) == TB_GDB_READY);
    CHECK(tb_gdb_request(gdb, "-list-features", NULL, 0, TB_GDB_SESSION, 44, 1500, NULL));
    while (!evidence.replies && tb_gdb_now_ms() < deadline)
        pump(gdb, helper, &evidence);
    CHECK(evidence.replies == 1 && !evidence.diagnostics && !evidence.debugger_exits);
    CHECK(tb_gdb_pid(gdb) == debugger_pid);
    tb_symbol_helper_close(helper);
    tb_gdb_close(gdb);
    while (tb_gdb_state(gdb) != TB_GDB_DISCONNECTED && tb_gdb_now_ms() < deadline)
        pump(gdb, helper, &evidence);
    CHECK(tb_gdb_state(gdb) == TB_GDB_DISCONNECTED && evidence.debugger_exits == 1);
    while (!tb_symbol_helper_destroy(helper)) {
        CHECK(tb_gdb_now_ms() < deadline);
        pump(gdb, helper, &evidence);
    }
    CHECK(tb_gdb_destroy(gdb) && unsetenv("TB_TEST_SYMBOL_HELPER") == 0);
    puts("PASS symbol helper isolation: real GDB process survives helper crash "
         "and answers structured MI; no target");
    return 0;
}
