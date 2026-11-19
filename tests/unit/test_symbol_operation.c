#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Private Linux filesystem fixtures. */
#include "symbol/operation.h"
#include "../support/check.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool fail_allocation;

void *__real_calloc(size_t count, size_t length);
void *__wrap_calloc(size_t count, size_t length);

void *__wrap_calloc(size_t count, size_t length)
{
    return fail_allocation ? NULL : __real_calloc(count, length);
}


typedef struct {
    unsigned char bytes[1024];
    size_t        length;
} Arguments_t;

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


static TbSymbolOperation_t *operation(uint32_t code, const Arguments_t *arguments, TbSymbolBudget_t *budget)
{
    TbSymbolOperation_t *state = tb_symbol_operation_create(code, arguments->bytes, arguments->length, budget);
    CHECK(state);
    return state;
}


static TbSymbolOutcome_e finish(TbSymbolOperation_t *state, size_t slice)
{
    uint64_t previous = 0;
    for (unsigned i = 0; i < 100000; ++i) {
        TbSymbolWorkResult_t result = tb_symbol_operation_step(state, slice);
        CHECK(result.progress >= previous);
        previous = result.progress;
        if (result.outcome != TB_SYMBOL_PENDING) {
            TbSymbolWorkResult_t again = tb_symbol_operation_step(state, slice);
            CHECK(again.outcome == result.outcome && again.progress == result.progress);
            CHECK(!tb_symbol_operation_grant(state, 0));
            return result.outcome;
        }
    }
    CHECK(false);
    return TB_SYMBOL_PENDING;
}


static TbSymbolStorageKey_t create_namespace(const char *path, TbSymbolBudget_t *budget)
{
    Arguments_t arguments = {0};
    paths(&arguments, path, NULL);
    TbSymbolOperation_t *state = operation(TB_SYMBOL_OPERATION_CREATE_STORAGE, &arguments, budget);
    struct stat native;
    CHECK(stat(path, &native) && errno == ENOENT);
    CHECK(finish(state, 1) == TB_SYMBOL_SUCCESS);
    TbSymbolOperationResult_t result = tb_symbol_operation_result(state);
    CHECK(result.length == 16 && result.descriptor < 0 && !result.system_error);
    CHECK((result.detail & TB_SYMBOL_DETAIL_CREATED) && (result.detail & TB_SYMBOL_DETAIL_HAS_KEY));
    TbSymbolStorageKey_t key = {
        .device = tb_symbol_protocol_get(result.payload, 8),
        .inode  = tb_symbol_protocol_get(result.payload + 8, 8)
    };
    CHECK(key.device == result.key.device && key.inode == result.key.inode);
    tb_symbol_operation_destroy(state);
    CHECK(!budget->used && !stat(path, &native));
    return key;
}


static Arguments_t acquire_arguments(const char *origin, const char *path, TbSymbolStorageKey_t key,
                                     uint64_t slot)
{
    Arguments_t arguments = {0};
    word(&arguments, 100000);
    word(&arguments, slot);
    word(&arguments, key.device);
    word(&arguments, key.inode);
    paths(&arguments, origin, path);
    return arguments;
}


