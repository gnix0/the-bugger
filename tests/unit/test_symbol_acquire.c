#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Private Linux file fixtures. */
#include "symbol/acquire.h"
#include "../support/check.h"

#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

static bool fail_allocation;
static int read_error;
static int write_error;
static int hash_error;
static bool short_io;
static bool close_error;
static bool mutate_source;
static bool saw_frozen_read;
static int fixture_fd = -1;
static size_t inspected;
static size_t step_limit;
static TbSymbolStorage_t *storage;
static uint64_t next_slot;
static bool unsupported_storage;
static int recycled_fd = -1;
static uint64_t storage_max = UINT64_MAX;

void *__real_calloc(size_t count, size_t size);
void *__wrap_calloc(size_t count, size_t size);
ssize_t __real_pread(int fd, void *bytes, size_t length, off_t offset);
ssize_t __wrap_pread(int fd, void *bytes, size_t length, off_t offset);
ssize_t __real_pwrite(int fd, const void *bytes, size_t length, off_t offset);
ssize_t __wrap_pwrite(int fd, const void *bytes, size_t length, off_t offset);
int __real_close(int fd);
int __wrap_close(int fd);
int __real_fstatfs(int fd, struct statfs *filesystem);
int __wrap_fstatfs(int fd, struct statfs *filesystem);

void *__wrap_calloc(size_t count, size_t size)
{
    return fail_allocation ? NULL : __real_calloc(count, size);
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


static void no_writer(int fd)
{
    struct stat frozen;
    CHECK(!fstat(fd, &frozen));
    struct stat original;
    CHECK(!fstat(fixture_fd, &original));
    if (frozen.st_dev == original.st_dev && frozen.st_ino == original.st_ino)
        return;
    saw_frozen_read = true;
    DIR *directory = opendir("/proc/self/fd");
    CHECK(directory);
    struct dirent *entry;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] == '.')
            continue;
        char *end;
        long number = strtol(entry->d_name, &end, 10);
        CHECK(!*end && number >= 0 && number <= INT32_MAX);
        struct stat candidate;
        CHECK(!fstat((int)number, &candidate));
        if (candidate.st_ino == frozen.st_ino && candidate.st_dev == frozen.st_dev)
            CHECK((fcntl((int)number, F_GETFL) & O_ACCMODE) == O_RDONLY);
    }
    CHECK(!closedir(directory));
}


ssize_t __wrap_pread(int fd, void *bytes, size_t length, off_t offset)
{
    CHECK(length && length <= step_limit);
    inspected += length;
    no_writer(fd);
    struct stat file;
    CHECK(!fstat(fd, &file));
    struct stat original;
    CHECK(!fstat(fixture_fd, &original));
    if ((file.st_dev != original.st_dev || file.st_ino != original.st_ino) && hash_error) {
        read_error = hash_error;
        hash_error = 0;
    }
    if (read_error) {
        errno = read_error;
        read_error = 0;
        return -1;
    }
    if (short_io && length > 1)
        length = 1;
    ssize_t count = __real_pread(fd, bytes, length, offset);
    if (mutate_source) {
        struct stat info;
        CHECK(!fstat(fixture_fd, &info));
        CHECK(__real_pwrite(fixture_fd, "X", 1, info.st_size) == 1);
        mutate_source = false;
    }
    return count;
}


ssize_t __wrap_pwrite(int fd, const void *bytes, size_t length, off_t offset)
{
    CHECK(length && length <= step_limit);
    inspected += length;
    CHECK(offset >= 0 && (uint64_t)offset <= storage_max && length <= storage_max - (uint64_t)offset);
    if (write_error) {
        errno = write_error;
        write_error = 0;
        return -1;
    }
    if (short_io && length > 1)
        length = 1;
    return __real_pwrite(fd, bytes, length, offset);
}


int __wrap_close(int fd)
{
    int result = __real_close(fd);
    if (close_error) {
        CHECK(!result);
        close_error = false;
        recycled_fd = open("/dev/null", O_RDONLY | O_CLOEXEC);
        CHECK(recycled_fd == fd);
        errno = EIO;
        return -1;
    }
    return result;
}


int __wrap_fstatfs(int fd, struct statfs *filesystem)
{
    int result = __real_fstatfs(fd, filesystem);
    if (!result && unsupported_storage)
        filesystem->f_type = 0;
    return result;
}


