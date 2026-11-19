#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Actual private helper/file fixtures. */
#include "symbol/helper.h"
#include "symbol/admission.h"
#include "symbol/artifact.h"
#include "symbol/protocol.h"
#include "symbol/worker.h"
#include "../support/check.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <signal.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool fail_serialization;

void *__real_malloc(size_t length);
void *__wrap_malloc(size_t length);

void *__wrap_malloc(size_t length)
{
    return fail_serialization && length == TB_SYMBOL_RESULT_BYTES + 56 ? NULL : __real_malloc(length);
}


typedef struct {
    unsigned char bytes[1024];
    size_t        length;
} Arguments_t;

typedef struct {
    TbSymbolHelper_t       *helper;
    TbSymbolWorkIdentity_t  identity;
    TbSymbolHelperResult_t  result;
    unsigned char          bytes[56];
    bool                   completed;
    bool                   released;
    bool                   block_release;
    uint64_t               process_generation;
} Fixture_t;

static bool emit(void *context, const TbSymbolWorkIdentity_t *identity, TbSymbolHelperResult_t result)
{
    Fixture_t *fixture = context;
    CHECK(!fixture->completed && identity->request == fixture->identity.request);
    CHECK(identity->consumer == 3 && identity->revision == 7 && identity->generation == 11);
    CHECK(result.length <= sizeof(fixture->bytes));
    fixture->result = result;
    fixture->result.payload = fixture->bytes;
    if (result.length)
        memcpy(fixture->bytes, result.payload, result.length);
    if (result.descriptor >= 0) {
        CHECK((fcntl(result.descriptor, F_GETFD) & FD_CLOEXEC) != 0);
        fixture->result.descriptor = fcntl(result.descriptor, F_DUPFD_CLOEXEC, 3);
        CHECK(fixture->result.descriptor >= 0);
    }
    fixture->completed = true;
    return true;
}


static bool released(void *context, const TbSymbolWorkIdentity_t *identity, uint64_t generation)
{
    Fixture_t *fixture = context;
    CHECK(fixture->completed && !fixture->released);
    CHECK(identity->request == fixture->identity.request && identity->consumer == 3 &&
          identity->revision == 7 && identity->generation == 11);
    CHECK(generation == fixture->process_generation);
    if (fixture->block_release)
        return false;
    fixture->released = true;
    return true;
}


static void pump(Fixture_t *fixture)
{
    tb_symbol_helper_pump_with_release(fixture->helper, emit, released, fixture);
    struct pollfd fds[2];
    size_t count = tb_symbol_helper_pollfds(fixture->helper, fds);
    CHECK(count <= 2 && poll(fds, (nfds_t)count, tb_symbol_helper_timeout(fixture->helper, 2)) >= 0);
}