static TbSymbolStorageInfo_t acquire_file(const char *origin, const char *path, TbSymbolStorageKey_t key,
                                          uint64_t slot, TbSymbolBudget_t *budget)
{
    Arguments_t arguments = acquire_arguments(origin, path, key, slot);
    TbSymbolOperation_t *state = operation(TB_SYMBOL_OPERATION_ACQUIRE, &arguments, budget);
    TbSymbolAcquireOffer_t offer;
    bool offered = false;
    CHECK(!tb_symbol_operation_grant(state, 5));
    for (unsigned i = 0; i < 4096; ++i) {
        CHECK(tb_symbol_operation_step(state, 2).outcome == TB_SYMBOL_PENDING);
        if (tb_symbol_operation_offer(state, &offer)) {
            offered = true;
            break;
        }
    }
    CHECK(offered && offer.bytes == 5);
    char leaf[512];
    CHECK(snprintf(leaf, sizeof(leaf), "%s/artifact-%016llx", path, (unsigned long long)slot) > 0);
    struct stat native;
    CHECK(!stat(leaf, &native) && !native.st_size && (uint64_t)native.st_ino == offer.key.inode);
    uint64_t progress = tb_symbol_operation_step(state, 2).progress;
    for (unsigned i = 0; i < 32; ++i) {
        TbSymbolWorkResult_t idle = tb_symbol_operation_step(state, 2);
        CHECK(idle.outcome == TB_SYMBOL_PENDING && idle.progress == progress);
        CHECK(!stat(leaf, &native) && !native.st_size);
    }
    CHECK(!tb_symbol_operation_grant(state, 4));
    CHECK(tb_symbol_operation_grant(state, offer.bytes));
    CHECK(!tb_symbol_operation_grant(state, offer.bytes));
    CHECK(finish(state, 2) == TB_SYMBOL_SUCCESS);
    TbSymbolOperationResult_t result = tb_symbol_operation_result(state);
    CHECK(result.length == 56 && result.descriptor >= 0);
    CHECK(tb_symbol_protocol_get(result.payload, 8) == 5);
    CHECK(tb_symbol_protocol_get(result.payload + 8, 8) == offer.key.device);
    CHECK(tb_symbol_protocol_get(result.payload + 16, 8) == offer.key.inode);
    CHECK((fcntl(result.descriptor, F_GETFD) & FD_CLOEXEC) != 0);
    CHECK((fcntl(result.descriptor, F_GETFL) & O_ACCMODE) == O_RDONLY);
    unsigned char bytes[5];
    CHECK(pread(result.descriptor, bytes, sizeof(bytes), 0) == 5 && !memcmp(bytes, "a\0bcd", 5));
    TbSha256_t hash;
    unsigned char digest[32];
    tb_sha256_init(&hash);
    CHECK(tb_sha256_update(&hash, bytes, sizeof(bytes)) && tb_sha256_final(&hash, sizeof(bytes), digest));
    CHECK(!memcmp(result.payload + 24, digest, sizeof(digest)));
    tb_symbol_operation_destroy(state);
    CHECK(!budget->used && !stat(leaf, &native) && native.st_size == 5);
    return (TbSymbolStorageInfo_t){
        .key   = offer.key,
        .bytes = offer.bytes
    };
}


static void compare_files(const char *path, TbSymbolStorageKey_t root, TbSymbolStorageInfo_t left,
                          TbSymbolStorageInfo_t right, TbSymbolBudget_t *budget)
{
    Arguments_t arguments = {0};
    word(&arguments, left.bytes);
    word(&arguments, root.device);
    word(&arguments, root.inode);
    word(&arguments, 1);
    word(&arguments, left.key.device);
    word(&arguments, left.key.inode);
    word(&arguments, 2);
    word(&arguments, right.key.device);
    word(&arguments, right.key.inode);
    paths(&arguments, path, NULL);
    TbSymbolOperation_t *state = operation(TB_SYMBOL_OPERATION_COMPARE, &arguments, budget);
    CHECK(finish(state, 2) == TB_SYMBOL_SUCCESS);
    TbSymbolOperationResult_t result = tb_symbol_operation_result(state);
    CHECK(result.length == 1 && result.payload[0] == 1 && result.descriptor < 0);
    tb_symbol_operation_destroy(state);
    CHECK(!budget->used);

    /* A replacement key cannot become equality evidence. */
    tb_symbol_protocol_put(arguments.bytes + 64, right.key.inode + 1, 8);
    state = operation(TB_SYMBOL_OPERATION_COMPARE, &arguments, budget);
    CHECK(finish(state, 2) == TB_SYMBOL_OPERATIONAL_FAILURE);
    result = tb_symbol_operation_result(state);
    CHECK(result.system_error == ESTALE && !result.payload && !result.length && result.descriptor < 0);
    CHECK(result.detail == (TB_SYMBOL_DETAIL_COMPARE | UINT32_C(2)));
    tb_symbol_operation_destroy(state);
    CHECK(!budget->used);
}