static TbSymbolOutcome_e step(TbSymbolAcquire_t *acquisition, size_t limit)
{
    step_limit = limit;
    inspected = 0;
    TbSymbolOutcome_e result = tb_symbol_acquire_step(acquisition, limit);
    CHECK(inspected <= limit);
    return result;
}


static void offer(TbSymbolAcquire_t *acquisition, uint64_t expected)
{
    uint64_t bytes = UINT64_MAX;
    for (unsigned i = 0; i < 32; ++i) {
        if (tb_symbol_acquire_offer(acquisition, &bytes)) {
            CHECK(bytes == expected);
            TbSymbolStorageKey_t key;
            TbSymbolStorageInfo_t staging;
            CHECK(tb_symbol_acquire_storage_key(acquisition, &key));
            CHECK(tb_symbol_storage_info(storage, next_slot, &staging) && !staging.bytes);
            CHECK(key.device == staging.key.device && key.inode == staging.key.inode);
            return;
        }
        CHECK(step(acquisition, 64) == TB_SYMBOL_PENDING);
    }
    CHECK(false);
}


static TbSymbolOutcome_e finish(TbSymbolAcquire_t *acquisition, size_t limit)
{
    for (unsigned i = 0; i < 1000000; ++i) {
        TbSymbolOutcome_e result = step(acquisition, limit);
        if (result != TB_SYMBOL_PENDING)
            return result;
    }
    CHECK(false);
    return TB_SYMBOL_PENDING;
}


static void write_fixture(size_t length)
{
    CHECK(!ftruncate(fixture_fd, 0));
    unsigned char bytes[4096];
    for (size_t offset = 0; offset < length;) {
        size_t count = length - offset;
        if (count > sizeof(bytes))
            count = sizeof(bytes);
        for (size_t i = 0; i < count; ++i)
            bytes[i] = (unsigned char)((offset + i) * 71);
        CHECK(__real_pwrite(fixture_fd, bytes, count, (off_t)offset) == (ssize_t)count);
        offset += count;
    }
}


static void retained_bytes(const char *path, size_t length, size_t limit)
{
    write_fixture(length);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 128 * 1024));
    TbSymbolAcquire_t *acquisition = tb_symbol_acquire_create(path, storage, ++next_slot,
                                                             256 * 1024, &budget);
    CHECK(acquisition && budget.used);
    TbSymbolAcquiredBytes_t result = {
        .bytes      = UINT64_MAX,
        .descriptor = -99
    };
    CHECK(!tb_symbol_acquire_take(acquisition, &result));
    CHECK(result.bytes == UINT64_MAX && result.descriptor == -99);
    CHECK(!tb_symbol_acquire_grant(acquisition, length));
    offer(acquisition, length);
    CHECK(!tb_symbol_acquire_grant(acquisition, length + 1));
    uint64_t progress = tb_symbol_acquire_progress(acquisition);
    for (unsigned i = 0; i < 10; ++i)
        CHECK(step(acquisition, limit) == TB_SYMBOL_PENDING);
    CHECK(tb_symbol_acquire_progress(acquisition) == progress);
    CHECK(tb_symbol_acquire_grant(acquisition, length));
    CHECK(!tb_symbol_acquire_grant(acquisition, length));
    saw_frozen_read = false;
    CHECK(finish(acquisition, limit) == TB_SYMBOL_SUCCESS);
    CHECK(!length || saw_frozen_read);
    CHECK(tb_symbol_acquire_take(acquisition, &result));
    CHECK(!tb_symbol_acquire_take(acquisition, &result));
    CHECK(result.bytes == length && result.descriptor >= 0);
    CHECK((fcntl(result.descriptor, F_GETFL) & O_ACCMODE) == O_RDONLY);
    CHECK(fcntl(result.descriptor, F_GETFD) & FD_CLOEXEC);
    errno = 0;
    CHECK(write(result.descriptor, "X", 1) == -1 && errno == EBADF);
    struct stat info;
    CHECK(!fstat(result.descriptor, &info));
    CHECK(info.st_nlink == 1 && (info.st_mode & 0777) == 0400);
    CHECK(result.key.device == (uint64_t)info.st_dev && result.key.inode == (uint64_t)info.st_ino);
    CHECK(__real_pwrite(fixture_fd, "Y", 1, 0) == 1);
    TbSha256_t hash;
    CHECK(tb_sha256_init(&hash));
    unsigned char bytes[4096];
    for (size_t offset = 0; offset < length;) {
        size_t count = length - offset;
        if (count > sizeof(bytes))
            count = sizeof(bytes);
        CHECK(__real_pread(result.descriptor, bytes, count, (off_t)offset) == (ssize_t)count);
        for (size_t i = 0; i < count; ++i)
            CHECK(bytes[i] == (unsigned char)((offset + i) * 71));
        CHECK(tb_sha256_update(&hash, bytes, count));
        offset += count;
    }
    unsigned char digest[TB_SHA256_DIGEST_BYTES];
    CHECK(tb_sha256_final(&hash, length, digest));
    CHECK(!memcmp(digest, result.digest, sizeof(digest)));
    tb_symbol_acquire_destroy(acquisition);
    CHECK(!budget.used);
    CHECK(!close(result.descriptor));
}


