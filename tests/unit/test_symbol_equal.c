#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Read-only anonymous-file fixtures. */
#include "symbol/equal.h"
#include "../support/check.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool fail_allocation;
static bool short_reads;
static int read_error;
static int failed_fd = -1;
static uint64_t read_bytes;
static size_t inspected;
static uint64_t compared_bytes;
static int mutate_after_fd = -1;
static int mutable_fd = -1;

void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size);
ssize_t __real_pread(int fd, void *bytes, size_t length, off_t offset);
ssize_t __wrap_pread(int fd, void *bytes, size_t length, off_t offset);
int __real_memcmp(const void *left, const void *right, size_t length);
int __wrap_memcmp(const void *left, const void *right, size_t length);
int __wrap_bcmp(const void *left, const void *right, size_t length);

void *__wrap_calloc(size_t count, size_t size)
{
    return fail_allocation ? NULL : __real_calloc(count, size);
}


ssize_t __wrap_pread(int fd, void *bytes, size_t length, off_t offset)
{
    inspected += length;
    if (fd == failed_fd && read_error) {
        errno = read_error;
        read_error = 0;
        return -1;
    }
    if (short_reads && length > 1)
        length = 1;
    ssize_t result = __real_pread(fd, bytes, length, offset);
    if (result > 0) {
        read_bytes += (uint64_t)result;
        if (fd == mutate_after_fd) {
            CHECK(!ftruncate(mutable_fd, 0));
            mutate_after_fd = -1;
        }
    }
    return result;
}


int __wrap_memcmp(const void *left, const void *right, size_t length)
{
    inspected += length * 2;
    compared_bytes += length * 2;
    return __real_memcmp(left, right, length);
}


int __wrap_bcmp(const void *left, const void *right, size_t length)
{
    return __wrap_memcmp(left, right, length);
}


static int backing(size_t bytes, size_t difference)
{
    int writer = open("/tmp", O_TMPFILE | O_EXCL | O_RDWR | O_CLOEXEC, 0600);
    CHECK(writer >= 0);
    unsigned char buffer[4096];
    for (size_t offset = 0; offset < bytes;) {
        size_t count = bytes - offset;
        if (count > sizeof(buffer))
            count = sizeof(buffer);
        for (size_t i = 0; i < count; ++i) {
            buffer[i] = (unsigned char)((offset + i) * 37 + 17);
            if (offset + i == difference)
                buffer[i] ^= 0x80;
        }
        CHECK(pwrite(writer, buffer, count, (off_t)offset) == (ssize_t)count);
        offset += count;
    }
    char path[64];
    CHECK(snprintf(path, sizeof(path), "/proc/self/fd/%d", writer) > 0);
    int reader = open(path, O_RDONLY | O_CLOEXEC);
    CHECK(reader >= 0 && !close(writer));
    return reader;
}


static TbSymbolOutcome_e finish(TbSymbolEquality_t *comparison, size_t first_limit)
{
    uint64_t previous = tb_symbol_equal_progress(comparison);
    for (unsigned i = 0; i < 1000000; ++i) {
        /* A large captured chunk must remain reproducible when later slices
         * shrink, including while either input has only partially arrived. */
        size_t limit = i % 5 == 0 ? first_limit : 2;
        inspected = 0;
        TbSymbolOutcome_e result = tb_symbol_equal_step(comparison, limit);
        CHECK(inspected <= limit);
        uint64_t progress = tb_symbol_equal_progress(comparison);
        CHECK(progress >= previous);
        previous = progress;
        if (result != TB_SYMBOL_PENDING)
            return result;
    }
    CHECK(false);
    return TB_SYMBOL_PENDING;
}


static void pair(size_t bytes, size_t difference, size_t limit)
{
    int left = backing(bytes, SIZE_MAX);
    int right = backing(bytes, difference);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 128 * 1024));
    TbSymbolEquality_t *comparison = tb_symbol_equal_create(left, right, bytes, &budget);
    CHECK(comparison && budget.used);
    bool equal = false;
    CHECK(!tb_symbol_equal_result(comparison, &equal));
    read_bytes = 0;
    compared_bytes = 0;
    CHECK(finish(comparison, limit) == TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_equal_result(comparison, &equal));
    CHECK(equal == (difference >= bytes));
    if (equal) {
        CHECK(read_bytes == bytes * 2);
        CHECK(compared_bytes == bytes * 2);
    }
    tb_symbol_equal_destroy(comparison);
    CHECK(!budget.used);
    CHECK(fcntl(left, F_GETFD) >= 0 && fcntl(right, F_GETFD) >= 0);
    CHECK(!close(left) && !close(right));
}


