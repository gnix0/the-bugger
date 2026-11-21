#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Linux clone fixture interfaces. */
#include "symbol/acquire.h"
#include "../support/check.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>

typedef enum {
    CLONE_EMULATED,
    CLONE_UNSUPPORTED,
    CLONE_PARTIAL_UNSUPPORTED,
    CLONE_SHORT,
    CLONE_GROW_SOURCE,
    CLONE_ERROR,
    CLONE_NATIVE
} CloneMode_e;

static CloneMode_e mode;
static int clone_error;
static bool interrupt_clone;
static bool reset_error;
static bool interrupt_reset;
static unsigned clone_calls;
static unsigned reset_calls;
static uint64_t granted;
static int source_writer = -1;
static uint64_t next_slot;
static TbSymbolStorage_t *storage;
static unsigned char fixture[8193];

int __real_ioctl(int fd, unsigned long request, ...);
int __wrap_ioctl(int fd, unsigned long request, ...);
int __real_ftruncate(int fd, off_t length);
int __wrap_ftruncate(int fd, off_t length);

int __wrap_ioctl(int fd, unsigned long request, ...)
{
    va_list arguments;
    va_start(arguments, request);
    struct file_clone_range *range = va_arg(arguments, struct file_clone_range *);
    va_end(arguments);
    CHECK(request == FICLONERANGE);
    CHECK(range && range->src_length && range->src_length == granted);
    CHECK(!range->src_offset && !range->dest_offset);
    CHECK((fcntl((int)range->src_fd, F_GETFL) & O_ACCMODE) == O_RDONLY);
    CHECK((fcntl(fd, F_GETFL) & O_ACCMODE) == O_RDWR);
    struct stat before;
    CHECK(!fstat(fd, &before) && !before.st_size && before.st_nlink == 1);
    ++clone_calls;
    if (interrupt_clone) {
        interrupt_clone = false;
        errno = EINTR;
        return -1;
    }
    if (mode == CLONE_NATIVE)
        return __real_ioctl(fd, request, range);
    if (mode == CLONE_ERROR || mode == CLONE_UNSUPPORTED) {
        errno = mode == CLONE_ERROR ? clone_error : clone_error ? clone_error : EOPNOTSUPP;
        return -1;
    }
    /* Emulate a kernel result to test acquisition branches, not filesystem
     * atomicity or reflink support. The native path above never emulates copying. */
    unsigned char bytes[sizeof(fixture)];
    size_t length = (size_t)range->src_length;
    CHECK(length <= sizeof(bytes));
    CHECK(pread((int)range->src_fd, bytes, length, 0) == (ssize_t)length);
    if (mode == CLONE_SHORT)
        --length;
    CHECK(pwrite(fd, bytes, length, 0) == (ssize_t)length);
    struct stat after;
    CHECK(!fstat(fd, &after) && (uint64_t)after.st_size <= granted);
    if (mode == CLONE_PARTIAL_UNSUPPORTED) {
        errno = EINVAL;
        return -1;
    }
    if (mode == CLONE_GROW_SOURCE)
        CHECK(pwrite(source_writer, "X", 1, (off_t)granted) == 1);
    return 0;
}


int __wrap_ftruncate(int fd, off_t length)
{
    if (fd != source_writer) {
        CHECK(!length);
        ++reset_calls;
        if (interrupt_reset) {
            interrupt_reset = false;
            errno = EINTR;
            return -1;
        }
        if (reset_error) {
            reset_error = false;
            errno = ENOSPC;
            return -1;
        }
    }
    return __real_ftruncate(fd, length);
}


static size_t descriptor_count(void)
{
    DIR *directory = opendir("/proc/self/fd");
    CHECK(directory);
    size_t count = 0;
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] != '.')
            ++count;
    }
    CHECK(!closedir(directory));
    return count;
}