static void failed(TbSymbolAcquire_t *acquisition, TbSymbolOutcome_e outcome,
                    TbSymbolAcquireFailure_e failure)
{
    CHECK(finish(acquisition, 64) == outcome);
    CHECK(tb_symbol_acquire_failure(acquisition, NULL) == failure);
    TbSymbolAcquiredBytes_t result;
    memset(&result, 0x5a, sizeof(result));
    TbSymbolAcquiredBytes_t original = result;
    CHECK(!tb_symbol_acquire_take(acquisition, &result));
    CHECK(!memcmp(&result, &original, sizeof(result)));
    tb_symbol_acquire_destroy(acquisition);
}


static void faults(const char *path, const char *directory)
{
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 128 * 1024));
    write_fixture(100);
    fail_allocation = true;
    CHECK(!tb_symbol_acquire_create(path, storage, ++next_slot, 100, &budget));
    fail_allocation = false;
    CHECK(!budget.used);
    budget.limit = 1;
    CHECK(!tb_symbol_acquire_create(path, storage, ++next_slot, 100, &budget));
    CHECK(!budget.used);
    budget.limit = 128 * 1024;
    TbSymbolAcquire_t *acquisition = tb_symbol_acquire_create(path, storage, ++next_slot, 99, &budget);
    CHECK(acquisition);
    failed(acquisition, TB_SYMBOL_RESOURCE_FAILURE, TB_SYMBOL_ACQUIRE_FILE_LIMIT);
    CHECK(!budget.used);
    TbSymbolStorage_t *unready = tb_symbol_storage_create("/does-not-exist/tb-symbol-storage", NULL, NULL);
    CHECK(unready);
    acquisition = tb_symbol_acquire_create(path, unready, ++next_slot, 100, &budget);
    CHECK(acquisition);
    failed(acquisition, TB_SYMBOL_OPERATIONAL_FAILURE, TB_SYMBOL_ACQUIRE_STORAGE);
    tb_symbol_storage_destroy(unready);
    CHECK(!budget.used);
    acquisition = tb_symbol_acquire_create(path, storage, ++next_slot, 100, &budget);
    CHECK(acquisition);
    offer(acquisition, 100);
    CHECK(tb_symbol_acquire_grant(acquisition, 100));
    mutate_source = true;
    failed(acquisition, TB_SYMBOL_OPERATIONAL_FAILURE, TB_SYMBOL_ACQUIRE_CHANGED);
    CHECK(!budget.used);
    write_fixture(100);
    acquisition = tb_symbol_acquire_create(path, storage, ++next_slot, 100, &budget);
    CHECK(acquisition);
    offer(acquisition, 100);
    CHECK(tb_symbol_acquire_grant(acquisition, 100));
    read_error = EIO;
    failed(acquisition, TB_SYMBOL_OPERATIONAL_FAILURE, TB_SYMBOL_ACQUIRE_IO);
    CHECK(!budget.used);
    acquisition = tb_symbol_acquire_create(path, storage, ++next_slot, 100, &budget);
    CHECK(acquisition);
    offer(acquisition, 100);
    CHECK(tb_symbol_acquire_grant(acquisition, 100));
    write_error = ENOSPC;
    failed(acquisition, TB_SYMBOL_RESOURCE_FAILURE, TB_SYMBOL_ACQUIRE_STORAGE);
    CHECK(!budget.used);
    acquisition = tb_symbol_acquire_create(path, storage, ++next_slot, 100, &budget);
    CHECK(acquisition);
    offer(acquisition, 100);
    CHECK(tb_symbol_acquire_grant(acquisition, 100));
    read_error = EINTR;
    write_error = EINTR;
    CHECK(finish(acquisition, 64) == TB_SYMBOL_SUCCESS);
    tb_symbol_acquire_destroy(acquisition);
    CHECK(!budget.used);
    acquisition = tb_symbol_acquire_create(path, storage, ++next_slot, 100, &budget);
    CHECK(acquisition);
    offer(acquisition, 100);
    CHECK(tb_symbol_acquire_grant(acquisition, 100));
    close_error = true;
    failed(acquisition, TB_SYMBOL_OPERATIONAL_FAILURE, TB_SYMBOL_ACQUIRE_IO);
    CHECK(!close_error && !budget.used);
    CHECK(recycled_fd >= 0 && fcntl(recycled_fd, F_GETFD) >= 0);
    CHECK(!close(recycled_fd));
    recycled_fd = -1;
    char unsupported[256];
    CHECK(snprintf(unsupported, sizeof(unsupported), "%s/unsupported", directory) > 0);
    TbSymbolStorage_t *unqualified = tb_symbol_storage_create(unsupported, NULL, &budget);
    CHECK(unqualified);
    unsupported_storage = true;
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 16 && outcome == TB_SYMBOL_PENDING; ++i)
        outcome = tb_symbol_storage_step(unqualified);
    CHECK(outcome == TB_SYMBOL_UNSUPPORTED);
    unsupported_storage = false;
    CHECK(tb_symbol_storage_remove(unqualified));
    tb_symbol_storage_destroy(unqualified);
    CHECK(!budget.used);
}


