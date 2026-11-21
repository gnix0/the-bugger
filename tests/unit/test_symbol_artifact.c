#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Local retained-file ownership fixtures. */
#include "symbol/artifact.h"
#include "symbol/equal.h"
#include "../support/check.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned allocation;
static unsigned fail_at;
static bool fail_inspection;

int __real_fstat(int descriptor, struct stat *info);
int __wrap_fstat(int descriptor, struct stat *info);

void *__real_calloc(size_t count, size_t bytes);
void *__real_malloc(size_t bytes);
void *__wrap_calloc(size_t count, size_t bytes);
void *__wrap_malloc(size_t bytes);

void *__wrap_calloc(size_t count, size_t bytes)
{
    return ++allocation == fail_at ? NULL : __real_calloc(count, bytes);
}


void *__wrap_malloc(size_t bytes)
{
    return ++allocation == fail_at ? NULL : __real_malloc(bytes);
}


int __wrap_fstat(int descriptor, struct stat *info)
{
    if (fail_inspection) {
        errno = EIO;
        return -1;
    }
    return __real_fstat(descriptor, info);
}


static void configuration(void)
{
    const char *names[] = {"TB_SYMBOL_FILE_MIB", "TB_SYMBOL_ADMISSION_MIB"};
    char *saved[2] = {0};
    for (unsigned i = 0; i < 2; ++i) {
        const char *value = getenv(names[i]);
        saved[i] = value ? strdup(value) : NULL;
        CHECK(!value || saved[i]);
        CHECK(!unsetenv(names[i]));
    }
    TbSymbolArtifactConfig_t config;
    tb_symbol_artifact_defaults(&config);
    CHECK(config.file_bytes == TB_SYMBOL_FILE_BYTES_DEFAULT &&
          config.aggregate_bytes == TB_SYMBOL_ADMISSION_BYTES_DEFAULT);
    CHECK(tb_symbol_artifact_environment(&config));
    CHECK(!setenv(names[0], "4", 1) && !setenv(names[1], "8", 1));
    CHECK(tb_symbol_artifact_environment(&config));
    CHECK(config.file_bytes == UINT64_C(4) * 1024 * 1024 &&
          config.aggregate_bytes == UINT64_C(8) * 1024 * 1024);
    const char *invalid[] = {"", "0", "-1", "+1", " 1", "1x", "17592186044416",
                             "18446744073709551616", "9"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(invalid[0]); ++i) {
        CHECK(!setenv(names[0], invalid[i], 1));
        CHECK(!tb_symbol_artifact_environment(&config));
        CHECK(config.file_bytes == UINT64_C(4) * 1024 * 1024 &&
              config.aggregate_bytes == UINT64_C(8) * 1024 * 1024);
    }
    for (unsigned i = 0; i < 2; ++i) {
        CHECK(!(saved[i] ? setenv(names[i], saved[i], 1) : unsetenv(names[i])));
        free(saved[i]);
    }
}


static void allocation_failures(void)
{
    TbSymbolArtifactIds_t identities = {0};
    for (unsigned point = 1; point < 48; ++point) {
        TbSymbolBudget_t budget;
        CHECK(tb_symbol_budget_init(&budget, 1024 * 1024));
        allocation = 0;
        fail_at = point;
        TbSymbolArtifacts_t *owner = tb_symbol_artifacts_create(16, 32, &identities, &budget);
        if (owner) {
            uint64_t identity = tb_symbol_artifact_prepare(owner, "same origin");
            fail_at = 0;
            if (identity) {
                CHECK(tb_symbol_artifact_abandon(owner, identity));
                CHECK(tb_symbol_artifact_cleaned(owner, identity, NULL));
            }
            CHECK(!tb_symbol_artifacts_usage(owner).bytes);
            CHECK(!tb_symbol_artifacts_usage(owner).reservations);
            CHECK(tb_symbol_artifacts_destroy(owner));
        }
        fail_at = 0;
        CHECK(!budget.used);
    }
}