static void remove_file(const char *path, TbSymbolStorageKey_t root, uint64_t slot,
                        TbSymbolStorageKey_t file, TbSymbolBudget_t *budget)
{
    Arguments_t arguments = {0};
    word(&arguments, slot);
    word(&arguments, root.device);
    word(&arguments, root.inode);
    word(&arguments, file.device);
    word(&arguments, file.inode);
    paths(&arguments, path, NULL);
    TbSymbolOperation_t *state = operation(TB_SYMBOL_OPERATION_REMOVE_FILE, &arguments, budget);
    CHECK(finish(state, 3) == TB_SYMBOL_SUCCESS);
    TbSymbolOperationResult_t result = tb_symbol_operation_result(state);
    CHECK(!result.length && result.descriptor < 0);
    tb_symbol_operation_destroy(state);
    CHECK(!budget->used);
}


static void remove_namespace(const char *path, TbSymbolStorageKey_t root, TbSymbolBudget_t *budget)
{
    Arguments_t arguments = {0};
    word(&arguments, root.device);
    word(&arguments, root.inode);
    paths(&arguments, path, NULL);
    TbSymbolOperation_t *state = operation(TB_SYMBOL_OPERATION_REMOVE_STORAGE, &arguments, budget);
    CHECK(finish(state, 3) == TB_SYMBOL_SUCCESS);
    tb_symbol_operation_destroy(state);
    CHECK(!budget->used);
}


