#define _POSIX_C_SOURCE 200809L
#include "symbol/companion_load.h"
#include "symbol/companion_receipt.h"
#include "symbol/protocol.h"
#include "symbol/sha256.h"
#include "../support/check.h"

#include <elf.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned allocation;
static unsigned fail_at;
static bool fail_revision;

void *__real_calloc(size_t count, size_t bytes);
void *__wrap_calloc(size_t count, size_t bytes);
uint64_t __real_tb_symbol_artifact_revise(TbSymbolArtifacts_t *owner, uint64_t context, uint64_t revision);
uint64_t __wrap_tb_symbol_artifact_revise(TbSymbolArtifacts_t *owner, uint64_t context, uint64_t revision);

void *__wrap_calloc(size_t count, size_t bytes)
{
    return ++allocation == fail_at ? NULL : __real_calloc(count, bytes);
}


uint64_t __wrap_tb_symbol_artifact_revise(TbSymbolArtifacts_t *owner, uint64_t context, uint64_t revision)
{
    return fail_revision ? 0 : __real_tb_symbol_artifact_revise(owner, context, revision);
}


typedef struct {
    char                  path[64];
    TbSymbolStorageInfo_t  backing;
    int                   descriptor;
} Frozen_t;

typedef struct {
    TbSymbolBudget_t         frontend;
    TbSymbolBudget_t         evidence;
    TbSymbolBudget_t         construction;
    TbSymbolArtifactIds_t    artifact_ids;
    TbSymbolCompanionIds_t   companion_ids;
    TbSymbolArtifacts_t     *artifacts;
    TbSymbolCompanions_t    *owner;
    uint64_t                request;
    uint64_t                primary;
    uint64_t                primary_content;
    Frozen_t                primary_file;
    TbSymbolCompanionFact_t *fact;
} Fixture_t;

static Frozen_t frozen(void)
{
    Frozen_t file = {0};
    CHECK(snprintf(file.path, sizeof(file.path), "/tmp/tb-companion-owner-XXXXXX") > 0);
    int writer = mkstemp(file.path);
    CHECK(writer >= 0 && write(writer, "abcd", 4) == 4 && fchmod(writer, 0400) == 0 && close(writer) == 0);
    file.descriptor = open(file.path, O_RDONLY | O_CLOEXEC);
    struct stat info;
    CHECK(file.descriptor >= 0 && fstat(file.descriptor, &info) == 0);
    file.backing = (TbSymbolStorageInfo_t) {
        .key = {
            .device = (uint64_t)info.st_dev,
            .inode  = (uint64_t)info.st_ino
        },
        .bytes = 4
    };

    return file;
}


static TbSymbolWorkIdentity_t request(Fixture_t *fixture)
{
    return (TbSymbolWorkIdentity_t) {
        .request    = ++fixture->request,
        .consumer   = 13,
        .revision   = 7,
        .generation = 11
    };
}


static TbSymbolAcquiredBytes_t acquired_bytes(Frozen_t file)
{
    TbSymbolAcquiredBytes_t bytes = {
        .bytes      = file.backing.bytes,
        .descriptor = file.descriptor,
        .key        = file.backing.key
    };
    TbSha256_t hash;
    CHECK(tb_sha256_init(&hash) && tb_sha256_update(&hash, "abcd", 4) &&
          tb_sha256_final(&hash, 4, bytes.digest));

    return bytes;
}


static size_t receipt(Frozen_t file, bool primary, unsigned char *bytes, size_t capacity)
{
    static const unsigned char id[] = {1, 2, 3, 4};
    TbSymbolCompanionInventory_t inventory = {
        .backing = file.backing,
        .header = {
            .byte_order    = TB_SYMBOL_LITTLE_ENDIAN,
            .address_bytes = 8,
            .object_type   = ET_EXEC,
            .machine       = EM_RISCV
        },
        .name              = primary ? (const unsigned char *)"candidate.debug" : NULL,
        .build_id          = id,
        .name_bytes        = primary ? 15 : 0,
        .id_bytes          = sizeof(id),
        .debuglink_section = primary ? 3 : UINT64_MAX,
        .build_id_section  = 2,
        .build_id_note     = 0,
        .debuglink_crc     = primary ? 9 : 0,
        .file_crc          = 9,
        .debuglink_known   = true
    };
    size_t count = tb_symbol_companion_receipt_size(&inventory);
    CHECK(count && count <= capacity && tb_symbol_companion_receipt_encode(&inventory, bytes, count));

    return count;
}