static void rejected_changes(const char *path, const char *directory)
{
    write_fixture(100);
    TbSymbolAcquire_t *acquisition = tb_symbol_acquire_create(path, storage, ++next_slot, 100, NULL);
    CHECK(acquisition);
    offer(acquisition, 100);
    CHECK(!ftruncate(fixture_fd, 131073));
    storage_max = 100;
    CHECK(tb_symbol_acquire_grant(acquisition, 100));
    failed(acquisition, TB_SYMBOL_OPERATIONAL_FAILURE, TB_SYMBOL_ACQUIRE_CHANGED);
    storage_max = UINT64_MAX;

    write_fixture(100);
    acquisition = tb_symbol_acquire_create(path, storage, ++next_slot, 100, NULL);
    CHECK(acquisition);
    offer(acquisition, 100);
    CHECK(!ftruncate(fixture_fd, 0));
    CHECK(tb_symbol_acquire_grant(acquisition, 100));
    failed(acquisition, TB_SYMBOL_OPERATIONAL_FAILURE, TB_SYMBOL_ACQUIRE_CHANGED);

    write_fixture(100);
    acquisition = tb_symbol_acquire_create(path, storage, ++next_slot, 100, NULL);
    CHECK(acquisition);
    offer(acquisition, 100);
    char moved[256];
    CHECK(snprintf(moved, sizeof(moved), "%s/moved", directory) > 0);
    CHECK(!rename(path, moved));
    int replacement = open(path, O_CREAT | O_EXCL | O_WRONLY | O_CLOEXEC, 0600);
    CHECK(replacement >= 0 && !close(replacement));
    CHECK(tb_symbol_acquire_grant(acquisition, 100));
    failed(acquisition, TB_SYMBOL_OPERATIONAL_FAILURE, TB_SYMBOL_ACQUIRE_CHANGED);
    CHECK(!unlink(path));
    CHECK(!rename(moved, path));

    write_fixture(100);
    acquisition = tb_symbol_acquire_create(path, storage, ++next_slot, 100, NULL);
    CHECK(acquisition);
    offer(acquisition, 100);
    CHECK(tb_symbol_acquire_grant(acquisition, 100));
    hash_error = EIO;
    failed(acquisition, TB_SYMBOL_OPERATIONAL_FAILURE, TB_SYMBOL_ACQUIRE_IO);
    CHECK(!hash_error);

    acquisition = tb_symbol_acquire_create(path, storage, ++next_slot, 100, NULL);
    CHECK(acquisition);
    CHECK(tb_symbol_acquire_step(acquisition, 0) == TB_SYMBOL_OPERATIONAL_FAILURE);
    failed(acquisition, TB_SYMBOL_OPERATIONAL_FAILURE, TB_SYMBOL_ACQUIRE_IO);

    char fifo[256];
    CHECK(snprintf(fifo, sizeof(fifo), "%s/fifo", directory) > 0);
    CHECK(!mkfifo(fifo, 0600));
    acquisition = tb_symbol_acquire_create(fifo, storage, ++next_slot, 100, NULL);
    CHECK(acquisition);
    failed(acquisition, TB_SYMBOL_UNSUPPORTED, TB_SYMBOL_ACQUIRE_NOT_REGULAR);
    CHECK(!unlink(fifo));
}