static TbSymbolAcquire_t *prepare(const char *path, size_t length, TbSymbolBudget_t *budget)
{
    CHECK(!ftruncate(source_writer, 0));
    if (length)
        CHECK(pwrite(source_writer, fixture, length, 0) == (ssize_t)length);
    clone_calls = 0;
    reset_calls = 0;
    granted = 0;
    TbSymbolAcquire_t *acquisition = tb_symbol_acquire_create(path, storage, ++next_slot,
                                                             sizeof(fixture), budget);
    CHECK(acquisition);
    CHECK(tb_symbol_acquire_method(acquisition) == TB_SYMBOL_ACQUIRED_NONE);
    CHECK(!tb_symbol_acquire_copy_policy(acquisition, (TbSymbolCopyPolicy_e)99));
    uint64_t offered = UINT64_MAX;
    for (unsigned i = 0; i < 32 && !tb_symbol_acquire_offer(acquisition, &offered); ++i)
        CHECK(tb_symbol_acquire_step(acquisition, 64) == TB_SYMBOL_PENDING);
    CHECK(tb_symbol_acquire_offer(acquisition, &offered) && offered == length);
    CHECK(!clone_calls);
    CHECK(!tb_symbol_acquire_copy_policy(acquisition, TB_SYMBOL_COPY_STREAM_ONLY));
    CHECK(!tb_symbol_acquire_grant(acquisition, length + 1));
    for (unsigned i = 0; i < 4; ++i)
        CHECK(tb_symbol_acquire_step(acquisition, 64) == TB_SYMBOL_PENDING);
    CHECK(!clone_calls && !reset_calls);
    TbSymbolStorageInfo_t info;
    CHECK(tb_symbol_storage_info(storage, next_slot, &info) && !info.bytes);
    granted = offered;
    CHECK(tb_symbol_acquire_grant(acquisition, offered));
    CHECK(!tb_symbol_acquire_grant(acquisition, offered));
    return acquisition;
}


static TbSymbolOutcome_e finish(TbSymbolAcquire_t *acquisition)
{
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 1000 && outcome == TB_SYMBOL_PENDING; ++i)
        outcome = tb_symbol_acquire_step(acquisition, 64);
    CHECK(outcome != TB_SYMBOL_PENDING);
    return outcome;
}


static void cleanup(TbSymbolAcquire_t *acquisition)
{
    TbSymbolStorageKey_t key;
    CHECK(tb_symbol_acquire_storage_key(acquisition, &key));
    tb_symbol_acquire_destroy(acquisition);
    TbSymbolStorageInfo_t info;
    CHECK(tb_symbol_storage_info(storage, next_slot, &info));
    CHECK(info.bytes <= granted);
    CHECK(tb_symbol_storage_unlink(storage, next_slot, &key));
}


static void check_bytes(TbSymbolAcquire_t *acquisition, size_t length)
{
    TbSymbolAcquiredBytes_t result;
    CHECK(tb_symbol_acquire_take(acquisition, &result));
    CHECK(result.bytes == length);
    CHECK((fcntl(result.descriptor, F_GETFL) & O_ACCMODE) == O_RDONLY);
    CHECK(fcntl(result.descriptor, F_GETFD) & FD_CLOEXEC);
    unsigned char actual[sizeof(fixture)];
    CHECK(pread(result.descriptor, actual, sizeof(actual), 0) == (ssize_t)length);
    CHECK(!memcmp(actual, fixture, length));
    TbSha256_t hash;
    unsigned char digest[TB_SHA256_DIGEST_BYTES];
    CHECK(tb_sha256_init(&hash) && tb_sha256_update(&hash, fixture, length));
    CHECK(tb_sha256_final(&hash, length, digest));
    CHECK(!memcmp(result.digest, digest, sizeof(digest)));
    /* Origin writes must not change either a real clone or streaming backing. */
    if (length) {
        CHECK(pwrite(source_writer, "X", 1, 0) == 1);
        CHECK(pread(result.descriptor, actual, length, 0) == (ssize_t)length);
        CHECK(!memcmp(actual, fixture, length));
    }
    CHECK(!close(result.descriptor));
}


