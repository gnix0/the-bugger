#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Explicit close-on-exec descriptor copies. */
#include "symbol/helper.h"
#include "symbol/protocol.h"
#include "symbol/companion_receipt.h"
#include "symbol/admission.h"
#include "symbol/operation.h"
#include "symbol/worker.h"
#include "../support/check.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static size_t fail_bytes;
static unsigned fail_match;
static unsigned matching_allocations;

void *__real_malloc(size_t bytes);
void *__wrap_malloc(size_t bytes);

void *__wrap_malloc(size_t bytes)
{
    if (fail_bytes && bytes == fail_bytes && ++matching_allocations == fail_match)
        return NULL;

    return __real_malloc(bytes);
}


typedef struct {
    unsigned char bytes[16384];
    size_t        length;
} Arguments_t;

typedef struct {
    TbSymbolHelper_t          *helper;
    TbSymbolAdmissionLedger_t *ledger;
    TbSymbolReservation_t     *tickets[2];
    TbSymbolBackingCharge_t   *backings[2];
    int                        descriptors[2];
    TbSymbolWorkIdentity_t      identity;
    TbSymbolHelperResult_t      result;
    unsigned char              bytes[16384];
    uint64_t                   process_generation;
    bool                       complete;
    bool                       retired;
} Fixture_t;