static void cancellation(const char *path)
{
    write_fixture(73);
    size_t baseline = descriptor_count();
    bool reached_success = false;
    for (unsigned cancel_at = 0; cancel_at < 100; ++cancel_at) {
        TbSymbolBudget_t budget;
        CHECK(tb_symbol_budget_init(&budget, 128 * 1024));
        TbSymbolAcquire_t *acquisition = tb_symbol_acquire_create(path, storage, ++next_slot, 100, &budget);
        CHECK(acquisition);
        for (unsigned tick = 0; tick < cancel_at; ++tick) {
            uint64_t size;
            if (tb_symbol_acquire_offer(acquisition, &size))
                CHECK(tb_symbol_acquire_grant(acquisition, size));
            if (step(acquisition, 17) == TB_SYMBOL_SUCCESS) {
                reached_success = true;
                break;
            }
        }
        tb_symbol_acquire_destroy(acquisition);
        CHECK(!budget.used && descriptor_count() == baseline);
        if (reached_success)
            break;
    }
    CHECK(reached_success);
}


int main(void)
{
    char directory[] = "/tmp/tb-symbol-acquire-XXXXXX";
    CHECK(mkdtemp(directory));
    char path[256];
    CHECK(snprintf(path, sizeof(path), "%s/origin", directory) > 0);
    fixture_fd = open(path, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    CHECK(fixture_fd >= 0);
    char pool_path[256];
    CHECK(snprintf(pool_path, sizeof(pool_path), "%s/pool", directory) > 0);
    storage = tb_symbol_storage_create(pool_path, NULL, NULL);
    CHECK(storage);
    TbSymbolOutcome_e setup = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 16 && setup == TB_SYMBOL_PENDING; ++i)
        setup = tb_symbol_storage_step(storage);
    CHECK(setup == TB_SYMBOL_SUCCESS);
    size_t baseline = descriptor_count();
    retained_bytes(path, 0, 64);
    retained_bytes(path, 4097, 1);
    retained_bytes(path, 131073, 65536);
    short_io = true;
    retained_bytes(path, 257, 64);
    short_io = false;
    faults(path, directory);
    rejected_changes(path, directory);
    cancellation(path);
    TbSymbolAcquire_t *acquisition = tb_symbol_acquire_create(directory, storage, ++next_slot, 100, NULL);
    CHECK(acquisition);
    failed(acquisition, TB_SYMBOL_UNSUPPORTED, TB_SYMBOL_ACQUIRE_NOT_REGULAR);
    acquisition = tb_symbol_acquire_create("/dev/null", storage, ++next_slot, 100, NULL);
    CHECK(acquisition);
    failed(acquisition, TB_SYMBOL_UNSUPPORTED, TB_SYMBOL_ACQUIRE_NOT_REGULAR);
    CHECK(!tb_symbol_acquire_create(NULL, storage, ++next_slot, 100, NULL));
    CHECK(!tb_symbol_acquire_create(path, NULL, ++next_slot, 100, NULL));
    CHECK(descriptor_count() == baseline);
    CHECK(!close(fixture_fd));
    for (uint64_t slot = 1; slot <= next_slot; ++slot) {
        TbSymbolStorageInfo_t info;
        if (tb_symbol_storage_info(storage, slot, &info))
            CHECK(tb_symbol_storage_unlink(storage, slot, &info.key));
        else
            CHECK(errno == ENOENT);
    }
    CHECK(tb_symbol_storage_remove(storage));
    tb_symbol_storage_destroy(storage);
    CHECK(!unlink(path));
    CHECK(!rmdir(directory));
    return 0;
}