static void failures(void)
{
    int left = backing(73, SIZE_MAX);
    int right = backing(73, SIZE_MAX);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 128 * 1024));
    fail_allocation = true;
    CHECK(!tb_symbol_equal_create(left, right, 73, &budget));
    CHECK(!budget.used);
    fail_allocation = false;
    budget.limit = 1;
    CHECK(!tb_symbol_equal_create(left, right, 73, &budget));
    CHECK(!budget.used);
    budget.limit = 128 * 1024;
    for (unsigned input = 0; input < 2; ++input) {
        TbSymbolEquality_t *comparison = tb_symbol_equal_create(left, right, 73, &budget);
        CHECK(comparison);
        failed_fd = input ? right : left;
        read_error = EIO;
        CHECK(finish(comparison, 64) == TB_SYMBOL_OPERATIONAL_FAILURE);
        TbSymbolEqualityFailure_t failure = tb_symbol_equal_failure(comparison);
        CHECK(failure.input_mask == (1U << input) && failure.system_error == EIO);
        bool equal = true;
        CHECK(!tb_symbol_equal_result(comparison, &equal) && equal);
        tb_symbol_equal_destroy(comparison);
        CHECK(!budget.used);
    }
    failed_fd = left;
    read_error = EINTR;
    short_reads = true;
    TbSymbolEquality_t *comparison = tb_symbol_equal_create(left, right, 73, &budget);
    CHECK(comparison && finish(comparison, 64) == TB_SYMBOL_SUCCESS);
    bool equal = false;
    CHECK(tb_symbol_equal_result(comparison, &equal) && equal);
    tb_symbol_equal_destroy(comparison);
    short_reads = false;
    comparison = tb_symbol_equal_create(left, right, 72, &budget);
    CHECK(comparison && finish(comparison, 64) == TB_SYMBOL_MALFORMED);
    CHECK(tb_symbol_equal_failure(comparison).input_mask == 1);
    tb_symbol_equal_destroy(comparison);
    comparison = tb_symbol_equal_create(left, right, 73, &budget);
    CHECK(comparison);
    CHECK(tb_symbol_equal_step(comparison, 1) == TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(!tb_symbol_equal_result(comparison, &equal));
    tb_symbol_equal_destroy(comparison);
    CHECK(!budget.used);
    CHECK(!close(left) && !close(right));
}


static void input_contracts(void)
{
    int left = backing(0, SIZE_MAX);
    int right = backing(1, SIZE_MAX);
    TbSymbolEquality_t *comparison = tb_symbol_equal_create(left, right, 0, NULL);
    CHECK(comparison && finish(comparison, 64) == TB_SYMBOL_MALFORMED);
    CHECK(tb_symbol_equal_failure(comparison).input_mask == 2);
    tb_symbol_equal_destroy(comparison);
    char path[64];
    CHECK(snprintf(path, sizeof(path), "/proc/self/fd/%d", right) > 0);
    int path_fd = open(path, O_PATH | O_CLOEXEC);
    CHECK(path_fd >= 0);
    comparison = tb_symbol_equal_create(path_fd, right, 1, NULL);
    CHECK(comparison && finish(comparison, 64) == TB_SYMBOL_UNSUPPORTED);
    CHECK(tb_symbol_equal_failure(comparison).input_mask == 1);
    tb_symbol_equal_destroy(comparison);
    int writable = open(path, O_RDWR | O_CLOEXEC);
    CHECK(writable >= 0);
    comparison = tb_symbol_equal_create(right, writable, 1, NULL);
    CHECK(comparison && finish(comparison, 64) == TB_SYMBOL_UNSUPPORTED);
    CHECK(tb_symbol_equal_failure(comparison).input_mask == 2);
    tb_symbol_equal_destroy(comparison);
    CHECK(!close(writable) && !close(path_fd) && !close(left) && !close(right));
}


static void changed_backing(void)
{
    int left = backing(73, SIZE_MAX);
    int right = backing(73, SIZE_MAX);
    char path[64];
    CHECK(snprintf(path, sizeof(path), "/proc/self/fd/%d", left) > 0);
    mutable_fd = open(path, O_WRONLY | O_CLOEXEC);
    CHECK(mutable_fd >= 0);
    TbSymbolEquality_t *comparison = tb_symbol_equal_create(left, right, 73, NULL);
    CHECK(comparison);
    mutate_after_fd = right;
    read_bytes = 0;
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 32 && outcome == TB_SYMBOL_PENDING; ++i) {
        inspected = 0;
        outcome = tb_symbol_equal_step(comparison, 65536);
        CHECK(inspected <= 65536);
    }
    CHECK(outcome == TB_SYMBOL_MALFORMED && read_bytes == 146);
    CHECK(tb_symbol_equal_failure(comparison).input_mask == 1);
    bool equal = true;
    CHECK(!tb_symbol_equal_result(comparison, &equal));
    tb_symbol_equal_destroy(comparison);
    CHECK(!close(mutable_fd) && !close(left) && !close(right));
    mutable_fd = -1;
}


static void cancellation(void)
{
    int reader = backing(5, SIZE_MAX);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 128 * 1024));
    bool reached_success = false;
    for (unsigned cancel_at = 0; cancel_at < 64; ++cancel_at) {
        TbSymbolEquality_t *comparison = tb_symbol_equal_create(reader, reader, 5, &budget);
        CHECK(comparison);
        for (unsigned i = 0; i < cancel_at; ++i) {
            if (tb_symbol_equal_step(comparison, 4) == TB_SYMBOL_SUCCESS) {
                reached_success = true;
                break;
            }
        }
        tb_symbol_equal_destroy(comparison);
        CHECK(!budget.used && fcntl(reader, F_GETFD) >= 0);
        if (reached_success)
            break;
    }
    CHECK(reached_success && !close(reader));
}


int main(void)
{
    pair(0, SIZE_MAX, 2);
    pair(1025, SIZE_MAX, 64);
    pair(65537, SIZE_MAX, 65536);
    pair(1025, 0, 64);
    pair(1025, 1024, 64);
    failures();
    input_contracts();
    changed_backing();
    cancellation();
    return 0;
}