static TbSymbolCompanionFact_t *inspect(Fixture_t *fixture, uint64_t context, Frozen_t file)
{
    TbSymbolCompanionInspectionPlan_t plan;
    TbSymbolCompanionInspection_t *inspection;
    TbSymbolWorkIdentity_t identity = request(fixture);
    CHECK(tb_symbol_companion_inspect_begin(fixture->owner, context, identity, 3, &plan, &inspection) ==
          TB_SYMBOL_SUCCESS);
    CHECK(plan.capture.context == context && plan.capture.backing.key.inode == file.backing.key.inode);
    unsigned char bytes[256];
    size_t length = receipt(file, true, bytes, sizeof(bytes));
    TbSymbolCompanionFact_t *fact = NULL;
    TbSymbolWorkIdentity_t wrong = identity;
    ++wrong.revision;
    CHECK(tb_symbol_companion_inspect_finish(inspection, wrong, 3, TB_SYMBOL_SUCCESS, bytes, length, &fact) ==
          TB_SYMBOL_STALE);
    CHECK(!fact);
    CHECK(tb_symbol_companion_inspect_finish(inspection, identity, 4, TB_SYMBOL_SUCCESS,
          bytes, length, &fact) ==
          TB_SYMBOL_STALE);
    CHECK(tb_symbol_companion_inspect_finish(inspection, identity, 3, TB_SYMBOL_SUCCESS,
          bytes, length, &fact) ==
          TB_SYMBOL_SUCCESS);
    CHECK(fact && tb_symbol_companion_fact_current(fact));

    return fact;
}


static void start(Fixture_t *fixture)
{
    *fixture = (Fixture_t) {0};
    CHECK(tb_symbol_budget_init(&fixture->frontend, 1024 * 1024));
    CHECK(tb_symbol_budget_init_child(&fixture->evidence, 128 * 1024, &fixture->frontend));
    CHECK(tb_symbol_budget_init_child(&fixture->construction, 128 * 1024, &fixture->frontend));
    fixture->artifacts = tb_symbol_artifacts_create(16, 32, &fixture->artifact_ids, &fixture->frontend);
    fixture->owner = tb_symbol_companions_create(fixture->artifacts, &fixture->companion_ids,
        (TbSymbolCompanionBudgets_t) {
            .frontend     = &fixture->frontend,
            .evidence     = &fixture->evidence,
            .construction = &fixture->construction
        }, (TbSymbolCompanionLimits_t) {
            .facts       = 8,
            .inspections = 2,
            .attachments = 4,
            .scopes      = 4
        });
    CHECK(fixture->artifacts && fixture->owner);
    fixture->primary_file = frozen();
    fixture->primary = tb_symbol_artifact_prepare(fixture->artifacts, "/primary");
    CHECK(fixture->primary);
    CHECK(tb_symbol_artifact_offer(fixture->artifacts, fixture->primary, (TbSymbolAcquireOffer_t) {
        .bytes = 4,
        .key   = fixture->primary_file.backing.key
    }) == TB_SYMBOL_SUCCESS);
    TbSymbolAcquiredBytes_t bytes = acquired_bytes(fixture->primary_file);
    CHECK(tb_symbol_artifact_accept(fixture->artifacts, fixture->primary, &bytes) == TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_artifact_publish(fixture->artifacts, fixture->primary));
    TbSymbolArtifactInfo_t info;
    CHECK(tb_symbol_artifact_info(fixture->artifacts, fixture->primary, &info));
    fixture->primary_content = info.content;
    fixture->fact = inspect(fixture, fixture->primary, fixture->primary_file);
}