static void ready(Fixture_t *fixture)
{
    for (unsigned i = 0; i < 4000 && tb_symbol_helper_state(fixture->helper) != TB_SYMBOL_HELPER_READY; ++i)
        pump(fixture);
    CHECK(tb_symbol_helper_state(fixture->helper) == TB_SYMBOL_HELPER_READY);
    fixture->process_generation = tb_symbol_helper_process_generation(fixture->helper);
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


static void submit(Fixture_t *fixture, uint32_t code, const Arguments_t *arguments)
{
    CHECK(!tb_symbol_helper_jobs(fixture->helper));
    ++fixture->identity.request;
    fixture->completed = false;
    fixture->released = false;
    fixture->result = (TbSymbolHelperResult_t){
        .descriptor = -1
    };
    TbSymbolHelperRequest_t request = {
        .identity  = fixture->identity,
        .operation = code,
        .priority  = TB_SYMBOL_CONSTRUCTION,
        .heavy     = true,
        .arguments = arguments->bytes,
        .length    = arguments->length
    };
    CHECK(tb_symbol_helper_submit(fixture->helper, &request) == TB_SYMBOL_ADMITTED);
}


static void finish(Fixture_t *fixture, TbSymbolOutcome_e expected)
{
    for (unsigned i = 0; i < 6000 && !fixture->released; ++i)
        pump(fixture);
    if (!fixture->released)
        fprintf(stderr, "resource request %llu: %s\n", (unsigned long long)fixture->identity.request,
                tb_symbol_helper_error(fixture->helper));
    CHECK(fixture->completed && fixture->released && fixture->result.outcome == expected);
    CHECK(!tb_symbol_helper_jobs(fixture->helper));
}


static Arguments_t acquisition(const char *origin, const char *path, TbSymbolStorageKey_t key, uint64_t slot)
{
    Arguments_t arguments = {0};
    word(&arguments, 100000);
    word(&arguments, slot);
    word(&arguments, key.device);
    word(&arguments, key.inode);
    paths(&arguments, origin, path);
    return arguments;
}


static TbSymbolAcquireOffer_t await_offer(Fixture_t *fixture)
{
    TbSymbolAcquireOffer_t offer = {0};
    bool found = false;
    for (unsigned i = 0; i < 4000; ++i) {
        pump(fixture);
        CHECK(!fixture->completed);
        if (tb_symbol_helper_offer(fixture->helper, fixture->identity, &offer)) {
            found = true;
            break;
        }
    }
    CHECK(found);
    return offer;
}


static void remove_file(Fixture_t *fixture, const char *path, TbSymbolStorageKey_t root, uint64_t slot,
                        TbSymbolStorageKey_t key)
{
    Arguments_t arguments = {0};
    word(&arguments, slot);
    word(&arguments, root.device);
    word(&arguments, root.inode);
    word(&arguments, key.device);
    word(&arguments, key.inode);
    paths(&arguments, path, NULL);
    submit(fixture, TB_SYMBOL_OPERATION_REMOVE_FILE, &arguments);
    finish(fixture, TB_SYMBOL_SUCCESS);
    CHECK(!fixture->result.length && fixture->result.descriptor < 0);
}


static TbSymbolReservation_t *shared_context(Fixture_t *fixture, const char *origin, const char *path,
                                            TbSymbolStorageKey_t root, TbSymbolStorageKey_t first,
                                            TbSymbolAdmissionLedger_t *ledger,
                                            TbSymbolBackingCharge_t *backing)
{
    TbSymbolReservation_t *ticket = tb_symbol_admission_prepare(ledger);
    CHECK(ticket);
    Arguments_t arguments = acquisition(origin, path, root, 6);
    submit(fixture, TB_SYMBOL_OPERATION_ACQUIRE, &arguments);
    TbSymbolAcquireOffer_t offer = await_offer(fixture);
    CHECK(tb_symbol_admission_assign(ticket, offer.bytes));
    CHECK(tb_symbol_helper_grant(fixture->helper, fixture->identity, offer.bytes));
    finish(fixture, TB_SYMBOL_SUCCESS);
    CHECK(fixture->result.length == 56 && fixture->result.descriptor >= 0);
    CHECK(!close(fixture->result.descriptor));
    arguments = (Arguments_t){0};
    word(&arguments, offer.bytes);
    word(&arguments, root.device);
    word(&arguments, root.inode);
    word(&arguments, 1);
    word(&arguments, first.device);
    word(&arguments, first.inode);
    word(&arguments, 6);
    word(&arguments, offer.key.device);
    word(&arguments, offer.key.inode);
    paths(&arguments, path, NULL);
    submit(fixture, TB_SYMBOL_OPERATION_COMPARE, &arguments);
    finish(fixture, TB_SYMBOL_SUCCESS);
    CHECK(fixture->result.length == 1 && fixture->bytes[0] == 1 && fixture->result.descriptor < 0);
    /* Sharing follows full equality and confirmed candidate-file cleanup. */
    remove_file(fixture, path, root, 6, offer.key);
    CHECK(tb_symbol_admission_commit(ticket, backing) == backing);
    CHECK(tb_symbol_admission_usage(ledger).bytes == 18);
    CHECK(tb_symbol_admission_usage(ledger).open_contexts == 2);
    return ticket;
}


static void cancellation(Fixture_t *fixture, const char *origin, const char *path,
                         TbSymbolStorageKey_t root, TbSymbolAdmissionLedger_t *ledger)
{
    TbSymbolReservation_t *ticket = tb_symbol_admission_prepare(ledger);
    CHECK(ticket);
    Arguments_t arguments = acquisition(origin, path, root, 2);
    submit(fixture, TB_SYMBOL_OPERATION_ACQUIRE, &arguments);
    TbSymbolAcquireOffer_t offer = await_offer(fixture);
    CHECK(offer.bytes == 9);
    /* The small ledger cannot grant this acquisition. Waiting does not grow it. */
    CHECK(!tb_symbol_admission_assign(ticket, offer.bytes));
    char leaf[512];
    CHECK(snprintf(leaf, sizeof(leaf), "%s/artifact-0000000000000002", path) > 0);
    struct stat native;
    for (unsigned i = 0; i < 40; ++i) {
        pump(fixture);
        CHECK(!stat(leaf, &native) && !native.st_size && !fixture->completed);
    }
    TbSymbolWorkIdentity_t wrong = fixture->identity;
    ++wrong.revision;
    CHECK(!tb_symbol_helper_grant(fixture->helper, wrong, offer.bytes));
    CHECK(!tb_symbol_helper_grant(fixture->helper, fixture->identity, offer.bytes - 1));
    fixture->block_release = true;
    CHECK(tb_symbol_helper_cancel(fixture->helper, fixture->identity));
    for (unsigned i = 0; i < 4000 && !fixture->completed; ++i)
        pump(fixture);
    CHECK(fixture->completed && fixture->result.outcome == TB_SYMBOL_CANCELLED);
    CHECK(!tb_symbol_helper_grant(fixture->helper, fixture->identity, offer.bytes));
    TbSymbolAcquireOffer_t retained;
    CHECK(tb_symbol_helper_offer(fixture->helper, fixture->identity, &retained));
    CHECK(retained.bytes == offer.bytes && retained.key.inode == offer.key.inode);
    CHECK(tb_symbol_admission_abort(ticket));
    CHECK(tb_symbol_admission_usage(ledger).cleanup_pending == 1);
    fixture->block_release = false;
    finish(fixture, TB_SYMBOL_CANCELLED);
    CHECK(!stat(leaf, &native));
    /* Neither cancellation nor worker retirement is filesystem cleanup. */
    CHECK(tb_symbol_admission_usage(ledger).cleanup_pending == 1);
    remove_file(fixture, path, root, 2, offer.key);
    CHECK(tb_symbol_admission_cleaned(ticket));
}


static void publication_failure(Fixture_t *fixture, const char *origin, const char *path,
                                TbSymbolStorageKey_t root, TbSymbolAdmissionLedger_t *ledger)
{
    TbSymbolReservation_t *ticket = tb_symbol_admission_prepare(ledger);
    CHECK(ticket);
    Arguments_t arguments = acquisition(origin, path, root, 4);
    submit(fixture, TB_SYMBOL_OPERATION_ACQUIRE, &arguments);
    TbSymbolAcquireOffer_t offer = await_offer(fixture);
    CHECK(tb_symbol_admission_assign(ticket, offer.bytes));
    CHECK(tb_symbol_helper_grant(fixture->helper, fixture->identity, offer.bytes));
    finish(fixture, TB_SYMBOL_RESOURCE_FAILURE);
    CHECK(!fixture->result.length && fixture->result.descriptor < 0);
    CHECK(fixture->result.detail == (TB_SYMBOL_DETAIL_PUBLICATION | TB_SYMBOL_DETAIL_CREATED |
                                     TB_SYMBOL_DETAIL_HAS_KEY));
    CHECK(fixture->result.storage_key.device == offer.key.device &&
          fixture->result.storage_key.inode == offer.key.inode);
    CHECK(tb_symbol_helper_state(fixture->helper) == TB_SYMBOL_HELPER_READY);
    CHECK(tb_symbol_admission_abort(ticket));
    CHECK(tb_symbol_admission_usage(ledger).bytes == offer.bytes);
    remove_file(fixture, path, root, 4, offer.key);
    CHECK(tb_symbol_admission_cleaned(ticket));
}


static TbSymbolAcquireOffer_t worker_death(Fixture_t *fixture, const char *origin, const char *path,
                                          TbSymbolStorageKey_t root)
{
    Arguments_t arguments = acquisition(origin, path, root, 5);
    submit(fixture, TB_SYMBOL_OPERATION_ACQUIRE, &arguments);
    TbSymbolAcquireOffer_t offer = await_offer(fixture);
    struct pollfd fds[2];
    CHECK(tb_symbol_helper_pollfds(fixture->helper, fds) == 2);
    CHECK(!syscall(SYS_pidfd_send_signal, fds[1].fd, SIGKILL, NULL, 0));
    finish(fixture, TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(tb_symbol_helper_state(fixture->helper) == TB_SYMBOL_HELPER_UNAVAILABLE);
    CHECK(tb_symbol_helper_reaped_generation(fixture->helper) == fixture->process_generation);
    char leaf[512];
    CHECK(snprintf(leaf, sizeof(leaf), "%s/artifact-0000000000000005", path) > 0);
    struct stat native;
    CHECK(!stat(leaf, &native) && !native.st_size);
    return offer;
}


static void close_helper(Fixture_t *fixture)
{
    tb_symbol_helper_close(fixture->helper);
    for (unsigned i = 0; i < 4000 && tb_symbol_helper_state(fixture->helper) != TB_SYMBOL_HELPER_IDLE; ++i)
        pump(fixture);
    CHECK(tb_symbol_helper_state(fixture->helper) == TB_SYMBOL_HELPER_IDLE);
    CHECK(tb_symbol_helper_reaped_generation(fixture->helper) == fixture->process_generation);
}


static uint64_t registry_acquire(Fixture_t *fixture, TbSymbolArtifacts_t *owner, const char *origin,
                                 const char *path, TbSymbolStorageKey_t root)
{
    uint64_t context = tb_symbol_artifact_prepare(owner, origin);
    CHECK(context);
    Arguments_t arguments = acquisition(origin, path, root, context);
    submit(fixture, TB_SYMBOL_OPERATION_ACQUIRE, &arguments);
    TbSymbolAcquireOffer_t offer = await_offer(fixture);
    CHECK(tb_symbol_artifact_offer(owner, context, offer) == TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_helper_grant(fixture->helper, fixture->identity, offer.bytes));
    finish(fixture, TB_SYMBOL_SUCCESS);
    CHECK(fixture->result.length == 56 && fixture->result.descriptor >= 0);
    TbSymbolAcquiredBytes_t bytes = {
        .bytes      = tb_symbol_protocol_get(fixture->bytes, 8),
        .descriptor = fixture->result.descriptor,
        .key = {
            .device = tb_symbol_protocol_get(fixture->bytes + 8, 8),
            .inode  = tb_symbol_protocol_get(fixture->bytes + 16, 8)
        }
    };
    memcpy(bytes.digest, fixture->bytes + 24, sizeof(bytes.digest));
    CHECK(tb_symbol_artifact_accept(owner, context, &bytes) == TB_SYMBOL_SUCCESS);
    fixture->result.descriptor = -1;
    return context;
}


static void registry_flow(Fixture_t *fixture, const char *origin, const char *base, const char *executable)
{
    char path[512];
    CHECK(snprintf(path, sizeof(path), "%s-registry", base) > 0);
    Arguments_t arguments = {0};
    paths(&arguments, path, NULL);
    submit(fixture, TB_SYMBOL_OPERATION_CREATE_STORAGE, &arguments);
    finish(fixture, TB_SYMBOL_SUCCESS);
    CHECK(fixture->result.length == 16);
    TbSymbolStorageKey_t root = {
        .device = tb_symbol_protocol_get(fixture->bytes, 8),
        .inode  = tb_symbol_protocol_get(fixture->bytes + 8, 8)
    };
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 1024 * 1024));
    TbSymbolArtifactIds_t identities = {0};
    TbSymbolArtifacts_t *owner = tb_symbol_artifacts_create(9, 18, &identities, &budget);
    CHECK(owner);
    uint64_t first = registry_acquire(fixture, owner, origin, path, root);
    CHECK(tb_symbol_artifact_publish(owner, first));
    TbSymbolArtifactInfo_t info;
    CHECK(tb_symbol_artifact_info(owner, first, &info));
    uint64_t content = info.content;
    uint64_t generation = fixture->process_generation;
    close_helper(fixture);
    CHECK(tb_symbol_artifact_info(owner, first, &info) && info.content == content &&
          info.state == TB_SYMBOL_ARTIFACT_RETAINED);
    CHECK(tb_symbol_helper_start(fixture->helper, executable));
    ready(fixture);
    CHECK(fixture->process_generation == generation + 1);
    uint64_t second = registry_acquire(fixture, owner, origin, path, root);
    TbSymbolWorkIdentity_t captured = fixture->identity;
    ++captured.request;
    TbSymbolArtifactMatchPlan_t plan;
    TbSymbolArtifactMatch_t *match = NULL;
    CHECK(tb_symbol_artifact_match_begin(owner, second, content, captured, fixture->process_generation,
                                         &plan, &match) == TB_SYMBOL_SUCCESS);
    CHECK(match);
    arguments = (Arguments_t){0};
    word(&arguments, plan.bytes);
    word(&arguments, root.device);
    word(&arguments, root.inode);
    word(&arguments, plan.left_slot);
    word(&arguments, plan.left_key.device);
    word(&arguments, plan.left_key.inode);
    word(&arguments, plan.right_slot);
    word(&arguments, plan.right_key.device);
    word(&arguments, plan.right_key.inode);
    paths(&arguments, path, NULL);
    submit(fixture, TB_SYMBOL_OPERATION_COMPARE, &arguments);
    CHECK(tb_symbol_artifact_close(owner, first));
    TbSymbolArtifactCleanup_t cleanup;
    CHECK(!tb_symbol_artifact_evict(owner, content, &cleanup));
    finish(fixture, TB_SYMBOL_SUCCESS);
    CHECK(fixture->result.length == 1 && fixture->bytes[0] == 1);
    CHECK(tb_symbol_artifact_match_finish(match, fixture->identity, fixture->process_generation,
                                          fixture->result.outcome, true) == TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_artifact_share_begin(owner, second, &cleanup));
    CHECK(tb_symbol_artifacts_usage(owner).bytes == 18);
    remove_file(fixture, path, root, cleanup.slot, cleanup.key);
    CHECK(tb_symbol_artifact_share_cleaned(owner, second, cleanup));
    CHECK(tb_symbol_artifacts_usage(owner).bytes == 9 && tb_symbol_artifacts_usage(owner).open_contexts == 1);
    CHECK(tb_symbol_artifact_info(owner, second, &info) && info.content == content);
    CHECK(!tb_symbol_artifact_info(owner, first, &info));
    TbSymbolArtifactLease_t *lease = tb_symbol_artifact_lease(owner, second);
    CHECK(lease && tb_symbol_artifact_close(owner, second) && !tb_symbol_artifact_lease_current(lease));
    unsigned char bytes[9];
    CHECK(pread(tb_symbol_artifact_lease_descriptor(lease), bytes, sizeof(bytes), 0) == 9);
    CHECK(!memcmp(bytes, "a\0bcdefgh", sizeof(bytes)));
    CHECK(!tb_symbol_artifact_evict(owner, content, &cleanup));
    tb_symbol_artifact_lease_release(lease);
    CHECK(tb_symbol_artifact_evict(owner, content, &cleanup) && cleanup.slot == first);
    remove_file(fixture, path, root, cleanup.slot, cleanup.key);
    CHECK(tb_symbol_artifact_backing_cleaned(owner, cleanup));
    CHECK(tb_symbol_artifacts_destroy(owner) && !budget.used);
    arguments = (Arguments_t){0};
    word(&arguments, root.device);
    word(&arguments, root.inode);
    paths(&arguments, path, NULL);
    submit(fixture, TB_SYMBOL_OPERATION_REMOVE_STORAGE, &arguments);
    finish(fixture, TB_SYMBOL_SUCCESS);
}