static void discard_empty(const char *path, TbSymbolStorageKey_t root, TbSymbolBudget_t *budget)
{
    TbSymbolStorage_t *storage = tb_symbol_storage_create(path, &root, NULL);
    CHECK(storage);
    while (tb_symbol_storage_step(storage) == TB_SYMBOL_PENDING) {}
    int writer = tb_symbol_storage_writer(storage, 3);
    CHECK(writer >= 0);
    Arguments_t arguments = {0};
    word(&arguments, 3);
    word(&arguments, root.device);
    word(&arguments, root.inode);
    paths(&arguments, path, NULL);
    CHECK(write(writer, "x", 1) == 1);
    TbSymbolOperation_t *state = operation(TB_SYMBOL_OPERATION_DISCARD_EMPTY, &arguments, budget);
    CHECK(finish(state, 4) == TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(tb_symbol_operation_result(state).system_error == ESTALE);
    tb_symbol_operation_destroy(state);
    CHECK(!ftruncate(writer, 0) && !close(writer));
    tb_symbol_storage_destroy(storage);
    state = operation(TB_SYMBOL_OPERATION_DISCARD_EMPTY, &arguments, budget);
    CHECK(finish(state, 4) == TB_SYMBOL_SUCCESS);
    tb_symbol_operation_destroy(state);
    state = operation(TB_SYMBOL_OPERATION_DISCARD_EMPTY, &arguments, budget);
    CHECK(finish(state, 4) == TB_SYMBOL_SUCCESS);
    tb_symbol_operation_destroy(state);
    CHECK(!budget->used);
}


static void cancellation(const char *origin, const char *path, TbSymbolStorageKey_t root,
                         TbSymbolBudget_t *budget)
{
    for (unsigned stop = 0; stop < 180; ++stop) {
        uint64_t slot = 10 + stop;
        Arguments_t arguments = acquire_arguments(origin, path, root, slot);
        TbSymbolOperation_t *state = operation(TB_SYMBOL_OPERATION_ACQUIRE, &arguments, budget);
        bool granted = false;
        for (unsigned i = 0; i < stop; ++i) {
            TbSymbolWorkResult_t result = tb_symbol_operation_step(state, 2);
            TbSymbolAcquireOffer_t offer;
            if (!granted && tb_symbol_operation_offer(state, &offer)) {
                CHECK(tb_symbol_operation_grant(state, offer.bytes));
                granted = true;
            }
            if (result.outcome != TB_SYMBOL_PENDING)
                break;
        }
        TbSymbolOperationResult_t result = tb_symbol_operation_result(state);
        int descriptor = result.descriptor;
        tb_symbol_operation_destroy(state);
        CHECK(!budget->used);
        if (descriptor >= 0)
            CHECK(fcntl(descriptor, F_GETFD) == -1 && errno == EBADF);
        TbSymbolStorage_t *storage = tb_symbol_storage_create(path, &root, NULL);
        CHECK(storage);
        TbSymbolOutcome_e ready;
        do {
            ready = tb_symbol_storage_step(storage);
        } while (ready == TB_SYMBOL_PENDING);
        CHECK(ready == TB_SYMBOL_SUCCESS);
        if (result.detail & TB_SYMBOL_DETAIL_HAS_KEY)
            CHECK(tb_symbol_storage_unlink(storage, slot, &result.key));
        else {
            CHECK(!granted);
            CHECK(tb_symbol_storage_discard_empty(storage, slot));
        }
        tb_symbol_storage_destroy(storage);
    }
}


static void malformed(const char *path, TbSymbolBudget_t *budget)
{
    Arguments_t arguments = {0};
    paths(&arguments, path, NULL);
    size_t complete = arguments.length;
    for (size_t length = 0; length < complete; ++length) {
        arguments.length = length;
        TbSymbolOperation_t *state = operation(TB_SYMBOL_OPERATION_CREATE_STORAGE, &arguments, budget);
        CHECK(finish(state, 1) == TB_SYMBOL_MALFORMED);
        TbSymbolOperationResult_t result = tb_symbol_operation_result(state);
        CHECK(!result.payload && !result.length && result.descriptor < 0 && !result.detail);
        tb_symbol_operation_destroy(state);
        CHECK(!budget->used);
    }
    arguments.length = complete;
    arguments.bytes[complete - 1] = 'x';
    TbSymbolOperation_t *state = operation(TB_SYMBOL_OPERATION_CREATE_STORAGE, &arguments, budget);
    CHECK(finish(state, 1) == TB_SYMBOL_MALFORMED);
    tb_symbol_operation_destroy(state);
    arguments.bytes[complete - 1] = 0;
    arguments.bytes[4] = 0;
    state = operation(TB_SYMBOL_OPERATION_CREATE_STORAGE, &arguments, budget);
    CHECK(finish(state, 1) == TB_SYMBOL_MALFORMED);
    tb_symbol_operation_destroy(state);
    struct stat native;
    CHECK(stat(path, &native) && errno == ENOENT && !budget->used);
    CHECK(!tb_symbol_operation_create(0, NULL, 0, budget));
    fail_allocation = true;
    CHECK(!tb_symbol_operation_create(TB_SYMBOL_OPERATION_CREATE_STORAGE, arguments.bytes,
                                      arguments.length, budget));
    fail_allocation = false;
    CHECK(!budget->used);
}


int main(void)
{
    char parent[] = "/tmp/tb-symbol-operation-XXXXXX";
    CHECK(mkdtemp(parent));
    char origin[256];
    char path[256];
    CHECK(snprintf(origin, sizeof(origin), "%s/origin", parent) > 0);
    CHECK(snprintf(path, sizeof(path), "%s/pool", parent) > 0);
    int source = open(origin, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    CHECK(source >= 0 && write(source, "a\0bcd", 5) == 5 && !close(source));
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 256 * 1024));
    malformed(path, &budget);
    TbSymbolStorageKey_t root = create_namespace(path, &budget);
    TbSymbolStorageInfo_t first = acquire_file(origin, path, root, 1, &budget);
    TbSymbolStorageInfo_t second = acquire_file(origin, path, root, 2, &budget);
    compare_files(path, root, first, second, &budget);
    cancellation(origin, path, root, &budget);
    discard_empty(path, root, &budget);
    remove_file(path, root, 1, first.key, &budget);
    remove_file(path, root, 2, second.key, &budget);
    remove_namespace(path, root, &budget);
    remove_namespace(path, root, &budget);
    CHECK(!unlink(origin) && !rmdir(parent));
    return 0;
}