static void finish(Fixture_t *fixture)
{
    tb_symbol_companion_fact_release(fixture->fact);
    fixture->fact = NULL;
    TbSymbolArtifactInfo_t info;
    if (tb_symbol_artifact_info(fixture->artifacts, fixture->primary, &info))
        CHECK(tb_symbol_artifact_close(fixture->artifacts, fixture->primary));
    TbSymbolArtifactCleanup_t cleanup;
    CHECK(tb_symbol_artifact_evict(fixture->artifacts, fixture->primary_content, &cleanup));
    CHECK(unlink(fixture->primary_file.path) == 0);
    CHECK(tb_symbol_artifact_backing_cleaned(fixture->artifacts, cleanup));
    CHECK(tb_symbol_companions_destroy(fixture->owner));
    CHECK(tb_symbol_artifacts_destroy(fixture->artifacts));
    CHECK(fixture->frontend.used == 0 && fixture->evidence.used == 0 && fixture->construction.used == 0);
}


static TbSymbolCompanionLoad_t *begin(Fixture_t *fixture)
{
    TbSymbolCompanionLoad_t *load = NULL;
    CHECK(tb_symbol_companion_load_begin(fixture->owner, fixture->fact, (TbSymbolSourcePathText_t) {
        .data  = (const unsigned char *)"/candidate.debug",
        .bytes = 16
    }, TB_SYMBOL_COMPANION_BY_DEBUGLINK, (TbSymbolCompanionStorage_t) {
        .path       = "/private-pool",
        .key        = {0},
        .file_bytes = 16
    }, &load) == TB_SYMBOL_SUCCESS);
    CHECK(load && !tb_symbol_companion_load_destroy(load));

    return load;
}


static TbSymbolWorkIdentity_t plan(Fixture_t *fixture, TbSymbolCompanionLoad_t *load, uint32_t expected)
{
    unsigned char arguments[1024];
    TbSymbolHelperRequest_t work;
    TbSymbolWorkIdentity_t identity = request(fixture);
    CHECK(tb_symbol_companion_load_request(load, identity, 3, arguments, sizeof(arguments), &work) ==
          TB_SYMBOL_SUCCESS);
    CHECK(work.operation == expected && work.identity.request == identity.request && work.heavy);
    CHECK(tb_symbol_companion_load_request(load, identity, 3, arguments, sizeof(arguments), &work) ==
          TB_SYMBOL_MALFORMED);

    return identity;
}


static void retire_load(Fixture_t *fixture, TbSymbolCompanionLoad_t *load, Frozen_t *file, bool fail_cleanup)
{
    TbSymbolCompanionLoadInfo_t info = tb_symbol_companion_load_info(load);
    uint64_t charged = tb_symbol_artifacts_usage(fixture->artifacts).bytes;
    CHECK(info.phase == TB_SYMBOL_COMPANION_LOAD_CLEANUP);
    TbSymbolWorkIdentity_t identity = plan(fixture, load,
        file ? TB_SYMBOL_OPERATION_REMOVE_FILE : TB_SYMBOL_OPERATION_DISCARD_EMPTY);
    TbSymbolHelperResult_t result = {
        .outcome    = fail_cleanup ? TB_SYMBOL_OPERATIONAL_FAILURE : TB_SYMBOL_SUCCESS,
        .descriptor = -1
    };
    if (!fail_cleanup && file)
        CHECK(unlink(file->path) == 0);
    (void)tb_symbol_companion_load_finish(load, identity, 3, &result);
    info = tb_symbol_companion_load_info(load);
    if (fail_cleanup) {
        CHECK(info.phase == TB_SYMBOL_COMPANION_LOAD_BLOCKED && !tb_symbol_companion_load_destroy(load));
        CHECK(tb_symbol_artifacts_usage(fixture->artifacts).bytes == charged);
        CHECK(tb_symbol_companion_load_retry_cleanup(load));
        retire_load(fixture, load, file, false);
    } else {
        CHECK(info.phase == TB_SYMBOL_COMPANION_LOAD_DONE);
        CHECK(tb_symbol_artifacts_usage(fixture->artifacts).bytes ==
              charged - (file ? file->backing.bytes : 0));
        CHECK(tb_symbol_companion_load_destroy(load));
    }
}