static bool emit(void *context, const TbSymbolWorkIdentity_t *identity, TbSymbolHelperResult_t result)
{
    Fixture_t *fixture = context;
    CHECK(!fixture->complete && identity->request == fixture->identity.request);
    CHECK(identity->consumer == 3 && identity->revision == 7 && identity->generation == 11);
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


static bool retired(void *context, const TbSymbolWorkIdentity_t *identity, uint64_t generation)
{
    Fixture_t *fixture = context;
    CHECK(fixture->complete && !fixture->retired && identity->request == fixture->identity.request);
    CHECK(generation == fixture->process_generation);
    fixture->retired = true;
    return true;
}


static void pump(Fixture_t *fixture)
{
    tb_symbol_helper_pump_with_release(fixture->helper, emit, retired, fixture);
    struct pollfd fds[2];
    size_t count = tb_symbol_helper_pollfds(fixture->helper, fds);
    CHECK(count <= 2 && poll(fds, (nfds_t)count, tb_symbol_helper_timeout(fixture->helper, 2)) >= 0);
}


static void start(Fixture_t *fixture, const char *executable, size_t decoder_bytes)
{
    TbSymbolHelperConfig_t config;
    tb_symbol_helper_defaults(&config);
    CHECK(!config.decoder_bytes);
    config.decoder_bytes = decoder_bytes;
    config.checkpoint_bytes = 64;
    config.checkpoint_steps = 1;
    fixture->helper = tb_symbol_helper_create(&config, NULL);
    CHECK(fixture->helper && tb_symbol_helper_start(fixture->helper, executable));
    for (unsigned turn = 0; turn < 4000 &&
         tb_symbol_helper_state(fixture->helper) != TB_SYMBOL_HELPER_READY; ++turn) {
        pump(fixture);
    }
    CHECK(tb_symbol_helper_state(fixture->helper) == TB_SYMBOL_HELPER_READY);
    fixture->process_generation = tb_symbol_helper_process_generation(fixture->helper);
}


static void close_helper(Fixture_t *fixture)
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


static void submit(Fixture_t *fixture, uint32_t code, const Arguments_t *arguments)
{
    CHECK(!tb_symbol_helper_jobs(fixture->helper));
    ++fixture->identity.request;
    fixture->complete = false;
    fixture->retired = false;
    fixture->result = (TbSymbolHelperResult_t) {
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
    TbSymbolAdmission_e admission = tb_symbol_helper_submit(fixture->helper, &request);
    if (admission != TB_SYMBOL_ADMITTED) {
        fprintf(stderr, "companion admission %u for request %llu, helper %u: %s\n", admission,
                (unsigned long long)request.identity.request, tb_symbol_helper_state(fixture->helper),
                tb_symbol_helper_error(fixture->helper));
    }
    CHECK(admission == TB_SYMBOL_ADMITTED);
}


static void finish(Fixture_t *fixture, TbSymbolOutcome_e expected)
{
    for (unsigned turn = 0; turn < 6000 && !fixture->retired; ++turn)
        pump(fixture);
    if (!fixture->retired || fixture->result.outcome != expected) {
        fprintf(stderr, "companion request %llu: expected %u, got %u, detail %u; %s\n",
                (unsigned long long)fixture->identity.request, expected, fixture->result.outcome,
                fixture->result.detail, tb_symbol_helper_error(fixture->helper));
    }
    CHECK(fixture->complete && fixture->retired && fixture->result.outcome == expected);
    CHECK(!tb_symbol_helper_jobs(fixture->helper));
    if (expected != TB_SYMBOL_SUCCESS)
        CHECK(!fixture->result.length && fixture->result.descriptor < 0);
}


static TbSymbolStorageInfo_t acquire(Fixture_t *fixture, const char *origin, const char *pool,
    TbSymbolStorageKey_t root, uint64_t slot)
{
    Arguments_t arguments = {0};
    word(&arguments, 256 * 1024 * 1024);
    word(&arguments, slot);
    word(&arguments, root.device);
    word(&arguments, root.inode);
    paths(&arguments, origin, pool);
    submit(fixture, TB_SYMBOL_OPERATION_ACQUIRE, &arguments);

    CHECK(slot >= 1 && slot <= 2);
    TbSymbolReservation_t *ticket = tb_symbol_admission_prepare(fixture->ledger);
    CHECK(ticket);
    TbSymbolAcquireOffer_t offer;
    bool offered = false;
    for (unsigned turn = 0; turn < 6000 && !offered; ++turn) {
        pump(fixture);
        offered = tb_symbol_helper_offer(fixture->helper, fixture->identity, &offer);
        CHECK(!fixture->complete);
    }
    CHECK(offered && tb_symbol_admission_assign(ticket, offer.bytes));
    CHECK(tb_symbol_helper_grant(fixture->helper, fixture->identity, offer.bytes));
    finish(fixture, TB_SYMBOL_SUCCESS);
    CHECK(fixture->result.descriptor >= 0 && fixture->result.length == 56);
    CHECK(tb_symbol_protocol_get(fixture->bytes, 8) == offer.bytes);
    TbSymbolStorageInfo_t info = {
        .key   = offer.key,
        .bytes = offer.bytes
    };
    fixture->tickets[slot - 1] = ticket;
    fixture->backings[slot - 1] = tb_symbol_admission_commit(ticket, NULL);
    fixture->descriptors[slot - 1] = fixture->result.descriptor;
    CHECK(fixture->backings[slot - 1]);
    fixture->result.descriptor = -1;

    return info;
}


static Arguments_t inspect_arguments(const char *pool, TbSymbolStorageKey_t root, uint64_t slot,
    TbSymbolStorageInfo_t file, size_t name_capacity, size_t id_capacity)
{
    Arguments_t arguments = {0};
    word(&arguments, file.bytes);
    word(&arguments, slot);
    word(&arguments, root.device);
    word(&arguments, root.inode);
    word(&arguments, file.key.device);
    word(&arguments, file.key.inode);
    word(&arguments, name_capacity);
    word(&arguments, id_capacity);
    paths(&arguments, pool, NULL);
    return arguments;
}


static TbSymbolCompanionInventory_t inspect(Fixture_t *fixture, const char *pool, TbSymbolStorageKey_t root,
    uint64_t slot, TbSymbolStorageInfo_t file, unsigned char *retained, size_t capacity)
{
    Arguments_t arguments = inspect_arguments(pool, root, slot, file, 4096, 4096);
    submit(fixture, TB_SYMBOL_OPERATION_COMPANION_INSPECT, &arguments);
    finish(fixture, TB_SYMBOL_SUCCESS);
    CHECK(fixture->result.descriptor < 0 && fixture->result.length <= capacity);
    memcpy(retained, fixture->bytes, fixture->result.length);
    TbSymbolCompanionInventory_t view;
    CHECK(tb_symbol_companion_receipt_decode(retained, fixture->result.length, &view));
    CHECK(view.backing.bytes == file.bytes && view.backing.key.device == file.key.device &&
          view.backing.key.inode == file.key.inode);

    return view;
}


static void cleanup_file(Fixture_t *fixture, const char *pool, TbSymbolStorageKey_t root,
    uint64_t slot, TbSymbolStorageKey_t key)
{
    CHECK(slot >= 1 && slot <= 2 && close(fixture->descriptors[slot - 1]) == 0);
    CHECK(tb_symbol_admission_close(fixture->tickets[slot - 1]));
    CHECK(tb_symbol_admission_evict(fixture->backings[slot - 1]));
    Arguments_t arguments = {0};
    word(&arguments, slot);
    word(&arguments, root.device);
    word(&arguments, root.inode);
    word(&arguments, key.device);
    word(&arguments, key.inode);
    paths(&arguments, pool, NULL);
    submit(fixture, TB_SYMBOL_OPERATION_REMOVE_FILE, &arguments);
    finish(fixture, TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_admission_backing_cleaned(fixture->backings[slot - 1]));
}


static void malformed(const Arguments_t *complete)
{
    TbSymbolBudget_t control;
    TbSymbolBudget_t decoder;
    CHECK(tb_symbol_budget_init(&control, 1024 * 1024));
    CHECK(tb_symbol_budget_init(&decoder, 1024 * 1024));
    for (size_t length = 0; length < complete->length; ++length) {
        TbSymbolOperation_t *operation = tb_symbol_operation_create(TB_SYMBOL_OPERATION_COMPANION_INSPECT,
            complete->bytes, length, (TbSymbolOperationBudgets_t) {
                .control = &control,
                .decoder = &decoder
            });
        CHECK(operation);
        TbSymbolWorkResult_t result = {0};
        for (unsigned i = 0; i < 10000 && result.outcome == TB_SYMBOL_PENDING; ++i)
            result = tb_symbol_operation_step(operation, 1);
        CHECK(result.outcome == TB_SYMBOL_MALFORMED);
        TbSymbolOperationResult_t receipt = tb_symbol_operation_result(operation);
        CHECK(!receipt.length && !receipt.payload && receipt.descriptor < 0);
        tb_symbol_operation_destroy(operation);
        CHECK(control.used == 0 && decoder.used == 0);
    }
    TbSymbolOperation_t *disabled = tb_symbol_operation_create(TB_SYMBOL_OPERATION_COMPANION_INSPECT,
        complete->bytes, complete->length, (TbSymbolOperationBudgets_t) {
            .control = &control
        });
    CHECK(disabled);
    TbSymbolWorkResult_t result = {0};
    for (unsigned i = 0; i < 10000 && result.outcome == TB_SYMBOL_PENDING; ++i)
        result = tb_symbol_operation_step(disabled, 64);
    CHECK(result.outcome == TB_SYMBOL_UNSUPPORTED);
    CHECK(tb_symbol_operation_result(disabled).detail == TB_SYMBOL_DETAIL_ELF_DISABLED);
    tb_symbol_operation_destroy(disabled);
    CHECK(control.used == 0);
}


static void publication_failures(Fixture_t *fixture, const char *executable, const char *pool,
    TbSymbolStorageKey_t root, TbSymbolStorageInfo_t file, size_t receipt_bytes)
{
    for (unsigned mode = 0; mode < 3; ++mode) {
        close_helper(fixture);
        char bytes_text[32];
        char match_text[32];
        CHECK(snprintf(bytes_text, sizeof(bytes_text), "%zu",
              receipt_bytes + (mode ? TB_SYMBOL_RESULT_BYTES : 0)) > 0);
        CHECK(snprintf(match_text, sizeof(match_text), "%u", mode == 2 ? 2U : 1U) > 0);
        CHECK(setenv("TB_TEST_COMPANION_FAIL_BYTES", bytes_text, 1) == 0);
        CHECK(setenv("TB_TEST_COMPANION_FAIL_MATCH", match_text, 1) == 0);
        start(fixture, NULL, 1024 * 1024);
        CHECK(unsetenv("TB_TEST_COMPANION_FAIL_BYTES") == 0);
        CHECK(unsetenv("TB_TEST_COMPANION_FAIL_MATCH") == 0);
        Arguments_t arguments = inspect_arguments(pool, root, 1, file, 4096, 4096);
        submit(fixture, TB_SYMBOL_OPERATION_COMPANION_INSPECT, &arguments);
        finish(fixture, TB_SYMBOL_RESOURCE_FAILURE);
        CHECK(fixture->result.descriptor < 0 && !fixture->result.length);
        if (mode)
            CHECK(fixture->result.detail == TB_SYMBOL_DETAIL_PUBLICATION);
        CHECK(tb_symbol_helper_state(fixture->helper) == TB_SYMBOL_HELPER_READY);

        /* Small legacy receipts still publish after the large-result failure. */
        arguments = (Arguments_t) {0};
        word(&arguments, file.bytes);
        word(&arguments, 1);
        word(&arguments, root.device);
        word(&arguments, root.inode);
        word(&arguments, file.key.device);
        word(&arguments, file.key.inode);
        word(&arguments, UINT64_MAX);
        paths(&arguments, pool, NULL);
        submit(fixture, TB_SYMBOL_OPERATION_ELF_VALIDATE, &arguments);
        finish(fixture, TB_SYMBOL_SUCCESS);
        CHECK(fixture->result.length == 56);
    }
    close_helper(fixture);
    start(fixture, executable, 1024 * 1024);
}


static void copy_origin(const char *from, const char *to)
{
    int input = open(from, O_RDONLY | O_CLOEXEC);
    int output = open(to, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    CHECK(input >= 0 && output >= 0);
    unsigned char bytes[4096];
    ssize_t count;
    while ((count = read(input, bytes, sizeof(bytes))) > 0)
        CHECK(write(output, bytes, (size_t)count) == count);
    CHECK(count == 0 && close(input) == 0 && close(output) == 0);
}


int main(int argc, char **argv)
{
    if (argc == 2 && strcmp(argv[1], "--symbol-worker") == 0) {
        const char *bytes = getenv("TB_TEST_COMPANION_FAIL_BYTES");
        const char *match = getenv("TB_TEST_COMPANION_FAIL_MATCH");
        fail_bytes = bytes ? (size_t)strtoull(bytes, NULL, 10) : 0;
        fail_match = match ? (unsigned)strtoul(match, NULL, 10) : 0;
        return tb_symbol_worker_main(3);
    }
    CHECK(argc == 4);
    char parent[] = "/tmp/tb-companion-discovery-XXXXXX";
    CHECK(mkdtemp(parent));
    char pool[4096];
    CHECK(snprintf(pool, sizeof(pool), "%s/pool", parent) > 0);
    char primary_origin[4096];
    char companion_origin[4096];
    CHECK(snprintf(primary_origin, sizeof(primary_origin), "%s/primary", parent) > 0);
    const char *basename = strrchr(argv[3], '/');
    CHECK(basename);
    CHECK(snprintf(companion_origin, sizeof(companion_origin), "%s/%s", parent, basename + 1) > 0);
    copy_origin(argv[2], primary_origin);
    copy_origin(argv[3], companion_origin);
    Fixture_t fixture = {
        .identity = {
            .consumer   = 3,
            .revision   = 7,
            .generation = 11
        }
    };
    fixture.ledger = tb_symbol_admission_create(256 * 1024 * 1024, 1024 * 1024 * 1024, NULL);
    CHECK(fixture.ledger);
    start(&fixture, argv[1], 1024 * 1024);
    Arguments_t arguments = {0};
    paths(&arguments, pool, NULL);
    submit(&fixture, TB_SYMBOL_OPERATION_CREATE_STORAGE, &arguments);
    finish(&fixture, TB_SYMBOL_SUCCESS);
    CHECK(fixture.result.length == 16);
    TbSymbolStorageKey_t root = {
        .device = tb_symbol_protocol_get(fixture.bytes, 8),
        .inode  = tb_symbol_protocol_get(fixture.bytes + 8, 8)
    };
    TbSymbolStorageInfo_t primary_bytes = acquire(&fixture, primary_origin, pool, root, 1);
    unsigned char primary_receipt[16384];
    unsigned char companion_receipt[16384];
    TbSymbolCompanionInventory_t primary = inspect(&fixture, pool, root, 1, primary_bytes,
        primary_receipt, sizeof(primary_receipt));
    CHECK(primary.name_bytes && primary.id_bytes);
    unsigned char candidate[4096];
    TbSymbolCompanionPaths_t planner;
    TbSymbolCompanionPathsRequest_t request = {
        .origin = {
            .data  = (const unsigned char *)primary_origin,
            .bytes = strlen(primary_origin)
        },
        .debuglink = {
            .data  = primary.name,
            .bytes = primary.name_bytes
        },
        .build_id = {
            .data  = primary.build_id,
            .bytes = primary.id_bytes
        }
    };
    CHECK(tb_symbol_companion_paths_init(&planner, request, candidate, sizeof(candidate)) ==
          TB_SYMBOL_COMPANION_PATH_PENDING);
    TbSymbolCompanionPathStatus_e status;
    do {
        status = tb_symbol_companion_paths_step(&planner, 17);
    } while (status == TB_SYMBOL_COMPANION_PATH_PENDING);
    CHECK(status == TB_SYMBOL_COMPANION_PATH_READY && strcmp((const char *)candidate, companion_origin) == 0);
    TbSymbolStorageInfo_t companion_bytes = acquire(&fixture, (const char *)candidate, pool, root, 2);
    TbSymbolCompanionInventory_t companion = inspect(&fixture, pool, root, 2, companion_bytes,
        companion_receipt, sizeof(companion_receipt));
    CHECK(primary.backing.key.inode != companion.backing.key.inode);
    CHECK(tb_symbol_companion_relation(&primary, &companion, TB_SYMBOL_COMPANION_BY_DEBUGLINK) ==
          TB_SYMBOL_COMPANION_RELATION_MATCH);
    CHECK(tb_symbol_companion_relation(&primary, &companion, TB_SYMBOL_COMPANION_BY_BUILD_ID) ==
          TB_SYMBOL_COMPANION_RELATION_MATCH);
    printf("PAIR ELF%u CRC=%08x ID=", primary.header.address_bytes * 8, companion.file_crc);
    for (size_t i = 0; i < primary.id_bytes; ++i)
        printf("%02x", primary.build_id[i]);
    puts("");

    int changed = open(primary_origin, O_WRONLY | O_TRUNC | O_CLOEXEC);
    CHECK(changed >= 0 && write(changed, "changed", 7) == 7 && close(changed) == 0);
    changed = open(companion_origin, O_WRONLY | O_TRUNC | O_CLOEXEC);
    CHECK(changed >= 0 && write(changed, "changed", 7) == 7 && close(changed) == 0);
    TbSymbolAdmissionUsage_t usage = tb_symbol_admission_usage(fixture.ledger);
    CHECK(usage.open_contexts == 2 && usage.bytes == primary_bytes.bytes + companion_bytes.bytes);

    /* A mismatched complete-file CRC cannot establish the relationship. */
    TbSymbolCompanionInventory_t mismatched = companion;
    ++mismatched.file_crc;
    CHECK(tb_symbol_companion_relation(&primary, &mismatched, TB_SYMBOL_COMPANION_BY_DEBUGLINK) ==
          TB_SYMBOL_COMPANION_RELATION_CRC_MISMATCH);

    /* Missing subsequent candidate is an explicit failed operation. */
    CHECK(tb_symbol_companion_paths_next(&planner));
    do {
        status = tb_symbol_companion_paths_step(&planner, 17);
    } while (status == TB_SYMBOL_COMPANION_PATH_PENDING);
    CHECK(status == TB_SYMBOL_COMPANION_PATH_READY);
    arguments = (Arguments_t) {0};
    word(&arguments, 256 * 1024 * 1024);
    word(&arguments, 3);
    word(&arguments, root.device);
    word(&arguments, root.inode);
    paths(&arguments, (const char *)candidate, pool);
    submit(&fixture, TB_SYMBOL_OPERATION_ACQUIRE, &arguments);
    finish(&fixture, TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(fixture.result.system_error == ENOENT);
    CHECK(!(fixture.result.detail & TB_SYMBOL_DETAIL_HAS_KEY));
    arguments = (Arguments_t) {0};
    word(&arguments, 3);
    word(&arguments, root.device);
    word(&arguments, root.inode);
    paths(&arguments, pool, NULL);
    submit(&fixture, TB_SYMBOL_OPERATION_DISCARD_EMPTY, &arguments);
    finish(&fixture, TB_SYMBOL_SUCCESS);

    arguments = inspect_arguments(pool, root, 1, primary_bytes, 4096, 4096);
    malformed(&arguments);
    publication_failures(&fixture, argv[1], pool, root, primary_bytes,
        tb_symbol_companion_receipt_size(&primary));

    unsigned char after[16384];
    TbSymbolCompanionInventory_t still_primary = inspect(&fixture, pool, root, 1, primary_bytes,
        after, sizeof(after));
    CHECK(still_primary.file_crc == primary.file_crc && still_primary.id_bytes == primary.id_bytes);
    CHECK(memcmp(still_primary.build_id, primary.build_id, primary.id_bytes) == 0);

    arguments = inspect_arguments(pool, root, 1, primary_bytes, 4, 4096);
    submit(&fixture, TB_SYMBOL_OPERATION_COMPANION_INSPECT, &arguments);
    finish(&fixture, TB_SYMBOL_RESOURCE_FAILURE);
    CHECK((fixture.result.detail & TB_SYMBOL_DETAIL_COMPANION) == TB_SYMBOL_DETAIL_COMPANION);
    arguments = inspect_arguments(pool, root, 1, primary_bytes, 0, 4096);
    submit(&fixture, TB_SYMBOL_OPERATION_COMPANION_INSPECT, &arguments);
    finish(&fixture, TB_SYMBOL_MALFORMED);
    ++primary_bytes.key.inode;
    arguments = inspect_arguments(pool, root, 1, primary_bytes, 4096, 4096);
    submit(&fixture, TB_SYMBOL_OPERATION_COMPANION_INSPECT, &arguments);
    finish(&fixture, TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(fixture.result.system_error == ESTALE);
    --primary_bytes.key.inode;

    cleanup_file(&fixture, pool, root, 1, primary_bytes.key);
    cleanup_file(&fixture, pool, root, 2, companion_bytes.key);
    arguments = (Arguments_t) {0};
    word(&arguments, root.device);
    word(&arguments, root.inode);
    paths(&arguments, pool, NULL);
    submit(&fixture, TB_SYMBOL_OPERATION_REMOVE_STORAGE, &arguments);
    finish(&fixture, TB_SYMBOL_SUCCESS);
    close_helper(&fixture);
    CHECK(tb_symbol_admission_destroy(fixture.ledger));
    CHECK(unlink(primary_origin) == 0 && unlink(companion_origin) == 0 && rmdir(parent) == 0);
    puts("PASS supervised discovery, independent receipts, publication failures and retained primary");
    return 0;
}
