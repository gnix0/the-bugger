#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Linux descriptor fixtures. */
#include "symbol/file.h"
#include "../support/check.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static bool fail_allocation;
static bool short_read;
static bool early_eof;
static bool mutate;
static int read_error;
static int writer = -1;
static size_t allowance;
static unsigned read_calls;
static unsigned char fixture[131073];

void *__real_calloc(size_t count, size_t bytes);
void *__wrap_calloc(size_t count, size_t bytes);
ssize_t __real_pread(int fd, void *bytes, size_t length, off_t offset);
ssize_t __wrap_pread(int fd, void *bytes, size_t length, off_t offset);

void *__wrap_calloc(size_t count, size_t bytes)
{
    return fail_allocation ? NULL : __real_calloc(count, bytes);
}


ssize_t __wrap_pread(int fd, void *bytes, size_t length, off_t offset)
{
    ++read_calls;
    CHECK(length && length <= allowance);
    if (read_error) {
        errno = read_error;
        read_error = 0;
        return -1;
    }
    if (early_eof)
        return 0;
    if (short_read && length > 1)
        length = 1;
    ssize_t count = __real_pread(fd, bytes, length, offset);
    if (mutate && count > 0) {
        mutate = false;
        struct stat info;
        CHECK(!fstat(writer, &info));
        CHECK(pwrite(writer, "Z", 1, 0) == 1);
        struct timespec times[2] = {
            {.tv_nsec = UTIME_OMIT},
            {
                .tv_sec  = info.st_mtim.tv_sec + 1,
                .tv_nsec = 0
            }
        };
        CHECK(!futimens(writer, times));
    }
    return count;
}


static TbSymbolStorageInfo_t expected(int fd)
{
    struct stat info;
    CHECK(!fstat(fd, &info) && info.st_size >= 0);
    return (TbSymbolStorageInfo_t){
        .key = {
            .device = (uint64_t)info.st_dev,
            .inode  = (uint64_t)info.st_ino
        },
        .bytes = (uint64_t)info.st_size
    };
}


static TbSymbolOutcome_e step(TbSymbolFile_t *file, size_t limit)
{
    allowance = limit;
    read_calls = 0;
    TbSymbolOutcome_e outcome = tb_symbol_file_step(file, limit);
    CHECK(read_calls <= 1);
    return outcome;
}


static TbSymbolOutcome_e finish(TbSymbolFile_t *file, size_t limit)
{
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 100000 && outcome == TB_SYMBOL_PENDING; ++i)
        outcome = step(file, limit);
    CHECK(outcome != TB_SYMBOL_PENDING);
    return outcome;
}


static void no_view(TbSymbolFile_t *file)
{
    TbSymbolFileView_t view;
    memset(&view, 0xa5, sizeof(view));
    unsigned char original[sizeof(view)];
    memcpy(original, &view, sizeof(view));
    CHECK(!tb_symbol_file_view(file, &view));
    CHECK(!memcmp(original, &view, sizeof(view)));
}


static void windows(int fd, TbSymbolBudget_t *budget)
{
    TbSymbolFile_t *file = tb_symbol_file_create(expected(fd), fd, budget);
    CHECK(file && budget->used);
    no_view(file);
    TbSymbolFileRange_t ranges[] = {
        {
            .offset = 0,
            .bytes  = 64
        },
        {
            .offset = 65536,
            .bytes  = 65536
        },
        {
            .offset = sizeof(fixture) - 1,
            .bytes  = 1
        },
        {
            .offset = 0,
            .bytes  = 65536
        }
    };
    TbSymbolFileRange_t invalid[] = {
        {
            .offset = 0,
            .bytes  = 0
        },
        {
            .offset = 0,
            .bytes  = 65537
        },
        {
            .offset = UINT64_MAX,
            .bytes  = 1
        },
        {
            .offset = sizeof(fixture),
            .bytes  = 1
        }
    };
    size_t limits[] = {1, 65536, 7, 257};
    for (size_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); ++i) {
        CHECK(tb_symbol_file_request(file, ranges[i]));
        CHECK(!tb_symbol_file_request(file, ranges[i]));
        no_view(file);
        TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
        while (outcome == TB_SYMBOL_PENDING) {
            no_view(file);
            outcome = step(file, limits[i]);
        }
        CHECK(outcome == TB_SYMBOL_SUCCESS);
        TbSymbolFileView_t view;
        CHECK(tb_symbol_file_view(file, &view));
        CHECK(view.offset == ranges[i].offset && view.bytes == ranges[i].bytes);
        CHECK(!memcmp(view.data, fixture + view.offset, view.bytes));
        for (size_t bad = 0; bad < sizeof(invalid) / sizeof(invalid[0]); ++bad)
            CHECK(!tb_symbol_file_request(file, invalid[bad]));
        TbSymbolFileView_t preserved;
        CHECK(tb_symbol_file_view(file, &preserved) && preserved.data == view.data);
        CHECK(preserved.bytes == view.bytes && preserved.offset == view.offset);
    }
    CHECK(tb_symbol_file_failure(file).reason == TB_SYMBOL_FILE_NONE);
    tb_symbol_file_destroy(file);
    CHECK(!budget->used && fcntl(fd, F_GETFD) >= 0);
}