static void successes(const char *path, TbSymbolBudget_t *budget)
{
    CloneMode_e modes[] = {CLONE_EMULATED, CLONE_UNSUPPORTED, CLONE_PARTIAL_UNSUPPORTED};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        mode = modes[i];
        TbSymbolAcquire_t *acquisition = prepare(path, sizeof(fixture), budget);
        CHECK(finish(acquisition) == TB_SYMBOL_SUCCESS);
        CHECK(clone_calls == 1 && reset_calls == (mode != CLONE_EMULATED));
        CHECK(tb_symbol_acquire_method(acquisition) == (mode == CLONE_EMULATED
                  ? TB_SYMBOL_ACQUIRED_REFLINK : TB_SYMBOL_ACQUIRED_STREAM));
        check_bytes(acquisition, sizeof(fixture));
        cleanup(acquisition);
        CHECK(!budget->used);
    }
    int unsupported[] = {ENOTTY, ENOSYS, EXDEV, EINVAL, ETXTBSY};
    for (size_t i = 0; i < sizeof(unsupported) / sizeof(unsupported[0]); ++i) {
        mode = CLONE_UNSUPPORTED;
        clone_error = unsupported[i];
        TbSymbolAcquire_t *fallback = prepare(path, 100, budget);
        CHECK(finish(fallback) == TB_SYMBOL_SUCCESS && reset_calls == 1);
        CHECK(tb_symbol_acquire_method(fallback) == TB_SYMBOL_ACQUIRED_STREAM);
        check_bytes(fallback, 100);
        cleanup(fallback);
        CHECK(!budget->used);
    }
    clone_error = 0;
    mode = CLONE_EMULATED;
    TbSymbolAcquire_t *acquisition = prepare(path, 0, budget);
    CHECK(finish(acquisition) == TB_SYMBOL_SUCCESS);
    CHECK(!clone_calls && !reset_calls);
    CHECK(tb_symbol_acquire_method(acquisition) == TB_SYMBOL_ACQUIRED_STREAM);
    check_bytes(acquisition, 0);
    cleanup(acquisition);
    acquisition = prepare(path, 100, budget);
    interrupt_clone = true;
    uint64_t progress = tb_symbol_acquire_progress(acquisition);
    CHECK(tb_symbol_acquire_step(acquisition, 64) == TB_SYMBOL_PENDING);
    CHECK(tb_symbol_acquire_progress(acquisition) == progress);
    CHECK(finish(acquisition) == TB_SYMBOL_SUCCESS && clone_calls == 2);
    check_bytes(acquisition, 100);
    cleanup(acquisition);
    mode = CLONE_UNSUPPORTED;
    acquisition = prepare(path, 100, budget);
    CHECK(tb_symbol_acquire_step(acquisition, 64) == TB_SYMBOL_PENDING && clone_calls == 1);
    interrupt_reset = true;
    progress = tb_symbol_acquire_progress(acquisition);
    CHECK(tb_symbol_acquire_step(acquisition, 64) == TB_SYMBOL_PENDING);
    CHECK(tb_symbol_acquire_progress(acquisition) == progress);
    CHECK(finish(acquisition) == TB_SYMBOL_SUCCESS && reset_calls == 2);
    check_bytes(acquisition, 100);
    cleanup(acquisition);
    CHECK(!budget->used);
}