static Frozen_t ready(Fixture_t *fixture, TbSymbolCompanionLoad_t *load)
{
    Frozen_t file = frozen();
    TbSymbolWorkIdentity_t identity = plan(fixture, load, TB_SYMBOL_OPERATION_ACQUIRE);
    CHECK(tb_symbol_companion_load_offer(load, identity, 3, (TbSymbolAcquireOffer_t) {
        .bytes = 4,
        .key   = file.backing.key
    }) == TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_artifacts_usage(fixture->artifacts).bytes == 8);
    TbSymbolAcquiredBytes_t bytes = acquired_bytes(file);
    unsigned char payload[56];
    tb_symbol_protocol_put(payload, bytes.bytes, 8);
    tb_symbol_protocol_put(payload + 8, bytes.key.device, 8);
    tb_symbol_protocol_put(payload + 16, bytes.key.inode, 8);
    memcpy(payload + 24, bytes.digest, sizeof(bytes.digest));
    TbSymbolHelperResult_t result = {
        .outcome    = TB_SYMBOL_SUCCESS,
        .payload    = payload,
        .length     = sizeof(payload),
        .descriptor = file.descriptor
    };
    TbSymbolWorkIdentity_t wrong = identity;
    ++wrong.generation;
    CHECK(tb_symbol_companion_load_finish(load, wrong, 3, &result) == TB_SYMBOL_STALE);
    CHECK(result.descriptor == file.descriptor);
    CHECK(tb_symbol_companion_load_finish(load, identity, 3, &result) == TB_SYMBOL_SUCCESS);
    CHECK(result.descriptor == -1);
    CHECK(tb_symbol_companion_load_info(load).phase == TB_SYMBOL_COMPANION_LOAD_INSPECT);

    identity = plan(fixture, load, TB_SYMBOL_OPERATION_COMPANION_INSPECT);
    unsigned char metadata[256];
    result = (TbSymbolHelperResult_t) {
        .outcome    = TB_SYMBOL_SUCCESS,
        .payload    = metadata,
        .length     = receipt(file, false, metadata, sizeof(metadata)),
        .descriptor = -1
    };
    CHECK(tb_symbol_companion_load_finish(load, identity, 3, &result) == TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_companion_load_info(load).phase == TB_SYMBOL_COMPANION_LOAD_READY);

    return file;
}


static void publication(void)
{
    Fixture_t fixture;
    start(&fixture);
    TbSymbolCompanionScopeCapture_t *old;
    CHECK(tb_symbol_companion_scope_capture(fixture.owner, fixture.primary, TB_SYMBOL_COMPANION_ORIGIN_ALL,
          &old) == TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_companion_scope_count(old) == 1);
    TbSymbolCompanionLoad_t *load = begin(&fixture);
    Frozen_t file = ready(&fixture, load);
    CHECK(tb_symbol_companion_scope_current(old));
    CHECK(tb_symbol_companion_load_commit(load) == TB_SYMBOL_SUCCESS);
    uint64_t relationship = tb_symbol_companion_load_info(load).relationship;
    CHECK(relationship && tb_symbol_companion_load_destroy(load));
    CHECK(!tb_symbol_companion_scope_current(old) && tb_symbol_companion_scope_count(old) == 1);
    tb_symbol_companion_scope_release(old);
    CHECK(tb_symbol_companion_fact_current(fixture.fact));
    TbSymbolArtifactInfo_t info;
    CHECK(tb_symbol_artifact_info(fixture.artifacts, fixture.primary, &info) && info.revision == 2);

    size_t stable = fixture.frontend.used;
    for (unsigned failure = 1; failure <= 4; ++failure) {
        TbSymbolCompanionScopeCapture_t *candidate = NULL;
        allocation = 0;
        fail_at = failure;
        TbSymbolOutcome_e outcome = tb_symbol_companion_scope_capture(fixture.owner, fixture.primary,
            TB_SYMBOL_COMPANION_ORIGIN_ALL, &candidate);
        fail_at = 0;
        if (outcome == TB_SYMBOL_SUCCESS)
            tb_symbol_companion_scope_release(candidate);
        else
            CHECK(outcome == TB_SYMBOL_RESOURCE_FAILURE && !candidate);
        CHECK(fixture.frontend.used == stable);
    }

    TbSymbolCompanionScopeCapture_t *combined;
    TbSymbolCompanionScopeCapture_t *primary;
    TbSymbolCompanionScopeCapture_t *companion;
    CHECK(tb_symbol_companion_scope_capture(fixture.owner, fixture.primary, TB_SYMBOL_COMPANION_ORIGIN_ALL,
          &combined) == TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_companion_scope_capture(fixture.owner, fixture.primary,
          TB_SYMBOL_COMPANION_ORIGIN_PRIMARY,
          &primary) == TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_companion_scope_capture(fixture.owner, fixture.primary,
          TB_SYMBOL_COMPANION_ORIGIN_COMPANION,
          &companion) == TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_companion_scope_count(combined) == 2 && tb_symbol_companion_scope_count(primary) == 1 &&
          tb_symbol_companion_scope_count(companion) == 1);
    TbSymbolCompanionScopeMember_t member;
    CHECK(tb_symbol_companion_scope_member(combined, 1, &member));
    CHECK(member.relationship == relationship && member.capture.content != info.content &&
          member.origin == TB_SYMBOL_COMPANION_ORIGIN_COMPANION);
    uint64_t content = member.capture.content;
    CHECK(!tb_symbol_companions_destroy(fixture.owner));

    TbSymbolCompanionRetired_t retired;
    CHECK(tb_symbol_companion_detach(fixture.owner, fixture.primary, &retired) == TB_SYMBOL_SUCCESS);
    CHECK(retired.content == content && retired.relationship == relationship);
    CHECK(!tb_symbol_companion_scope_current(combined));
    TbSymbolArtifactCleanup_t cleanup;
    CHECK(!tb_symbol_artifact_evict(fixture.artifacts, content, &cleanup));
    CHECK(tb_symbol_artifacts_usage(fixture.artifacts).bytes == 8);
    tb_symbol_companion_scope_release(combined);
    tb_symbol_companion_scope_release(primary);
    tb_symbol_companion_scope_release(companion);
    CHECK(tb_symbol_artifact_evict(fixture.artifacts, content, &cleanup));
    CHECK(unlink(file.path) == 0 && tb_symbol_artifact_backing_cleaned(fixture.artifacts, cleanup));
    finish(&fixture);
}