static void errors(int fd, TbSymbolBudget_t *budget)
{
    int errors[] = {EIO, ENOMEM, 0};
    for (size_t i = 0; i < sizeof(errors) / sizeof(errors[0]); ++i) {
        TbSymbolFile_t *file = tb_symbol_file_create(expected(fd), fd, budget);
        CHECK(file && tb_symbol_file_request(file, (TbSymbolFileRange_t){
            .offset = 0,
            .bytes  = 32
        }));
        read_error = errors[i];
        early_eof = !errors[i];
        CHECK(finish(file, 7) == (errors[i] == ENOMEM
                  ? TB_SYMBOL_RESOURCE_FAILURE : TB_SYMBOL_OPERATIONAL_FAILURE));
        CHECK(tb_symbol_file_failure(file).system_error == errors[i]);
        CHECK(tb_symbol_file_failure(file).reason ==
              (early_eof ? TB_SYMBOL_FILE_CHANGED : TB_SYMBOL_FILE_IO));
        no_view(file);
        CHECK(!tb_symbol_file_request(file, (TbSymbolFileRange_t){
            .offset = 0,
            .bytes  = 1
        }));
        tb_symbol_file_destroy(file);
        CHECK(!budget->used);
        early_eof = false;
    }
    TbSymbolFile_t *file = tb_symbol_file_create(expected(fd), fd, budget);
    CHECK(file && tb_symbol_file_request(file, (TbSymbolFileRange_t){
        .offset = 0,
        .bytes  = 32
    }));
    for (unsigned i = 0; i < 3; ++i)
        CHECK(step(file, 7) == TB_SYMBOL_PENDING);
    read_error = EINTR;
    uint64_t progress = tb_symbol_file_progress(file);
    CHECK(step(file, 7) == TB_SYMBOL_PENDING && read_calls == 1);
    CHECK(tb_symbol_file_progress(file) == progress);
    short_read = true;
    CHECK(finish(file, 7) == TB_SYMBOL_SUCCESS);
    short_read = false;
    tb_symbol_file_destroy(file);
    CHECK(!budget->used);
}


static void rejected(int fd, TbSymbolStorageInfo_t info, TbSymbolOutcome_e outcome, TbSymbolBudget_t *budget)
{
    TbSymbolFile_t *file = tb_symbol_file_create(info, fd, budget);
    CHECK(file && tb_symbol_file_request(file, (TbSymbolFileRange_t){
        .offset = 0,
        .bytes  = 1
    }));
    CHECK(finish(file, 7) == outcome);
    no_view(file);
    tb_symbol_file_destroy(file);
    CHECK(!budget->used && fcntl(fd, F_GETFD) >= 0);
}


static void descriptors(const char *path, int fd, TbSymbolBudget_t *budget)
{
    TbSymbolStorageInfo_t info = expected(fd);
    rejected(writer, info, TB_SYMBOL_UNSUPPORTED, budget);
    int path_fd = open(path, O_PATH | O_CLOEXEC);
    CHECK(path_fd >= 0);
    rejected(path_fd, info, TB_SYMBOL_UNSUPPORTED, budget);
    CHECK(!close(path_fd));
    CHECK(!fcntl(fd, F_SETFD, 0));
    rejected(fd, info, TB_SYMBOL_UNSUPPORTED, budget);
    CHECK(!fcntl(fd, F_SETFD, FD_CLOEXEC));
    ++info.key.inode;
    rejected(fd, info, TB_SYMBOL_OPERATIONAL_FAILURE, budget);
    info = expected(fd);
    --info.bytes;
    rejected(fd, info, TB_SYMBOL_OPERATIONAL_FAILURE, budget);
    CHECK(!fchmod(writer, 0600));
    rejected(fd, expected(fd), TB_SYMBOL_OPERATIONAL_FAILURE, budget);
    CHECK(!fchmod(writer, 0400));
}