static void failures(const char *path, TbSymbolBudget_t *budget)
{
    int errors[] = {ENOSPC, ENOMEM, EIO, EBADF};
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        mode = CLONE_ERROR;
        clone_error = errors[i];
        TbSymbolAcquire_t *acquisition = prepare(path, 100, budget);
        CHECK(finish(acquisition) == ((clone_error == EIO || clone_error == EBADF)
                  ? TB_SYMBOL_OPERATIONAL_FAILURE : TB_SYMBOL_RESOURCE_FAILURE));
        int error;
        CHECK(tb_symbol_acquire_failure(acquisition, &error) == TB_SYMBOL_ACQUIRE_STORAGE);
        CHECK(error == clone_error && clone_calls == 1 && !reset_calls);
        CHECK(tb_symbol_acquire_method(acquisition) == TB_SYMBOL_ACQUIRED_NONE);
        TbSymbolAcquiredBytes_t result;
        memset(&result, 0xa5, sizeof(result));
        TbSymbolAcquiredBytes_t original = result;
        CHECK(!tb_symbol_acquire_take(acquisition, &result));
        CHECK(!memcmp(&result, &original, sizeof(result)));
        cleanup(acquisition);
        CHECK(!budget->used);
    }
    clone_error = 0;
    CloneMode_e modes[] = {CLONE_SHORT, CLONE_GROW_SOURCE, CLONE_UNSUPPORTED};
    for (size_t i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) {
        mode = modes[i];
        reset_error = mode == CLONE_UNSUPPORTED;
        TbSymbolAcquire_t *acquisition = prepare(path, 100, budget);
        CHECK(finish(acquisition) == (mode == CLONE_UNSUPPORTED
                  ? TB_SYMBOL_RESOURCE_FAILURE : TB_SYMBOL_OPERATIONAL_FAILURE));
        CHECK(tb_symbol_acquire_failure(acquisition, NULL) == (mode == CLONE_SHORT
                  ? TB_SYMBOL_ACQUIRE_INTEGRITY : mode == CLONE_GROW_SOURCE
                    ? TB_SYMBOL_ACQUIRE_CHANGED : TB_SYMBOL_ACQUIRE_STORAGE));
        CHECK(tb_symbol_acquire_method(acquisition) == TB_SYMBOL_ACQUIRED_NONE);
        cleanup(acquisition);
        CHECK(!budget->used);
    }
}


static void cancellation(const char *path, TbSymbolBudget_t *budget)
{
    for (unsigned stage = 0; stage < 24; ++stage) {
        mode = stage % 2 ? CLONE_EMULATED : CLONE_PARTIAL_UNSUPPORTED;
        TbSymbolAcquire_t *acquisition = prepare(path, 100, budget);
        for (unsigned i = 0; i < stage; ++i) {
            TbSymbolOutcome_e outcome = tb_symbol_acquire_step(acquisition, 64);
            CHECK(outcome == TB_SYMBOL_PENDING || outcome == TB_SYMBOL_SUCCESS);
        }
        cleanup(acquisition);
        CHECK(!budget->used);
    }
}


int main(void)
{
    char directory[] = "/tmp/tb-symbol-reflink-XXXXXX";
    CHECK(mkdtemp(directory));
    char path[256];
    char pool[256];
    CHECK(snprintf(path, sizeof(path), "%s/origin", directory) > 0);
    CHECK(snprintf(pool, sizeof(pool), "%s/pool", directory) > 0);
    source_writer = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    CHECK(source_writer >= 0);
    storage = tb_symbol_storage_create(pool, NULL, NULL);
    CHECK(storage);
    TbSymbolOutcome_e setup = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 16 && setup == TB_SYMBOL_PENDING; ++i)
        setup = tb_symbol_storage_step(storage);
    CHECK(setup == TB_SYMBOL_SUCCESS);
    for (size_t i = 0; i < sizeof(fixture); ++i)
        fixture[i] = (unsigned char)(i * 37);
    size_t baseline = descriptor_count();
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 128 * 1024));
    successes(path, &budget);
    failures(path, &budget);
    cancellation(path, &budget);
    mode = CLONE_NATIVE;
    TbSymbolAcquire_t *acquisition = prepare(path, sizeof(fixture), &budget);
    CHECK(finish(acquisition) == TB_SYMBOL_SUCCESS && clone_calls == 1);
    check_bytes(acquisition, sizeof(fixture));
    if (tb_symbol_acquire_method(acquisition) == TB_SYMBOL_ACQUIRED_REFLINK) {
        CHECK(!reset_calls);
        puts("Native fixed-extent reflink: retained bytes and source-write isolation passed");
    } else {
        CHECK(reset_calls == 1);
        puts("Native fixed-extent reflink: unavailable; streaming fallback passed");
    }
    cleanup(acquisition);
    CHECK(!budget.used && descriptor_count() == baseline);
    CHECK(tb_symbol_storage_remove(storage));
    tb_symbol_storage_destroy(storage);
    CHECK(!close(source_writer));
    CHECK(!unlink(path) && !rmdir(directory));
    return 0;
}
