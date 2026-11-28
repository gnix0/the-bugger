#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Close-on-exec descriptor copies. */
#include "symbol/companion_load.h"
#include "symbol/protocol.h"
#include "symbol/worker.h"
#include "../support/check.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    unsigned char bytes[16384];
    size_t        length;
} Arguments_t;

typedef struct {
    TbSymbolHelper_t           *helper;
    TbSymbolArtifacts_t        *artifacts;
    TbSymbolCompanions_t       *companions;
    TbSymbolCompanionLoad_t    *load;
    TbSymbolWorkIdentity_t      identity;
    TbSymbolHelperResult_t      result;
    unsigned char              bytes[16384];
    uint64_t                   generation;
    uint64_t                   primary;
    bool                       complete;
    bool                       released;
    bool                       offered;
    bool                       cancel_at_offer;
} Fixture_t;

static bool emit(void *context, const TbSymbolWorkIdentity_t *identity, TbSymbolHelperResult_t result)
{
    Fixture_t *fixture = context;
    CHECK(!fixture->complete && identity->request == fixture->identity.request);
    CHECK(identity->consumer == fixture->identity.consumer &&
          identity->revision == fixture->identity.revision &&
          identity->generation == fixture->identity.generation);
    CHECK(result.length <= sizeof(fixture->bytes));
    fixture->result = result;
    if (result.length)
        memcpy(fixture->bytes, result.payload, result.length);
    fixture->result.payload = fixture->bytes;
    if (result.descriptor >= 0) {
        CHECK((fcntl(result.descriptor, F_GETFD) & FD_CLOEXEC) != 0);
        fixture->result.descriptor = fcntl(result.descriptor, F_DUPFD_CLOEXEC, 3);
        CHECK(fixture->result.descriptor >= 0);
    }
    fixture->complete = true;
    return true;
}


static bool released(void *context, const TbSymbolWorkIdentity_t *identity, uint64_t generation)
{
    Fixture_t *fixture = context;
    CHECK(fixture->complete && !fixture->released && identity->request == fixture->identity.request);
    CHECK(generation == fixture->generation);
    fixture->released = true;
    return true;
}


static void pump(Fixture_t *fixture)
{
    tb_symbol_helper_pump_with_release(fixture->helper, emit, released, fixture);
    struct pollfd fds[2];
    size_t count = tb_symbol_helper_pollfds(fixture->helper, fds);
    CHECK(poll(fds, (nfds_t)count, tb_symbol_helper_timeout(fixture->helper, 2)) >= 0);
}


static void start(Fixture_t *fixture, const char *executable)
{
    TbSymbolHelperConfig_t config;
    tb_symbol_helper_defaults(&config);
    config.decoder_bytes = 1024 * 1024;
    config.checkpoint_bytes = 64;
    config.checkpoint_steps = 1;
    fixture->helper = tb_symbol_helper_create(&config, NULL);
    CHECK(fixture->helper && tb_symbol_helper_start(fixture->helper, executable));
    for (unsigned turn = 0; turn < 4000 &&
         tb_symbol_helper_state(fixture->helper) != TB_SYMBOL_HELPER_READY; ++turn) {
        pump(fixture);
    }
    CHECK(tb_symbol_helper_state(fixture->helper) == TB_SYMBOL_HELPER_READY);
    fixture->generation = tb_symbol_helper_process_generation(fixture->helper);
}


static void stop(Fixture_t *fixture)
{
    tb_symbol_helper_close(fixture->helper);
    for (unsigned turn = 0; turn < 4000; ++turn) {
        if (tb_symbol_helper_destroy(fixture->helper)) {
            fixture->helper = NULL;
            return;
        }
        pump(fixture);
    }
    CHECK(false);
}


static void word(Arguments_t *arguments, uint64_t value)
{
    CHECK(arguments->length + 8 <= sizeof(arguments->bytes));
    tb_symbol_protocol_put(arguments->bytes + arguments->length, value, 8);
    arguments->length += 8;
}