static void cancelled_and_stale(void)
{
    for (unsigned mode = 0; mode < 5; ++mode) {
        Fixture_t fixture;
        start(&fixture);
        TbSymbolCompanionLoad_t *load = begin(&fixture);
        if (!mode) {
            CHECK(tb_symbol_companion_load_cancel(load));
            retire_load(&fixture, load, NULL, false);
        } else {
            Frozen_t file = ready(&fixture, load);
            if (mode == 1) {
                CHECK(tb_symbol_companion_load_cancel(load));
            } else if (mode == 2) {
                CHECK(tb_symbol_artifact_revise(fixture.artifacts, fixture.primary, 1) == 2);
                CHECK(tb_symbol_companion_load_commit(load) == TB_SYMBOL_STALE);
            } else if (mode == 3) {
                CHECK(tb_symbol_artifact_close(fixture.artifacts, fixture.primary));
                CHECK(tb_symbol_companion_load_commit(load) == TB_SYMBOL_STALE);
            } else {
                fail_revision = true;
                CHECK(tb_symbol_companion_load_commit(load) == TB_SYMBOL_RESOURCE_FAILURE);
                fail_revision = false;
            }
            retire_load(&fixture, load, &file, mode == 1);
        }
        finish(&fixture);
    }
}


static void withdrawal(void)
{
    Fixture_t fixture;
    start(&fixture);
    TbSymbolCompanionLoad_t *load = begin(&fixture);
    Frozen_t file = ready(&fixture, load);
    CHECK(tb_symbol_companion_load_commit(load) == TB_SYMBOL_SUCCESS &&
          tb_symbol_companion_load_destroy(load));
    TbSymbolCompanionScopeCapture_t *combined;
    CHECK(tb_symbol_companion_scope_capture(fixture.owner, fixture.primary, TB_SYMBOL_COMPANION_ORIGIN_ALL,
          &combined) == TB_SYMBOL_SUCCESS);
    TbSymbolCompanionScopeMember_t child;
    CHECK(tb_symbol_companion_scope_member(combined, 1, &child));
    CHECK(tb_symbol_companion_fact_withdraw(fixture.fact));
    CHECK(!tb_symbol_companion_scope_current(combined));
    TbSymbolCompanionScopeCapture_t *rejected = NULL;
    CHECK(tb_symbol_companion_scope_capture(fixture.owner, fixture.primary, TB_SYMBOL_COMPANION_ORIGIN_ALL,
          &rejected) == TB_SYMBOL_STALE && !rejected);
    CHECK(tb_symbol_companion_scope_capture(fixture.owner, fixture.primary,
          TB_SYMBOL_COMPANION_ORIGIN_PRIMARY,
          &rejected) == TB_SYMBOL_SUCCESS);
    tb_symbol_companion_scope_release(rejected);
    TbSymbolCompanionRetired_t retired;
    bool found = false;
    CHECK(tb_symbol_companion_collect(fixture.owner, &retired, &found) == TB_SYMBOL_SUCCESS && found);
    CHECK(retired.content == child.capture.content);
    tb_symbol_companion_scope_release(combined);
    TbSymbolArtifactCleanup_t cleanup;
    CHECK(tb_symbol_artifact_evict(fixture.artifacts, retired.content, &cleanup));
    CHECK(unlink(file.path) == 0 && tb_symbol_artifact_backing_cleaned(fixture.artifacts, cleanup));
    finish(&fixture);
}


