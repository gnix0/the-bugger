#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Reject Linux O_PATH descriptors. */
#include "symbol/file.h"
#include "symbol/reader.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    FILE_WINDOW_BYTES = 65536
};

typedef enum {
    FILE_IDLE,
    FILE_FLAGS,
    FILE_EXEC_FLAGS,
    FILE_BEFORE,
    FILE_READ,
    FILE_AFTER,
    FILE_DONE,
    FILE_FAILED
} FilePhase_e;

struct tb_symbol_file {
    TbSymbolBudget_t     *construction;
    TbSymbolStorageInfo_t expected;
    int                   descriptor;
    struct stat           initial;
    bool                  captured;
    TbSymbolFileRange_t   range;
    size_t                filled;
    uint64_t              progress;
    FilePhase_e           phase;
    TbSymbolOutcome_e     outcome;
    TbSymbolFileFailure_t failure;
    unsigned char         buffer[FILE_WINDOW_BYTES];
};

TbSymbolFile_t *tb_symbol_file_create(TbSymbolStorageInfo_t expected, int descriptor,
                                     TbSymbolBudget_t *construction)
{
    if (descriptor < 0 || expected.bytes > INT64_MAX || !construction ||
        !tb_symbol_budget_reserve(construction, sizeof(TbSymbolFile_t))) {
        return NULL;
    }
    TbSymbolFile_t *file = calloc(1, sizeof(*file));
    if (!file) {
        (void)tb_symbol_budget_release(construction, sizeof(TbSymbolFile_t));
        return NULL;
    }
    file->construction = construction;
    file->expected     = expected;
    file->descriptor   = descriptor;
    return file;
}


bool tb_symbol_file_request(TbSymbolFile_t *file, TbSymbolFileRange_t range)
{
    if (!file || (file->phase != FILE_IDLE && file->phase != FILE_DONE) || !range.bytes ||
        range.bytes > FILE_WINDOW_BYTES ||
        !tb_symbol_range(range.offset, range.bytes, file->expected.bytes)) {
        return false;
    }
    file->range   = range;
    file->filled  = 0;
    file->phase   = FILE_FLAGS;
    file->outcome = TB_SYMBOL_PENDING;
    return true;
}


static void fail(TbSymbolFile_t *file, TbSymbolFileFailure_e reason, int error)
{
    file->failure = (TbSymbolFileFailure_t){
        .reason       = reason,
        .system_error = error
    };
    file->outcome = error == ENOMEM || error == EMFILE || error == ENFILE
                      ? TB_SYMBOL_RESOURCE_FAILURE : TB_SYMBOL_OPERATIONAL_FAILURE;
    file->phase = FILE_FAILED;
}


static void flags(TbSymbolFile_t *file, bool exec)
{
    int value = fcntl(file->descriptor, exec ? F_GETFD : F_GETFL);
    if (value < 0) {
        if (errno != EINTR)
            fail(file, TB_SYMBOL_FILE_DESCRIPTOR, errno);
    } else if (exec ? !((unsigned)value & (unsigned)FD_CLOEXEC) :
               ((unsigned)value & (unsigned)O_PATH) ||
               ((unsigned)value & (unsigned)O_ACCMODE) != O_RDONLY) {
        fail(file, TB_SYMBOL_FILE_DESCRIPTOR, 0);
        file->outcome = TB_SYMBOL_UNSUPPORTED;
    } else {
        file->phase = exec ? FILE_BEFORE : FILE_EXEC_FLAGS;
    }
}


static bool unchanged(const struct stat *left, const struct stat *right)
{
    return left->st_mtim.tv_sec == right->st_mtim.tv_sec && left->st_mtim.tv_nsec == right->st_mtim.tv_nsec &&
           left->st_ctim.tv_sec == right->st_ctim.tv_sec && left->st_ctim.tv_nsec == right->st_ctim.tv_nsec;
}