static void paths(Arguments_t *arguments, const char *first, const char *second)
{
    size_t left = strlen(first) + 1;
    size_t right = second ? strlen(second) + 1 : 0;
    size_t prefix = second ? 8 : 4;
    CHECK(arguments->length + prefix + left + right <= sizeof(arguments->bytes));
    tb_symbol_protocol_put(arguments->bytes + arguments->length, left, 4);
    if (second)
        tb_symbol_protocol_put(arguments->bytes + arguments->length + 4, right, 4);
    arguments->length += prefix;
    memcpy(arguments->bytes + arguments->length, first, left);
    arguments->length += left;
    if (second) {
        memcpy(arguments->bytes + arguments->length, second, right);
        arguments->length += right;
    }
}


static void submit(Fixture_t *fixture, TbSymbolHelperRequest_t request)
{
    fixture->complete = false;
    fixture->released = false;
    fixture->offered = false;
    fixture->result = (TbSymbolHelperResult_t) {
        .descriptor = -1
    };
    CHECK(!tb_symbol_helper_jobs(fixture->helper));
    CHECK(tb_symbol_helper_submit(fixture->helper, &request) == TB_SYMBOL_ADMITTED);
}


static void wait_result(Fixture_t *fixture)
{
    for (unsigned turn = 0; turn < 12000 && !fixture->released; ++turn) {
        pump(fixture);
        TbSymbolAcquireOffer_t offer;
        if (!fixture->offered && tb_symbol_helper_offer(fixture->helper, fixture->identity, &offer)) {
            fixture->offered = true;
            if (fixture->cancel_at_offer) {
                CHECK(fixture->load && tb_symbol_companion_load_cancel(fixture->load));
                fixture->cancel_at_offer = false;
            }
            TbSymbolOutcome_e outcome = fixture->load ?
                tb_symbol_companion_load_offer(fixture->load, fixture->identity, fixture->generation, offer) :
                tb_symbol_artifact_offer(fixture->artifacts, fixture->primary, offer);
            if (outcome == TB_SYMBOL_SUCCESS)
                CHECK(tb_symbol_helper_grant(fixture->helper, fixture->identity, offer.bytes));
            else
                CHECK(tb_symbol_helper_cancel(fixture->helper, fixture->identity));
        }
    }
    if (!fixture->released) {
        fprintf(stderr, "attachment request %llu incomplete: %s\n",
                (unsigned long long)fixture->identity.request, tb_symbol_helper_error(fixture->helper));
    }
    CHECK(fixture->complete && fixture->released && !tb_symbol_helper_jobs(fixture->helper));
}


static void operation(Fixture_t *fixture, uint32_t code, Arguments_t arguments)
{
    ++fixture->identity.request;
    submit(fixture, (TbSymbolHelperRequest_t) {
        .identity  = fixture->identity,
        .operation = code,
        .priority  = TB_SYMBOL_CONSTRUCTION,
        .heavy     = true,
        .arguments = arguments.bytes,
        .length    = arguments.length
    });
    wait_result(fixture);
    CHECK(fixture->result.outcome == TB_SYMBOL_SUCCESS);
}


static void load_job(Fixture_t *fixture)
{
    Arguments_t arguments = {0};
    TbSymbolHelperRequest_t request;
    ++fixture->identity.request;
    CHECK(tb_symbol_companion_load_request(fixture->load, fixture->identity, fixture->generation,
          arguments.bytes, sizeof(arguments.bytes), &request) == TB_SYMBOL_SUCCESS);
    submit(fixture, request);
    wait_result(fixture);
    (void)tb_symbol_companion_load_finish(fixture->load, fixture->identity,
        fixture->generation, &fixture->result);
    CHECK(fixture->result.descriptor < 0);
}


static void remove_backing(Fixture_t *fixture, const char *pool, TbSymbolStorageKey_t root, uint64_t content)
{
    TbSymbolArtifactCleanup_t cleanup;
    CHECK(tb_symbol_artifact_evict(fixture->artifacts, content, &cleanup));
    Arguments_t arguments = {0};
    word(&arguments, cleanup.slot);
    word(&arguments, root.device);
    word(&arguments, root.inode);
    word(&arguments, cleanup.key.device);
    word(&arguments, cleanup.key.inode);
    paths(&arguments, pool, NULL);
    operation(fixture, TB_SYMBOL_OPERATION_REMOVE_FILE, arguments);
    CHECK(tb_symbol_artifact_backing_cleaned(fixture->artifacts, cleanup));
}