static void context_identities(void)
{
    TbSymbolArtifactIds_t identities = {0};
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 1024 * 1024));
    TbSymbolArtifacts_t *owner = tb_symbol_artifacts_create(16, 32, &identities, &budget);
    CHECK(owner);
    size_t baseline = budget.used;
    uint64_t values[1024];
    char origin[] = "origin";
    for (size_t i = 0; i < 1024; ++i) {
        values[i] = tb_symbol_artifact_prepare(owner, origin);
        CHECK(values[i] && (!i || values[i] > values[i - 1]));
    }
    origin[0] = 'x';
    for (size_t i = 0; i < 1024; ++i) {
        size_t index = (i * 797) % 1024;
        uint64_t identity = values[index];
        TbSymbolArtifactInfo_t info;
        CHECK(tb_symbol_artifact_info(owner, identity, &info));
        CHECK(info.context == identity && info.revision == 1 && !info.content && !info.bytes);
        CHECK(info.state == TB_SYMBOL_ARTIFACT_PREPARED && !strcmp(info.origin, "origin"));
        CHECK(!tb_symbol_artifact_publish(owner, identity));
        CHECK(!tb_symbol_artifact_lease(owner, identity));
        CHECK(!tb_symbol_artifact_cleaned(owner, identity, NULL));
        CHECK(tb_symbol_artifact_close(owner, identity));
        CHECK(tb_symbol_artifact_info(owner, identity, &info) && info.revision == 2);
        CHECK(!tb_symbol_artifact_close(owner, identity));
        CHECK(tb_symbol_artifact_abandon(owner, identity));
        CHECK(tb_symbol_artifact_cleaned(owner, identity, NULL));
        CHECK(!tb_symbol_artifact_info(owner, identity, &info));
    }
    CHECK(budget.used == baseline);
    CHECK(!tb_symbol_artifact_info(owner, UINT64_MAX, &(TbSymbolArtifactInfo_t){0}));
    uint64_t later = tb_symbol_artifact_prepare(owner, "origin");
    CHECK(later > values[1023]);
    CHECK(tb_symbol_artifact_abandon(owner, later) && tb_symbol_artifact_cleaned(owner, later, NULL));
    budget.limit = budget.used;
    CHECK(!tb_symbol_artifact_prepare(owner, "exhausted"));
    CHECK(tb_symbol_artifacts_destroy(owner) && !budget.used);
    budget.limit = 1024 * 1024;
    owner = tb_symbol_artifacts_create(16, 32, &identities, &budget);
    CHECK(owner);
    uint64_t replacement = tb_symbol_artifact_prepare(owner, "origin");
    CHECK(replacement > later);
    CHECK(tb_symbol_artifact_abandon(owner, replacement) &&
          tb_symbol_artifact_cleaned(owner, replacement, NULL));
    identities.sequence = UINT64_MAX - 2;
    uint64_t last = tb_symbol_artifact_prepare(owner, "last");
    CHECK(last == UINT64_MAX - 1 && !tb_symbol_artifact_prepare(owner, "exhausted"));
    CHECK(tb_symbol_artifact_abandon(owner, last) && tb_symbol_artifact_cleaned(owner, last, NULL));
    CHECK(tb_symbol_artifacts_destroy(owner) && !budget.used);
}


static TbSymbolAcquiredBytes_t acquire(TbSymbolArtifacts_t *owner, uint64_t identity,
                                      TbSymbolStorage_t *storage, const char *origin)
{
    TbSymbolAcquire_t *operation = tb_symbol_acquire_create(origin, storage, identity, 16, NULL);
    CHECK(operation);
    bool granted = false;
    TbSymbolOutcome_e result = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 10000 && result == TB_SYMBOL_PENDING; ++i) {
        result = tb_symbol_acquire_step(operation, 2);
        uint64_t extent;
        if (!granted && tb_symbol_acquire_offer(operation, &extent)) {
            TbSymbolAcquireOffer_t offer = {
                .bytes = extent
            };
            CHECK(tb_symbol_acquire_storage_key(operation, &offer.key));
            CHECK(tb_symbol_artifact_offer(owner, identity, offer) == TB_SYMBOL_SUCCESS);
            CHECK(tb_symbol_artifact_offer(owner, identity, offer) == TB_SYMBOL_STALE);
            CHECK(tb_symbol_acquire_grant(operation, extent));
            granted = true;
        }
    }
    CHECK(granted && result == TB_SYMBOL_SUCCESS);
    TbSymbolAcquiredBytes_t bytes;
    CHECK(tb_symbol_acquire_take(operation, &bytes));
    tb_symbol_acquire_destroy(operation);
    return bytes;
}