int main(int argc, char **argv)
{
    if (argc == 2 && !strcmp(argv[1], "--symbol-worker")) {
        fail_serialization = getenv("TB_TEST_SYMBOL_RESOURCE_FAILURE") != NULL;
        return tb_symbol_worker_main(3);
    }
    CHECK(argc == 2);
    char parent[] = "/tmp/tb-symbol-resource-XXXXXX";
    CHECK(mkdtemp(parent));
    char origin[256];
    char path[256];
    CHECK(snprintf(origin, sizeof(origin), "%s/source", parent) > 0);
    CHECK(snprintf(path, sizeof(path), "%s/pool", parent) > 0);
    int source = open(origin, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    CHECK(source >= 0 && write(source, "a\0bcdefgh", 9) == 9 && !close(source));
    TbSymbolHelperConfig_t config;
    tb_symbol_helper_defaults(&config);
    Fixture_t fixture = {
        .identity = {
            .consumer   = 3,
            .revision   = 7,
            .generation = 11
        }
    };
    fixture.helper = tb_symbol_helper_create(&config, NULL);
    CHECK(fixture.helper && tb_symbol_helper_start(fixture.helper, argv[1]));
    ready(&fixture);
    Arguments_t arguments = {0};
    paths(&arguments, path, NULL);
    submit(&fixture, TB_SYMBOL_OPERATION_CREATE_STORAGE, &arguments);
    finish(&fixture, TB_SYMBOL_SUCCESS);
    CHECK(fixture.result.length == 16 && fixture.result.descriptor < 0);
    TbSymbolStorageKey_t root = {
        .device = tb_symbol_protocol_get(fixture.bytes, 8),
        .inode  = tb_symbol_protocol_get(fixture.bytes + 8, 8)
    };
    registry_flow(&fixture, origin, path, argv[1]);
    TbSymbolAdmissionLedger_t *ledger = tb_symbol_admission_create(9, 18, NULL);
    CHECK(ledger);
    TbSymbolReservation_t *ticket = tb_symbol_admission_prepare(ledger);
    CHECK(ticket);
    arguments = acquisition(origin, path, root, 1);
    submit(&fixture, TB_SYMBOL_OPERATION_ACQUIRE, &arguments);
    TbSymbolAcquireOffer_t offer = await_offer(&fixture);
    CHECK(offer.bytes == 9 && tb_symbol_admission_assign(ticket, offer.bytes));
    CHECK(tb_symbol_admission_usage(ledger).bytes == offer.bytes);
    CHECK(tb_symbol_helper_grant(fixture.helper, fixture.identity, offer.bytes));
    CHECK(!tb_symbol_helper_grant(fixture.helper, fixture.identity, offer.bytes));
    finish(&fixture, TB_SYMBOL_SUCCESS);
    CHECK(fixture.result.length == 56 && fixture.result.descriptor >= 0);
    CHECK(tb_symbol_protocol_get(fixture.bytes, 8) == offer.bytes);
    CHECK(tb_symbol_protocol_get(fixture.bytes + 8, 8) == offer.key.device);
    CHECK(tb_symbol_protocol_get(fixture.bytes + 16, 8) == offer.key.inode);
    CHECK((fcntl(fixture.result.descriptor, F_GETFL) & O_ACCMODE) == O_RDONLY);
    unsigned char bytes[9];
    CHECK(pread(fixture.result.descriptor, bytes, sizeof(bytes), 0) == 9 && !memcmp(bytes, "a\0bcdefgh", 9));
    TbSha256_t hash;
    unsigned char digest[32];
    tb_sha256_init(&hash);
    CHECK(tb_sha256_update(&hash, bytes, sizeof(bytes)) && tb_sha256_final(&hash, sizeof(bytes), digest));
    CHECK(!memcmp(digest, fixture.bytes + 24, sizeof(digest)));
    int retained = fixture.result.descriptor;
    TbSymbolBackingCharge_t *backing = tb_symbol_admission_commit(ticket, NULL);
    CHECK(backing);
    TbSymbolReservation_t *second = shared_context(&fixture, origin, path, root, offer.key, ledger, backing);
    cancellation(&fixture, origin, path, root, ledger);
    CHECK(tb_symbol_admission_close(second));
    CHECK(tb_symbol_admission_usage(ledger).bytes == 9);

    /* Named backing and a separately retained descriptor survive worker restart. */
    uint64_t generation = fixture.process_generation;
    close_helper(&fixture);
    CHECK(pread(retained, bytes, sizeof(bytes), 0) == 9 && !memcmp(bytes, "a\0bcdefgh", 9));
    CHECK(tb_symbol_helper_start(fixture.helper, argv[1]));
    ready(&fixture);
    CHECK(fixture.process_generation == generation + 1 && tb_symbol_admission_usage(ledger).bytes == 9);
    arguments = acquisition("/does/not/exist", path, root, 3);
    submit(&fixture, TB_SYMBOL_OPERATION_ACQUIRE, &arguments);
    finish(&fixture, TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(fixture.result.system_error == ENOENT && !fixture.result.length && fixture.result.descriptor < 0);
    CHECK(!(fixture.result.detail & TB_SYMBOL_DETAIL_HAS_KEY));
    CHECK(!close(retained));
    CHECK(tb_symbol_admission_close(ticket) && tb_symbol_admission_evict(backing));
    CHECK(tb_symbol_admission_usage(ledger).bytes == 9);
    remove_file(&fixture, path, root, 1, offer.key);
    CHECK(tb_symbol_admission_backing_cleaned(backing));
    CHECK(!tb_symbol_admission_usage(ledger).bytes);
    TbSymbolAcquireOffer_t abandoned = worker_death(&fixture, origin, path, root);
    CHECK(tb_symbol_helper_start(fixture.helper, argv[1]));
    ready(&fixture);
    remove_file(&fixture, path, root, 5, abandoned.key);
    close_helper(&fixture);
    CHECK(!setenv("TB_TEST_SYMBOL_RESOURCE_FAILURE", "1", 1));
    CHECK(tb_symbol_helper_start(fixture.helper, NULL));
    CHECK(!unsetenv("TB_TEST_SYMBOL_RESOURCE_FAILURE"));
    ready(&fixture);
    publication_failure(&fixture, origin, path, root, ledger);
    CHECK(tb_symbol_admission_destroy(ledger));
    arguments = (Arguments_t){0};
    word(&arguments, root.device);
    word(&arguments, root.inode);
    paths(&arguments, path, NULL);
    submit(&fixture, TB_SYMBOL_OPERATION_REMOVE_STORAGE, &arguments);
    finish(&fixture, TB_SYMBOL_SUCCESS);
    close_helper(&fixture);
    CHECK(tb_symbol_helper_destroy(fixture.helper));
    CHECK(!unlink(origin) && !rmdir(parent));
    return 0;
}