static TbSymbolCompanionFact_t *open_primary(Fixture_t *fixture, const char *origin,
    const char *pool, TbSymbolStorageKey_t root)
{
    fixture->primary = tb_symbol_artifact_prepare(fixture->artifacts, origin);
    CHECK(fixture->primary);
    Arguments_t arguments = {0};
    word(&arguments, 256 * 1024 * 1024);
    word(&arguments, fixture->primary);
    word(&arguments, root.device);
    word(&arguments, root.inode);
    paths(&arguments, origin, pool);
    operation(fixture, TB_SYMBOL_OPERATION_ACQUIRE, arguments);
    CHECK(fixture->result.length == 56 && fixture->result.descriptor >= 0);
    TbSymbolAcquiredBytes_t acquired = {
        .bytes      = tb_symbol_protocol_get(fixture->bytes, 8),
        .descriptor = fixture->result.descriptor,
        .key = {
            .device = tb_symbol_protocol_get(fixture->bytes + 8, 8),
            .inode  = tb_symbol_protocol_get(fixture->bytes + 16, 8)
        }
    };
    memcpy(acquired.digest, fixture->bytes + 24, sizeof(acquired.digest));
    CHECK(tb_symbol_artifact_accept(fixture->artifacts, fixture->primary, &acquired) == TB_SYMBOL_SUCCESS);
    fixture->result.descriptor = -1;
    CHECK(tb_symbol_artifact_publish(fixture->artifacts, fixture->primary));

    TbSymbolCompanionInspectionPlan_t plan;
    TbSymbolCompanionInspection_t *inspection;
    ++fixture->identity.request;
    CHECK(tb_symbol_companion_inspect_begin(fixture->companions, fixture->primary, fixture->identity,
          fixture->generation, &plan, &inspection) == TB_SYMBOL_SUCCESS);
    arguments = (Arguments_t) {0};
    word(&arguments, plan.capture.backing.bytes);
    word(&arguments, plan.capture.slot);
    word(&arguments, root.device);
    word(&arguments, root.inode);
    word(&arguments, plan.capture.backing.key.device);
    word(&arguments, plan.capture.backing.key.inode);
    word(&arguments, 4096);
    word(&arguments, 4096);
    paths(&arguments, pool, NULL);
    submit(fixture, (TbSymbolHelperRequest_t) {
        .identity  = fixture->identity,
        .operation = TB_SYMBOL_OPERATION_COMPANION_INSPECT,
        .priority  = TB_SYMBOL_CONSTRUCTION,
        .heavy     = true,
        .arguments = arguments.bytes,
        .length    = arguments.length
    });
    wait_result(fixture);
    TbSymbolCompanionFact_t *fact = NULL;
    CHECK(fixture->result.descriptor < 0);
    CHECK(tb_symbol_companion_inspect_finish(inspection, fixture->identity, fixture->generation,
          fixture->result.outcome, fixture->bytes, fixture->result.length, &fact) == TB_SYMBOL_SUCCESS);

    return fact;
}


static void begin_load(Fixture_t *fixture, TbSymbolCompanionFact_t *fact, const char *candidate,
    const char *pool, TbSymbolStorageKey_t root, TbSymbolCompanionMatch_e method)
{
    CHECK(tb_symbol_companion_load_begin(fixture->companions, fact, (TbSymbolSourcePathText_t) {
        .data  = (const unsigned char *)candidate,
        .bytes = strlen(candidate)
    }, method, (TbSymbolCompanionStorage_t) {
        .path       = pool,
        .key        = root,
        .file_bytes = 256 * 1024 * 1024
    }, &fixture->load) == TB_SYMBOL_SUCCESS);
}


static void destroy_load(Fixture_t *fixture)
{
    CHECK(tb_symbol_companion_load_info(fixture->load).phase == TB_SYMBOL_COMPANION_LOAD_DONE);
    CHECK(tb_symbol_companion_load_destroy(fixture->load));
    fixture->load = NULL;
}