static void retained(const char *origin, const char *path)
{
    TbSymbolArtifactIds_t identities = {0};
    TbSymbolStorage_t *storage = tb_symbol_storage_create(path, NULL, NULL);
    CHECK(storage);
    TbSymbolOutcome_e result;
    do {
        result = tb_symbol_storage_step(storage);
    } while (result == TB_SYMBOL_PENDING);
    CHECK(result == TB_SYMBOL_SUCCESS);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 1024 * 1024));
    TbSymbolArtifacts_t *owner = tb_symbol_artifacts_create(16, 32, &identities, &budget);
    CHECK(owner);
    uint64_t identity = tb_symbol_artifact_prepare(owner, origin);
    CHECK(identity);
    TbSymbolAcquiredBytes_t bytes = acquire(owner, identity, storage, origin);
    TbSymbolAcquiredBytes_t wrong = bytes;
    ++wrong.bytes;
    CHECK(tb_symbol_artifact_accept(owner, identity, &wrong) == TB_SYMBOL_MALFORMED);
    wrong = bytes;
    wrong.descriptor = open(origin, O_RDWR | O_CLOEXEC);
    CHECK(wrong.descriptor >= 0);
    CHECK(tb_symbol_artifact_accept(owner, identity, &wrong) == TB_SYMBOL_MALFORMED);
    CHECK(!close(wrong.descriptor));
    wrong = bytes;
    wrong.descriptor = fcntl(bytes.descriptor, F_DUPFD, 3);
    CHECK(wrong.descriptor >= 0);
    CHECK(tb_symbol_artifact_accept(owner, identity, &wrong) == TB_SYMBOL_MALFORMED);
    CHECK(!close(wrong.descriptor));
    char descriptor_path[64];
    CHECK(snprintf(descriptor_path, sizeof(descriptor_path), "/proc/self/fd/%d", bytes.descriptor) > 0);
    wrong = bytes;
    wrong.descriptor = open(descriptor_path, O_PATH | O_CLOEXEC);
    CHECK(wrong.descriptor >= 0);
    CHECK(tb_symbol_artifact_accept(owner, identity, &wrong) == TB_SYMBOL_MALFORMED);
    CHECK(!close(wrong.descriptor));
    fail_inspection = true;
    CHECK(tb_symbol_artifact_accept(owner, identity, &bytes) == TB_SYMBOL_OPERATIONAL_FAILURE);
    fail_inspection = false;
    CHECK(tb_symbol_artifact_accept(owner, identity, &bytes) == TB_SYMBOL_SUCCESS);
    allocation = 0;
    fail_at = 1;
    CHECK(tb_symbol_artifact_publish(owner, identity));
    CHECK(tb_symbol_artifact_lease(owner, identity) == NULL);
    fail_at = 0;
    TbSymbolArtifactInfo_t info;
    CHECK(tb_symbol_artifact_info(owner, identity, &info));
    CHECK(info.state == TB_SYMBOL_ARTIFACT_RETAINED && info.bytes == 5 && info.content &&
          info.content != identity);
    uint64_t content = info.content;
    CHECK(!strcmp(info.origin, origin));
    TbSymbolArtifactLease_t *lease = tb_symbol_artifact_lease(owner, identity);
    CHECK(lease && tb_symbol_artifact_lease_current(lease));
    int descriptor = tb_symbol_artifact_lease_descriptor(lease);
    CHECK(descriptor == bytes.descriptor);
    CHECK(!tb_symbol_artifacts_destroy(owner));
    TbSymbolArtifactCleanup_t cleanup;
    CHECK(!tb_symbol_artifact_evict(owner, info.content, &cleanup));
    CHECK(tb_symbol_artifact_close(owner, identity));
    CHECK(!tb_symbol_artifact_lease_current(lease));
    CHECK(!tb_symbol_artifact_info(owner, identity, &info));
    CHECK(!tb_symbol_artifact_evict(owner, content, &cleanup));
    unsigned char buffer[5];
    CHECK(pread(descriptor, buffer, sizeof(buffer), 0) == 5 && !memcmp(buffer, "a\0bcd", 5));
    tb_symbol_artifact_lease_release(lease);
    CHECK(tb_symbol_artifact_evict(owner, content, &cleanup));
    CHECK(fcntl(descriptor, F_GETFD) == -1 && errno == EBADF);
    CHECK(cleanup.slot == identity && cleanup.bytes == 5);
    CHECK(tb_symbol_artifacts_usage(owner).bytes == 5);
    TbSymbolArtifactCleanup_t bad = cleanup;
    ++bad.key.inode;
    CHECK(!tb_symbol_artifact_backing_cleaned(owner, bad));
    TbSymbolStorageInfo_t file;
    CHECK(tb_symbol_storage_info(storage, cleanup.slot, &file));
    CHECK(tb_symbol_storage_unlink(storage, cleanup.slot, &cleanup.key));
    CHECK(tb_symbol_artifact_backing_cleaned(owner, cleanup));
    CHECK(!tb_symbol_artifact_backing_cleaned(owner, cleanup));
    CHECK(!tb_symbol_artifacts_usage(owner).bytes);

    identity = tb_symbol_artifact_prepare(owner, origin);
    CHECK(identity > cleanup.content);
    bytes = acquire(owner, identity, storage, origin);
    CHECK(tb_symbol_artifact_accept(owner, identity, &bytes) == TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_artifact_close(owner, identity));
    CHECK(!tb_symbol_artifact_publish(owner, identity));
    CHECK(tb_symbol_artifact_abandon(owner, identity));
    CHECK(tb_symbol_artifacts_usage(owner).bytes == 5);
    CHECK(!tb_symbol_artifact_cleaned(owner, identity, NULL));
    CHECK(tb_symbol_storage_unlink(storage, identity, &bytes.key));
    CHECK(tb_symbol_artifact_cleaned(owner, identity, &bytes.key));
    CHECK(tb_symbol_artifacts_destroy(owner) && !budget.used);
    CHECK(tb_symbol_storage_remove(storage));
    tb_symbol_storage_destroy(storage);
}