static void inspect(TbSymbolFile_t *file, bool final)
{
    struct stat info;
    if (fstat(file->descriptor, &info)) {
        if (errno != EINTR)
            fail(file, TB_SYMBOL_FILE_IO, errno);
        return;
    }
    if (!S_ISREG(info.st_mode) || info.st_size < 0 || (uint64_t)info.st_size != file->expected.bytes ||
        (uint64_t)info.st_dev != file->expected.key.device ||
        (uint64_t)info.st_ino != file->expected.key.inode ||
        info.st_uid != geteuid() || info.st_nlink != 1 || (info.st_mode & 0777U) != 0400U ||
        (file->captured && !unchanged(&file->initial, &info))) {
        fail(file, TB_SYMBOL_FILE_CHANGED, 0);
    } else if (final) {
        file->phase   = FILE_DONE;
        file->outcome = TB_SYMBOL_SUCCESS;
    } else {
        if (!file->captured) {
            file->initial  = info;
            file->captured = true;
        }
        file->phase = FILE_READ;
    }
}


static void read_bytes(TbSymbolFile_t *file, size_t max_bytes)
{
    size_t length = file->range.bytes - file->filled;
    if (length > max_bytes)
        length = max_bytes;
    ssize_t count = pread(file->descriptor, file->buffer + file->filled, length,
                          (off_t)(file->range.offset + file->filled));
    if (count < 0) {
        if (errno != EINTR)
            fail(file, TB_SYMBOL_FILE_IO, errno);
    } else if (!count) {
        fail(file, TB_SYMBOL_FILE_CHANGED, 0);
    } else {
        file->filled += (size_t)count;
        if (file->filled == file->range.bytes)
            file->phase = FILE_AFTER;
    }
}


TbSymbolOutcome_e tb_symbol_file_step(TbSymbolFile_t *file, size_t max_bytes)
{
    if (!file)
        return TB_SYMBOL_OPERATIONAL_FAILURE;
    if (file->phase == FILE_DONE || file->phase == FILE_FAILED)
        return file->outcome;
    if (!max_bytes || max_bytes > FILE_WINDOW_BYTES || file->phase == FILE_IDLE) {
        fail(file, TB_SYMBOL_FILE_REQUEST, EINVAL);
        return file->outcome;
    }
    FilePhase_e before = file->phase;
    size_t filled = file->filled;
    switch (file->phase) {
    case FILE_FLAGS:
        flags(file, false);
        break;
    case FILE_EXEC_FLAGS:
        flags(file, true);
        break;
    case FILE_BEFORE:
        inspect(file, false);
        break;
    case FILE_READ:
        read_bytes(file, max_bytes);
        break;
    case FILE_AFTER:
        inspect(file, true);
        break;
    case FILE_IDLE:
    case FILE_DONE:
    case FILE_FAILED:
        break;
    }
    if (before != file->phase || filled != file->filled) {
        if (file->progress == UINT64_MAX)
            fail(file, TB_SYMBOL_FILE_REQUEST, EOVERFLOW);
        else
            ++file->progress;
    }
    return file->outcome;
}


bool tb_symbol_file_view(const TbSymbolFile_t *file, TbSymbolFileView_t *view)
{
    if (!file || !view || file->phase != FILE_DONE || file->outcome != TB_SYMBOL_SUCCESS)
        return false;
    *view = (TbSymbolFileView_t){
        .data   = file->buffer,
        .offset = file->range.offset,
        .bytes  = file->range.bytes
    };
    return true;
}


TbSymbolFileFailure_t tb_symbol_file_failure(const TbSymbolFile_t *file)
{
    return file ? file->failure : (TbSymbolFileFailure_t){
        .reason       = TB_SYMBOL_FILE_REQUEST,
        .system_error = EINVAL
    };
}


uint64_t tb_symbol_file_progress(const TbSymbolFile_t *file)
{
    return file ? file->progress : 0;
}


void tb_symbol_file_destroy(TbSymbolFile_t *file)
{
    if (!file)
        return;
    (void)tb_symbol_budget_release(file->construction, sizeof(*file));
    free(file);
}