static void changes(int fd, TbSymbolBudget_t *budget)
{
    TbSymbolFile_t *file = tb_symbol_file_create(expected(fd), fd, budget);
    CHECK(file && tb_symbol_file_request(file, (TbSymbolFileRange_t){
        .offset = 0,
        .bytes  = 32
    }));
    mutate = true;
    CHECK(finish(file, 7) == TB_SYMBOL_OPERATIONAL_FAILURE && !mutate);
    CHECK(tb_symbol_file_failure(file).reason == TB_SYMBOL_FILE_CHANGED);
    no_view(file);
    tb_symbol_file_destroy(file);
    file = tb_symbol_file_create(expected(fd), fd, budget);
    CHECK(file && tb_symbol_file_request(file, (TbSymbolFileRange_t){
        .offset = 0,
        .bytes  = 32
    }));
    CHECK(finish(file, 7) == TB_SYMBOL_SUCCESS);
    struct stat before;
    CHECK(!fstat(writer, &before));
    struct timespec times[2] = {
        {.tv_nsec = UTIME_OMIT},
        {
            .tv_sec  = before.st_mtim.tv_sec + 1,
            .tv_nsec = 0
        }
    };
    CHECK(pwrite(writer, "Y", 1, 0) == 1 && !futimens(writer, times));
    CHECK(tb_symbol_file_request(file, (TbSymbolFileRange_t){
        .offset = 32,
        .bytes  = 32
    }));
    CHECK(finish(file, 7) == TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(tb_symbol_file_failure(file).reason == TB_SYMBOL_FILE_CHANGED);
    no_view(file);
    tb_symbol_file_destroy(file);
    file = tb_symbol_file_create(expected(fd), fd, budget);
    CHECK(file && tb_symbol_file_request(file, (TbSymbolFileRange_t){
        .offset = 0,
        .bytes  = 32
    }));
    CHECK(finish(file, 7) == TB_SYMBOL_SUCCESS);
    CHECK(!ftruncate(writer, sizeof(fixture) - 1));
    CHECK(tb_symbol_file_request(file, (TbSymbolFileRange_t){
        .offset = 32,
        .bytes  = 32
    }));
    CHECK(finish(file, 7) == TB_SYMBOL_OPERATIONAL_FAILURE);
    no_view(file);
    tb_symbol_file_destroy(file);
    CHECK(!budget->used);
}


int main(void)
{
    char path[] = "/tmp/tb-symbol-file-XXXXXX";
    writer = mkstemp(path);
    CHECK(writer >= 0);
    for (size_t i = 0; i < sizeof(fixture); ++i)
        fixture[i] = (unsigned char)(i * 37);
    CHECK(write(writer, fixture, sizeof(fixture)) == (ssize_t)sizeof(fixture));
    CHECK(!fchmod(writer, 0400));
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    CHECK(fd >= 0);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 128 * 1024));
    fail_allocation = true;
    CHECK(!tb_symbol_file_create(expected(fd), fd, &budget) && !budget.used);
    fail_allocation = false;
    budget.limit = 1;
    CHECK(!tb_symbol_file_create(expected(fd), fd, &budget) && !budget.used);
    budget.limit = 128 * 1024;
    TbSymbolStorageInfo_t invalid = expected(fd);
    invalid.bytes = UINT64_MAX;
    CHECK(!tb_symbol_file_create(invalid, fd, &budget) && !budget.used);
    CHECK(!tb_symbol_file_create(expected(fd), fd, NULL));
    windows(fd, &budget);
    errors(fd, &budget);
    descriptors(path, fd, &budget);
    for (unsigned stage = 0; stage < 24; ++stage) {
        TbSymbolFile_t *file = tb_symbol_file_create(expected(fd), fd, &budget);
        CHECK(file && tb_symbol_file_request(file, (TbSymbolFileRange_t){
            .offset = 0,
            .bytes  = 100
        }));
        for (unsigned i = 0; i < stage; ++i) {
            TbSymbolOutcome_e outcome = step(file, 7);
            CHECK(outcome == TB_SYMBOL_PENDING || outcome == TB_SYMBOL_SUCCESS);
        }
        tb_symbol_file_destroy(file);
        CHECK(!budget.used && fcntl(fd, F_GETFD) >= 0);
    }
    changes(fd, &budget);
    CHECK(!close(fd) && !close(writer) && !unlink(path));
    puts("PASS bounded retained-file windows, descriptor/range guards, cancellation and failure isolation");
    return 0;
}