static TbSymbolWorkIdentity_t comparison_request(void)
{
    return (TbSymbolWorkIdentity_t){
        .request    = 9,
        .consumer   = 77,
        .revision   = 97,
        .generation = 19
    };
}


static void shared(const char *origin, const char *path)
{
    TbSymbolArtifactIds_t identities = {0};
    TbSymbolStorage_t *storage = tb_symbol_storage_create(path, NULL, NULL);
    CHECK(storage);
    TbSymbolOutcome_e result;
    do {
        result = tb_symbol_storage_step(storage);
    } while (result == TB_SYMBOL_PENDING);
    CHECK(result == TB_SYMBOL_SUCCESS);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 1024 * 1024));
    TbSymbolArtifacts_t *owner = tb_symbol_artifacts_create(16, 32, &identities, &budget);
    CHECK(owner);
    uint64_t first = tb_symbol_artifact_prepare(owner, origin);
    TbSymbolAcquiredBytes_t left = acquire(owner, first, storage, origin);
    CHECK(tb_symbol_artifact_accept(owner, first, &left) == TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_artifact_publish(owner, first));
    TbSymbolArtifactInfo_t info;
    CHECK(tb_symbol_artifact_info(owner, first, &info));
    uint64_t content = info.content;
    TbSymbolArtifactLease_t *lease = tb_symbol_artifact_lease(owner, first);
    CHECK(lease);

    uint64_t cancelled = tb_symbol_artifact_prepare(owner, origin);
    TbSymbolAcquiredBytes_t candidate = acquire(owner, cancelled, storage, origin);
    CHECK(tb_symbol_artifact_accept(owner, cancelled, &candidate) == TB_SYMBOL_SUCCESS);
    TbSymbolArtifactMatchPlan_t plan;
    TbSymbolWorkIdentity_t request = comparison_request();
    TbSymbolArtifactMatch_t *match = NULL;
    CHECK(tb_symbol_artifact_match_begin(owner, cancelled, first, request, 2, &plan, &match) ==
          TB_SYMBOL_STALE);
    CHECK(tb_symbol_artifact_match_begin(owner, cancelled, content, request, 2, &plan, &match) ==
          TB_SYMBOL_SUCCESS);
    CHECK(match && !tb_symbol_artifact_publish(owner, cancelled));
    CHECK(tb_symbol_artifact_close(owner, cancelled));
    CHECK(!tb_symbol_artifact_abandon(owner, cancelled));
    CHECK(tb_symbol_artifact_match_finish(match, request, 2, TB_SYMBOL_CANCELLED, false) == TB_SYMBOL_STALE);
    CHECK(tb_symbol_artifact_abandon(owner, cancelled));
    CHECK(tb_symbol_storage_unlink(storage, cancelled, &candidate.key));
    CHECK(tb_symbol_artifact_cleaned(owner, cancelled, &candidate.key));

    uint64_t failed = tb_symbol_artifact_prepare(owner, origin);
    candidate = acquire(owner, failed, storage, origin);
    CHECK(tb_symbol_artifact_accept(owner, failed, &candidate) == TB_SYMBOL_SUCCESS);
    request = comparison_request();
    match = NULL;
    allocation = 0;
    fail_at = 1;
    CHECK(tb_symbol_artifact_match_begin(owner, failed, content, request, 2, &plan, &match) ==
          TB_SYMBOL_RESOURCE_FAILURE);
    fail_at = 0;
    CHECK(!match && !tb_symbol_artifact_publish(owner, failed));
    CHECK(tb_symbol_artifact_abandon(owner, failed));
    CHECK(tb_symbol_storage_unlink(storage, failed, &candidate.key));
    CHECK(tb_symbol_artifact_cleaned(owner, failed, &candidate.key));

    failed = tb_symbol_artifact_prepare(owner, origin);
    candidate = acquire(owner, failed, storage, origin);
    CHECK(tb_symbol_artifact_accept(owner, failed, &candidate) == TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_artifact_match_begin(owner, failed, content, request, 2, &plan, &match) ==
          TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_artifact_match_finish(match, request, 2, TB_SYMBOL_OPERATIONAL_FAILURE, false) ==
          TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(!tb_symbol_artifact_publish(owner, failed));
    CHECK(tb_symbol_artifact_abandon(owner, failed));
    CHECK(tb_symbol_storage_unlink(storage, failed, &candidate.key));
    CHECK(tb_symbol_artifact_cleaned(owner, failed, &candidate.key));

    uint64_t second = tb_symbol_artifact_prepare(owner, origin);
    candidate = acquire(owner, second, storage, origin);
    CHECK(tb_symbol_artifact_accept(owner, second, &candidate) == TB_SYMBOL_SUCCESS);
    request = comparison_request();
    CHECK(tb_symbol_artifact_match_begin(owner, second, content, request, 2, &plan, &match) ==
          TB_SYMBOL_SUCCESS);
    CHECK(match && plan.content == content && plan.left_slot == first && plan.right_slot == second);
    CHECK(plan.bytes == 5 && plan.left_key.inode == left.key.inode &&
          plan.right_key.inode == candidate.key.inode);
    CHECK(tb_symbol_artifact_close(owner, first) && !tb_symbol_artifact_lease_current(lease));
    TbSymbolEquality_t *equal = tb_symbol_equal_create(tb_symbol_artifact_lease_descriptor(lease),
                                                       candidate.descriptor, plan.bytes, NULL);
    CHECK(equal);
    do {
        result = tb_symbol_equal_step(equal, 2);
    } while (result == TB_SYMBOL_PENDING);
    bool identical = false;
    CHECK(result == TB_SYMBOL_SUCCESS && tb_symbol_equal_result(equal, &identical) && identical);
    tb_symbol_equal_destroy(equal);
    tb_symbol_artifact_lease_release(lease);
    TbSymbolArtifactCleanup_t cleanup;
    CHECK(!tb_symbol_artifact_evict(owner, content, &cleanup));
    TbSymbolWorkIdentity_t wrong = request;
    ++wrong.revision;
    CHECK(tb_symbol_artifact_match_finish(match, wrong, 2, result, identical) == TB_SYMBOL_STALE);
    CHECK(tb_symbol_artifact_match_finish(match, request, 3, result, identical) == TB_SYMBOL_STALE);
    CHECK(tb_symbol_artifact_match_finish(match, request, 2, result, identical) == TB_SYMBOL_SUCCESS);
    CHECK(!tb_symbol_artifact_evict(owner, content, &cleanup));
    CHECK(!tb_symbol_artifact_publish(owner, second));
    CHECK(tb_symbol_artifact_share_begin(owner, second, &cleanup));
    CHECK(cleanup.slot == second && cleanup.content != content);
    CHECK(fcntl(candidate.descriptor, F_GETFD) == -1 && errno == EBADF);
    CHECK(tb_symbol_artifacts_usage(owner).bytes == 10);
    TbSymbolArtifactCleanup_t bad = cleanup;
    ++bad.key.inode;
    CHECK(!tb_symbol_artifact_share_cleaned(owner, second, bad));
    CHECK(tb_symbol_storage_unlink(storage, cleanup.slot, &cleanup.key));
    allocation = 0;
    fail_at = 1;
    CHECK(tb_symbol_artifact_share_cleaned(owner, second, cleanup));
    fail_at = 0;
    CHECK(tb_symbol_artifact_info(owner, second, &info));
    CHECK(info.context == second && info.content == content && info.state == TB_SYMBOL_ARTIFACT_RETAINED);
    CHECK(tb_symbol_artifacts_usage(owner).bytes == 5 && tb_symbol_artifacts_usage(owner).open_contexts == 1);
    CHECK(!tb_symbol_artifact_info(owner, first, &info));
    lease = tb_symbol_artifact_lease(owner, second);
    CHECK(lease && tb_symbol_artifact_lease_current(lease));
    CHECK(tb_symbol_artifact_close(owner, second));
    CHECK(!tb_symbol_artifact_evict(owner, content, &cleanup));
    tb_symbol_artifact_lease_release(lease);
    CHECK(tb_symbol_artifact_evict(owner, content, &cleanup) && cleanup.slot == first);
    CHECK(tb_symbol_storage_unlink(storage, cleanup.slot, &cleanup.key));
    CHECK(tb_symbol_artifact_backing_cleaned(owner, cleanup));
    CHECK(tb_symbol_artifacts_destroy(owner) && !budget.used);
    CHECK(tb_symbol_storage_remove(storage));
    tb_symbol_storage_destroy(storage);
}