static void allocation_failures(void)
{
    Fixture_t fixture;
    start(&fixture);
    size_t stable = fixture.frontend.used;
    bool reached = false;
    for (unsigned failure = 1; failure < 40; ++failure) {
        allocation = 0;
        fail_at = failure;
        TbSymbolCompanionLoad_t *load = NULL;
        TbSymbolOutcome_e outcome = tb_symbol_companion_load_begin(fixture.owner, fixture.fact,
            (TbSymbolSourcePathText_t) {
                .data  = (const unsigned char *)"/candidate.debug",
                .bytes = 16
            }, TB_SYMBOL_COMPANION_BY_DEBUGLINK, (TbSymbolCompanionStorage_t) {
                .path       = "/private-pool",
                .file_bytes = 16
            }, &load);
        fail_at = 0;
        if (outcome == TB_SYMBOL_SUCCESS) {
            reached = true;
            CHECK(tb_symbol_companion_load_cancel(load));
            retire_load(&fixture, load, NULL, false);
            break;
        }
        CHECK(outcome == TB_SYMBOL_RESOURCE_FAILURE && !load && fixture.frontend.used == stable);
    }
    CHECK(reached && fixture.frontend.used == stable);
    finish(&fixture);
}


static void rejected_inspections(void)
{
    for (unsigned mode = 0; mode < 6; ++mode) {
        Fixture_t fixture;
        start(&fixture);
        size_t stable = fixture.frontend.used;
        TbSymbolCompanionInspectionPlan_t capture;
        TbSymbolCompanionInspection_t *inspection;
        TbSymbolWorkIdentity_t identity = request(&fixture);
        CHECK(tb_symbol_companion_inspect_begin(fixture.owner, fixture.primary, identity, 3,
              &capture, &inspection) == TB_SYMBOL_SUCCESS);
        unsigned char payload[256];
        Frozen_t file = fixture.primary_file;
        if (mode == 1)
            ++file.backing.key.inode;
        size_t length = receipt(file, true, payload, sizeof(payload));
        if (mode == 0)
            --length;
        if (mode == 2) {
            allocation = 0;
            fail_at = 1;
        }
        if (mode == 3)
            CHECK(tb_symbol_artifact_revise(fixture.artifacts, fixture.primary, 1) == 2);
        if (mode == 4)
            CHECK(tb_symbol_artifact_close(fixture.artifacts, fixture.primary));
        TbSymbolCompanionFact_t *fact = NULL;
        TbSymbolOutcome_e expected = mode < 2 ? TB_SYMBOL_MALFORMED : mode == 2 ? TB_SYMBOL_RESOURCE_FAILURE :
                                    mode < 5 ? TB_SYMBOL_STALE : TB_SYMBOL_UNSUPPORTED;
        CHECK(tb_symbol_companion_inspect_finish(inspection, identity, 3,
              mode == 5 ? TB_SYMBOL_UNSUPPORTED : TB_SYMBOL_SUCCESS, payload, length, &fact) == expected);
        fail_at = 0;
        CHECK(!fact && fixture.construction.used == 0);
        if (mode != 4)
            CHECK(fixture.frontend.used == stable && tb_symbol_companion_fact_current(fixture.fact));
        finish(&fixture);
    }
}