int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--symbol-worker") == 0)
        return tb_symbol_worker_main(3);
    CHECK(argc == 4);

    char parent[] = "/tmp/tb-companion-attach-XXXXXX";
    CHECK(mkdtemp(parent));
    char pool[4096];
    CHECK(snprintf(pool, sizeof(pool), "%s/pool", parent) > 0);
    TbSymbolBudget_t frontend;
    TbSymbolBudget_t evidence;
    TbSymbolBudget_t construction;
    CHECK(tb_symbol_budget_init(&frontend, 1024 * 1024));
    CHECK(tb_symbol_budget_init_child(&evidence, 128 * 1024, &frontend));
    CHECK(tb_symbol_budget_init_child(&construction, 128 * 1024, &frontend));
    TbSymbolArtifactIds_t artifact_ids = {0};
    TbSymbolCompanionIds_t companion_ids = {0};
    Fixture_t fixture = {
        .identity = {
            .consumer   = 3,
            .revision   = 7,
            .generation = 11
        }
    };
    fixture.artifacts = tb_symbol_artifacts_create(256 * 1024 * 1024, 1024 * 1024 * 1024,
        &artifact_ids, &frontend);
    fixture.companions = tb_symbol_companions_create(fixture.artifacts, &companion_ids,
        (TbSymbolCompanionBudgets_t) {
            .frontend     = &frontend,
            .evidence     = &evidence,
            .construction = &construction
        }, (TbSymbolCompanionLimits_t) {
            .facts       = 8,
            .inspections = 2,
            .attachments = 4,
            .scopes      = 8
        });
    CHECK(fixture.artifacts && fixture.companions);
    start(&fixture, argv[1]);
    Arguments_t arguments = {0};
    paths(&arguments, pool, NULL);
    operation(&fixture, TB_SYMBOL_OPERATION_CREATE_STORAGE, arguments);
    CHECK(fixture.result.length == 16);
    TbSymbolStorageKey_t root = {
        .device = tb_symbol_protocol_get(fixture.bytes, 8),
        .inode  = tb_symbol_protocol_get(fixture.bytes + 8, 8)
    };
    TbSymbolCompanionFact_t *primary = open_primary(&fixture, argv[2], pool, root);
    TbSymbolArtifactCapture_t capture;
    TbSymbolCompanionInventory_t inventory;
    const char *origin;
    CHECK(tb_symbol_companion_fact_view(primary, &capture, &inventory, &origin));
    CHECK(strcmp(origin, argv[2]) == 0 && inventory.name_bytes && inventory.id_bytes);
    uint64_t original_bytes = tb_symbol_artifacts_usage(fixture.artifacts).bytes;
    TbSymbolCompanionScopeCapture_t *old_scope;
    CHECK(tb_symbol_companion_scope_capture(fixture.companions, fixture.primary,
          TB_SYMBOL_COMPANION_ORIGIN_ALL, &old_scope) == TB_SYMBOL_SUCCESS);

    /* Missing, mismatching and cancelled candidates leave the primary scope intact. */
    for (unsigned mode = 0; mode < 3; ++mode) {
        begin_load(&fixture, primary, mode == 0 ? "/nonexistent-tb-companion" : mode == 1 ? argv[2] : argv[3],
            pool, root, TB_SYMBOL_COMPANION_BY_DEBUGLINK);
        fixture.cancel_at_offer = mode == 2;
        load_job(&fixture);
        if (mode == 1) {
            load_job(&fixture);
            CHECK(tb_symbol_companion_load_commit(fixture.load) == TB_SYMBOL_UNSUPPORTED);
            CHECK(tb_symbol_companion_load_info(fixture.load).relation ==
                  TB_SYMBOL_COMPANION_RELATION_CRC_MISMATCH);
        }
        CHECK(tb_symbol_companion_load_info(fixture.load).phase == TB_SYMBOL_COMPANION_LOAD_CLEANUP);
        load_job(&fixture);
        destroy_load(&fixture);
        CHECK(tb_symbol_companion_scope_current(old_scope));
        CHECK(tb_symbol_artifacts_usage(fixture.artifacts).bytes == original_bytes);
    }

    begin_load(&fixture, primary, argv[3], pool, root, TB_SYMBOL_COMPANION_BY_DEBUGLINK);
    load_job(&fixture);
    load_job(&fixture);
    CHECK(tb_symbol_companion_load_info(fixture.load).phase == TB_SYMBOL_COMPANION_LOAD_READY);
    CHECK(tb_symbol_companion_scope_current(old_scope));
    CHECK(tb_symbol_companion_load_commit(fixture.load) == TB_SYMBOL_SUCCESS);
    uint64_t relationship = tb_symbol_companion_load_info(fixture.load).relationship;
    destroy_load(&fixture);
    CHECK(!tb_symbol_companion_scope_current(old_scope) && tb_symbol_companion_scope_count(old_scope) == 1);
    TbSymbolCompanionScopeCapture_t *combined;
    CHECK(tb_symbol_companion_scope_capture(fixture.companions, fixture.primary,
          TB_SYMBOL_COMPANION_ORIGIN_ALL, &combined) == TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_companion_scope_count(combined) == 2);
    TbSymbolCompanionScopeMember_t member;
    CHECK(tb_symbol_companion_scope_member(combined, 1, &member));
    CHECK(member.relationship == relationship && member.capture.content != capture.content &&
          member.capture.context != capture.context &&
          member.capture.backing.key.inode != capture.backing.key.inode);
    CHECK(tb_symbol_artifacts_usage(fixture.artifacts).bytes ==
          original_bytes + member.capture.backing.bytes);

    /* Replacing the helper cannot revoke independently owned valid metadata. */
    stop(&fixture);
    CHECK(tb_symbol_companion_scope_current(combined) && tb_symbol_companion_fact_current(primary));
    start(&fixture, argv[1]);
    CHECK(tb_symbol_companion_scope_current(combined));
    TbSymbolCompanionRetired_t retired;
    CHECK(tb_symbol_companion_detach(fixture.companions, fixture.primary, &retired) == TB_SYMBOL_SUCCESS);
    CHECK(retired.relationship == relationship && retired.content == member.capture.content);
    CHECK(!tb_symbol_companion_scope_current(combined));
    TbSymbolArtifactCleanup_t blocked_cleanup;
    CHECK(!tb_symbol_artifact_evict(fixture.artifacts, retired.content, &blocked_cleanup));
    tb_symbol_companion_scope_release(combined);
    tb_symbol_companion_scope_release(old_scope);
    remove_backing(&fixture, pool, root, retired.content);

    /* A closed primary immediately invalidates its installed dependent inventory. */
    begin_load(&fixture, primary, argv[3], pool, root, TB_SYMBOL_COMPANION_BY_BUILD_ID);
    load_job(&fixture);
    load_job(&fixture);
    CHECK(tb_symbol_companion_load_commit(fixture.load) == TB_SYMBOL_SUCCESS);
    destroy_load(&fixture);
    CHECK(tb_symbol_artifact_close(fixture.artifacts, fixture.primary));
    CHECK(!tb_symbol_companion_fact_current(primary));
    bool found = false;
    CHECK(tb_symbol_companion_collect(fixture.companions, &retired, &found) == TB_SYMBOL_SUCCESS && found);
    tb_symbol_companion_fact_release(primary);
    remove_backing(&fixture, pool, root, retired.content);
    remove_backing(&fixture, pool, root, capture.content);
    CHECK(tb_symbol_artifacts_usage(fixture.artifacts).bytes == 0);
    CHECK(tb_symbol_companions_destroy(fixture.companions));
    CHECK(tb_symbol_artifacts_destroy(fixture.artifacts));
    CHECK(frontend.used == 0 && evidence.used == 0 && construction.used == 0);
    arguments = (Arguments_t) {0};
    word(&arguments, root.device);
    word(&arguments, root.inode);
    paths(&arguments, pool, NULL);
    operation(&fixture, TB_SYMBOL_OPERATION_REMOVE_STORAGE, arguments);
    stop(&fixture);
    CHECK(rmdir(parent) == 0);
    puts("PASS supervised companion attachment, failed candidates, cancellation, restart, detach and close");
    return 0;
}