static void quota(const char *origin, const char *path)
{
    TbSymbolStorage_t *storage = tb_symbol_storage_create(path, NULL, NULL);
    CHECK(storage);
    TbSymbolOutcome_e result;
    do {
        result = tb_symbol_storage_step(storage);
    } while (result == TB_SYMBOL_PENDING);
    CHECK(result == TB_SYMBOL_SUCCESS);
    TbSymbolArtifactIds_t identities = {0};
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 1024 * 1024));
    TbSymbolArtifacts_t *owner = tb_symbol_artifacts_create(5, 10, &identities, &budget);
    CHECK(owner);
    uint64_t contexts[2];
    uint64_t contents[2];
    for (unsigned i = 0; i < 2; ++i) {
        contexts[i] = tb_symbol_artifact_prepare(owner, origin);
        TbSymbolAcquiredBytes_t bytes = acquire(owner, contexts[i], storage, origin);
        CHECK(tb_symbol_artifact_accept(owner, contexts[i], &bytes) == TB_SYMBOL_SUCCESS);
        CHECK(tb_symbol_artifact_publish(owner, contexts[i]));
        TbSymbolArtifactInfo_t info;
        CHECK(tb_symbol_artifact_info(owner, contexts[i], &info));
        contents[i] = info.content;
    }
    CHECK(contents[0] != contents[1] && tb_symbol_artifacts_usage(owner).bytes == 10);
    uint64_t rejected = tb_symbol_artifact_prepare(owner, origin);
    CHECK(rejected);
    TbSymbolAcquire_t *operation = tb_symbol_acquire_create(origin, storage, rejected, 5, NULL);
    CHECK(operation);
    TbSymbolAcquireOffer_t offer = {0};
    bool found = false;
    for (unsigned i = 0; i < 1000; ++i) {
        CHECK(tb_symbol_acquire_step(operation, 2) == TB_SYMBOL_PENDING);
        if (tb_symbol_acquire_offer(operation, &offer.bytes)) {
            CHECK(tb_symbol_acquire_storage_key(operation, &offer.key));
            found = true;
            break;
        }
    }
    CHECK(found && tb_symbol_artifact_offer(owner, rejected, offer) == TB_SYMBOL_RESOURCE_FAILURE);
    CHECK(tb_symbol_artifact_offer(owner, rejected, offer) == TB_SYMBOL_STALE);
    TbSymbolStorageInfo_t empty;
    CHECK(tb_symbol_storage_info(storage, rejected, &empty) && !empty.bytes);
    CHECK(!tb_symbol_artifact_publish(owner, rejected));
    tb_symbol_acquire_destroy(operation);
    CHECK(tb_symbol_artifact_close(owner, rejected) && tb_symbol_artifact_abandon(owner, rejected));
    CHECK(tb_symbol_artifacts_usage(owner).bytes == 10);
    CHECK(tb_symbol_storage_unlink(storage, rejected, &offer.key));
    CHECK(tb_symbol_artifact_cleaned(owner, rejected, &offer.key));
    for (unsigned i = 0; i < 2; ++i) {
        TbSymbolArtifactInfo_t info;
        CHECK(tb_symbol_artifact_info(owner, contexts[i], &info));
        CHECK(info.state == TB_SYMBOL_ARTIFACT_RETAINED && info.content == contents[i]);
        CHECK(tb_symbol_artifact_close(owner, contexts[i]));
        TbSymbolArtifactCleanup_t cleanup;
        CHECK(tb_symbol_artifact_evict(owner, contents[i], &cleanup));
        CHECK(tb_symbol_storage_unlink(storage, cleanup.slot, &cleanup.key));
        CHECK(tb_symbol_artifact_backing_cleaned(owner, cleanup));
    }
    CHECK(tb_symbol_artifacts_destroy(owner) && !budget.used);
    CHECK(tb_symbol_storage_remove(storage));
    tb_symbol_storage_destroy(storage);
}