static void late_offers(void)
{
    for (unsigned mode = 0; mode < 3; ++mode) {
        Fixture_t fixture;
        start(&fixture);
        TbSymbolCompanionLoad_t *load = begin(&fixture);
        TbSymbolWorkIdentity_t identity = plan(&fixture, load, TB_SYMBOL_OPERATION_ACQUIRE);
        Frozen_t file = frozen();
        if (mode == 0)
            CHECK(tb_symbol_companion_load_cancel(load));
        else if (mode == 1)
            CHECK(tb_symbol_artifact_close(fixture.artifacts, fixture.primary));
        else
            CHECK(tb_symbol_artifact_withdraw(fixture.artifacts, fixture.primary_content));
        CHECK(tb_symbol_companion_load_info(load).cancel_required);
        CHECK(!tb_symbol_companion_load_destroy(load));
        CHECK(tb_symbol_companion_load_offer(load, identity, 3, (TbSymbolAcquireOffer_t) {
            .bytes = 4,
            .key   = file.backing.key
        }) == (mode == 0 ? TB_SYMBOL_CANCELLED : TB_SYMBOL_STALE));
        TbSymbolHelperResult_t result = {
            .outcome    = TB_SYMBOL_CANCELLED,
            .descriptor = file.descriptor
        };
        CHECK(tb_symbol_companion_load_finish(load, identity, 3, &result) ==
              (mode == 0 ? TB_SYMBOL_CANCELLED : TB_SYMBOL_STALE));
        CHECK(result.descriptor == -1 && fcntl(file.descriptor, F_GETFD) == -1);
        retire_load(&fixture, load, &file, false);
        finish(&fixture);
    }
}


static void independent_owners(void)
{
    Fixture_t fixture;
    start(&fixture);
    TbSymbolCompanionLoad_t *first = begin(&fixture);
    TbSymbolCompanionLoad_t *rejected = NULL;
    CHECK(tb_symbol_companion_load_begin(fixture.owner, fixture.fact, (TbSymbolSourcePathText_t) {
        .data  = (const unsigned char *)"/candidate.debug",
        .bytes = 16
    }, TB_SYMBOL_COMPANION_BY_DEBUGLINK, (TbSymbolCompanionStorage_t) {
        .path       = "/private-pool",
        .file_bytes = 16
    }, &rejected) == TB_SYMBOL_RESOURCE_FAILURE && !rejected);

    Frozen_t file = frozen();
    uint64_t context = tb_symbol_artifact_prepare(fixture.artifacts, "/another-primary");
    CHECK(context && tb_symbol_artifact_offer(fixture.artifacts, context, (TbSymbolAcquireOffer_t) {
        .bytes = 4,
        .key   = file.backing.key
    }) == TB_SYMBOL_SUCCESS);
    TbSymbolAcquiredBytes_t bytes = acquired_bytes(file);
    CHECK(tb_symbol_artifact_accept(fixture.artifacts, context, &bytes) == TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_artifact_publish(fixture.artifacts, context));
    TbSymbolArtifactInfo_t info;
    CHECK(tb_symbol_artifact_info(fixture.artifacts, context, &info));
    TbSymbolCompanionFact_t *other = inspect(&fixture, context, file);
    TbSymbolCompanionFact_t *saved = fixture.fact;
    fixture.fact = other;
    TbSymbolCompanionLoad_t *second = begin(&fixture);
    fixture.fact = saved;
    CHECK(tb_symbol_companion_load_cancel(first));
    retire_load(&fixture, first, NULL, false);
    CHECK(tb_symbol_companion_load_info(second).phase == TB_SYMBOL_COMPANION_LOAD_ACQUIRE);
    CHECK(tb_symbol_companion_fact_current(other));
    CHECK(tb_symbol_companion_load_cancel(second));
    retire_load(&fixture, second, NULL, false);
    tb_symbol_companion_fact_release(other);
    CHECK(tb_symbol_artifact_close(fixture.artifacts, context));
    TbSymbolArtifactCleanup_t cleanup;
    CHECK(tb_symbol_artifact_evict(fixture.artifacts, info.content, &cleanup));
    CHECK(unlink(file.path) == 0 && tb_symbol_artifact_backing_cleaned(fixture.artifacts, cleanup));
    finish(&fixture);
}