static void withdrawn(const char *origin, const char *path)
{
    TbSymbolStorage_t *storage = tb_symbol_storage_create(path, NULL, NULL);
    CHECK(storage);
    TbSymbolOutcome_e result;
    do {
        result = tb_symbol_storage_step(storage);
    } while (result == TB_SYMBOL_PENDING);
    CHECK(result == TB_SYMBOL_SUCCESS);
    TbSymbolArtifactIds_t identities = {0};
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 1024 * 1024));
    TbSymbolArtifacts_t *owner = tb_symbol_artifacts_create(16, 32, &identities, &budget);
    CHECK(owner);
    uint64_t first = tb_symbol_artifact_prepare(owner, origin);
    TbSymbolAcquiredBytes_t bytes = acquire(owner, first, storage, origin);
    CHECK(tb_symbol_artifact_accept(owner, first, &bytes) == TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_artifact_publish(owner, first));
    TbSymbolArtifactInfo_t info;
    CHECK(tb_symbol_artifact_info(owner, first, &info) && info.content_revision == 1);
    uint64_t content = info.content;
    TbSymbolArtifactLease_t *lease = tb_symbol_artifact_lease(owner, first);
    CHECK(lease && tb_symbol_artifact_lease_current(lease));
    uint64_t second = tb_symbol_artifact_prepare(owner, origin);
    TbSymbolAcquiredBytes_t candidate = acquire(owner, second, storage, origin);
    CHECK(tb_symbol_artifact_accept(owner, second, &candidate) == TB_SYMBOL_SUCCESS);
    TbSymbolWorkIdentity_t request = comparison_request();
    TbSymbolArtifactMatchPlan_t plan;
    TbSymbolArtifactMatch_t *match = NULL;
    CHECK(tb_symbol_artifact_match_begin(owner, second, content, request, 2, &plan, &match) ==
          TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_artifact_withdraw(owner, content));
    CHECK(!tb_symbol_artifact_withdraw(owner, content));
    CHECK(tb_symbol_artifact_info(owner, first, &info) && info.state == TB_SYMBOL_ARTIFACT_UNAVAILABLE &&
          info.content == content && info.content_revision == 2);
    CHECK(!tb_symbol_artifact_lease_current(lease) && !tb_symbol_artifact_lease(owner, first));
    CHECK(fcntl(tb_symbol_artifact_lease_descriptor(lease), F_GETFD) >= 0);
    CHECK(tb_symbol_artifacts_usage(owner).bytes == 10);
    /* A late positive completion cannot restore withdrawn correspondence. */
    CHECK(tb_symbol_artifact_match_finish(match, request, 2, TB_SYMBOL_SUCCESS, true) == TB_SYMBOL_STALE);
    CHECK(!tb_symbol_artifact_publish(owner, second));
    CHECK(tb_symbol_artifact_abandon(owner, second));
    CHECK(tb_symbol_storage_unlink(storage, second, &candidate.key));
    CHECK(tb_symbol_artifact_cleaned(owner, second, &candidate.key));
    CHECK(tb_symbol_artifact_close(owner, first));
    TbSymbolArtifactCleanup_t cleanup;
    CHECK(!tb_symbol_artifact_evict(owner, content, &cleanup));
    tb_symbol_artifact_lease_release(lease);
    CHECK(tb_symbol_artifact_evict(owner, content, &cleanup));
    CHECK(tb_symbol_storage_unlink(storage, cleanup.slot, &cleanup.key));
    CHECK(tb_symbol_artifact_backing_cleaned(owner, cleanup));
    CHECK(tb_symbol_artifacts_destroy(owner) && !budget.used);
    CHECK(tb_symbol_storage_remove(storage));
    tb_symbol_storage_destroy(storage);
}


int main(void)
{
    configuration();
    allocation_failures();
    context_identities();
    char parent[] = "/tmp/tb-symbol-artifact-XXXXXX";
    CHECK(mkdtemp(parent));
    char origin[256];
    char path[256];
    CHECK(snprintf(origin, sizeof(origin), "%s/origin", parent) > 0);
    CHECK(snprintf(path, sizeof(path), "%s/pool", parent) > 0);
    int descriptor = open(origin, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    CHECK(descriptor >= 0 && write(descriptor, "a\0bcd", 5) == 5 && !close(descriptor));
    retained(origin, path);
    shared(origin, path);
    quota(origin, path);
    withdrawn(origin, path);
    CHECK(!unlink(origin) && !rmdir(parent));
    return 0;
}