static void limits_and_rejected_grant(void)
{
    Fixture_t fixture;
    start(&fixture);
    TbSymbolCompanionInspection_t *tickets[2];
    TbSymbolWorkIdentity_t requests[2];
    TbSymbolCompanionInspectionPlan_t captures[2];
    for (unsigned n = 0; n < 2; ++n) {
        requests[n] = request(&fixture);
        CHECK(tb_symbol_companion_inspect_begin(fixture.owner, fixture.primary, requests[n], 3,
              &captures[n], &tickets[n]) == TB_SYMBOL_SUCCESS);
    }
    TbSymbolCompanionInspection_t *rejected = NULL;
    CHECK(tb_symbol_companion_inspect_begin(fixture.owner, fixture.primary, request(&fixture), 3,
          &captures[0], &rejected) == TB_SYMBOL_RESOURCE_FAILURE && !rejected);
    for (unsigned n = 0; n < 2; ++n) {
        TbSymbolCompanionFact_t *fact = NULL;
        CHECK(tb_symbol_companion_inspect_finish(tickets[n], requests[n], 3, TB_SYMBOL_CANCELLED,
              NULL, 0, &fact) == TB_SYMBOL_CANCELLED && !fact);
    }
    TbSymbolCompanionScopeCapture_t *scopes[4];
    for (unsigned n = 0; n < 4; ++n) {
        CHECK(tb_symbol_companion_scope_capture(fixture.owner, fixture.primary,
              TB_SYMBOL_COMPANION_ORIGIN_ALL, &scopes[n]) == TB_SYMBOL_SUCCESS);
    }
    TbSymbolCompanionScopeCapture_t *extra = NULL;
    CHECK(tb_symbol_companion_scope_capture(fixture.owner, fixture.primary,
          TB_SYMBOL_COMPANION_ORIGIN_ALL, &extra) == TB_SYMBOL_RESOURCE_FAILURE && !extra);

    TbSymbolCompanionLoad_t *load = begin(&fixture);
    TbSymbolWorkIdentity_t identity = plan(&fixture, load, TB_SYMBOL_OPERATION_ACQUIRE);
    Frozen_t file = frozen();
    CHECK(tb_symbol_companion_load_offer(load, identity, 3, (TbSymbolAcquireOffer_t) {
        .bytes = 17,
        .key   = file.backing.key
    }) == TB_SYMBOL_RESOURCE_FAILURE);
    CHECK(tb_symbol_companion_load_info(load).cancel_required);
    CHECK(tb_symbol_artifacts_usage(fixture.artifacts).bytes == 4);
    TbSymbolHelperResult_t result = {
        .outcome    = TB_SYMBOL_CANCELLED,
        .descriptor = -1
    };
    CHECK(tb_symbol_companion_load_finish(load, identity, 3, &result) == TB_SYMBOL_RESOURCE_FAILURE);
    identity = plan(&fixture, load, TB_SYMBOL_OPERATION_REMOVE_FILE);
    CHECK(close(file.descriptor) == 0 && unlink(file.path) == 0);
    result.outcome = TB_SYMBOL_SUCCESS;
    CHECK(tb_symbol_companion_load_finish(load, identity, 3, &result) == TB_SYMBOL_RESOURCE_FAILURE);
    CHECK(tb_symbol_companion_load_destroy(load));
    for (unsigned n = 0; n < 4; ++n) {
        CHECK(tb_symbol_companion_scope_current(scopes[n]));
        tb_symbol_companion_scope_release(scopes[n]);
    }
    finish(&fixture);
}


int main(void)
{
    publication();
    cancelled_and_stale();
    withdrawal();
    allocation_failures();
    rejected_inspections();
    late_offers();
    independent_owners();
    limits_and_rejected_grant();
    puts("PASS companion fact ownership, revision fences, scopes, withdrawal, cleanup"
         " and allocation failures");
    return 0;
}
