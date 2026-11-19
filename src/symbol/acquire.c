#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Linux private filesystem backing. */
#include "symbol/acquire.h"
#include "symbol/storage.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

enum {
    ACQUIRE_BUFFER_BYTES = 65536
};

typedef enum {
    ACQUIRE_ORIGIN,
    ACQUIRE_INSPECT,
    ACQUIRE_READER,
    ACQUIRE_READER_INSPECT,
    ACQUIRE_OFFER,
    ACQUIRE_STORAGE_DIRECTORY,
    ACQUIRE_STORAGE_PROFILE,
    ACQUIRE_STORAGE,
    ACQUIRE_DIRECTORY_CLOSE,
    ACQUIRE_READ,
    ACQUIRE_WRITE,
    ACQUIRE_SOURCE_CHECK,
    ACQUIRE_PATH_CHECK,
    ACQUIRE_SOURCE_CLOSE,
    ACQUIRE_ORIGIN_CLOSE,
    ACQUIRE_PROTECT,
    ACQUIRE_READONLY,
    ACQUIRE_BACKING_CHECK,
    ACQUIRE_WRITER_CLOSE,
    ACQUIRE_HASH,
    ACQUIRE_DONE,
    ACQUIRE_FAILED
} AcquirePhase_e;

struct tb_symbol_acquire {
    TbSymbolBudget_t        *construction;
    const char              *origin;
    const char              *storage_directory;
    struct stat              source;
    TbSha256_t               hash;
    uint64_t                 file_limit;
    uint64_t                 bytes;
    uint64_t                 copied;
    uint64_t                 hashed;
    uint64_t                 progress;
    size_t                   buffered;
    size_t                   written;
    int                      origin_fd;
    int                      source_fd;
    int                      writer_fd;
    int                      directory_fd;
    int                      readonly_fd;
    int                      system_error;
    AcquirePhase_e           phase;
    TbSymbolOutcome_e        outcome;
    TbSymbolAcquireFailure_e failure;
    unsigned char            digest[TB_SHA256_DIGEST_BYTES];
    unsigned char            buffer[ACQUIRE_BUFFER_BYTES];
};

TbSymbolAcquire_t *tb_symbol_acquire_create(const char *origin, const char *storage_directory,
                                           uint64_t file_limit, TbSymbolBudget_t *construction)
{
    if (!origin || !*origin || !storage_directory || !*storage_directory || !file_limit ||
        (construction && !tb_symbol_budget_reserve(construction, sizeof(TbSymbolAcquire_t)))) {
        return NULL;
    }
    TbSymbolAcquire_t *acquisition = calloc(1, sizeof(*acquisition));
    if (!acquisition) {
        if (construction)
            (void)tb_symbol_budget_release(construction, sizeof(TbSymbolAcquire_t));
        return NULL;
    }
    acquisition->construction      = construction;
    acquisition->origin            = origin;
    acquisition->storage_directory = storage_directory;
    acquisition->file_limit        = file_limit;
    acquisition->origin_fd         = -1;
    acquisition->source_fd         = -1;
    acquisition->writer_fd         = -1;
    acquisition->directory_fd      = -1;
    acquisition->readonly_fd       = -1;
    (void)tb_sha256_init(&acquisition->hash);
    return acquisition;
}


static void fail(TbSymbolAcquire_t *acquisition, TbSymbolAcquireFailure_e failure, int error)
{
    acquisition->failure = failure;
    acquisition->system_error = error;
    acquisition->phase = ACQUIRE_FAILED;
    if (failure == TB_SYMBOL_ACQUIRE_FILE_LIMIT || error == ENOSPC || error == EDQUOT ||
        error == ENOMEM || error == EMFILE || error == ENFILE || error == EFBIG) {
        acquisition->outcome = TB_SYMBOL_RESOURCE_FAILURE;
    } else if (failure == TB_SYMBOL_ACQUIRE_NOT_REGULAR || error == EOPNOTSUPP || error == ENOSYS) {
        acquisition->outcome = TB_SYMBOL_UNSUPPORTED;
    } else {
        acquisition->outcome = TB_SYMBOL_OPERATIONAL_FAILURE;
    }
}


static bool source_matches(const struct stat *left, const struct stat *right)
{
    return left->st_dev == right->st_dev && left->st_ino == right->st_ino &&
           left->st_size == right->st_size && left->st_mtim.tv_sec == right->st_mtim.tv_sec &&
           left->st_mtim.tv_nsec == right->st_mtim.tv_nsec && left->st_ctim.tv_sec == right->st_ctim.tv_sec &&
           left->st_ctim.tv_nsec == right->st_ctim.tv_nsec && S_ISREG(right->st_mode);
}


static int reopen(int fd, bool nonblocking)
{
    char path[64];
    int length = snprintf(path, sizeof(path), "/proc/self/fd/%d", fd);
    if (length < 0 || (size_t)length >= sizeof(path)) {
        errno = EOVERFLOW;
        return -1;
    }
    unsigned flags = O_RDONLY | O_CLOEXEC;
    if (nonblocking)
        flags |= (unsigned)(O_NONBLOCK | O_NOCTTY);
    return open(path, (int)flags);
}


static void inspect_source(TbSymbolAcquire_t *acquisition, bool reader)
{
    struct stat info;
    if (fstat(reader ? acquisition->source_fd : acquisition->origin_fd, &info)) {
        if (errno != EINTR)
            fail(acquisition, TB_SYMBOL_ACQUIRE_IO, errno);
        return;
    }
    if (!S_ISREG(info.st_mode) || info.st_size < 0) {
        fail(acquisition, TB_SYMBOL_ACQUIRE_NOT_REGULAR, 0);
        return;
    }
    if (reader) {
        if (!source_matches(&acquisition->source, &info))
            fail(acquisition, TB_SYMBOL_ACQUIRE_CHANGED, 0);
        else
            acquisition->phase = ACQUIRE_OFFER;
        return;
    }
    if ((uint64_t)info.st_size > acquisition->file_limit) {
        fail(acquisition, TB_SYMBOL_ACQUIRE_FILE_LIMIT, 0);
        return;
    }
    acquisition->source = info;
    acquisition->bytes = (uint64_t)info.st_size;
    acquisition->phase = ACQUIRE_READER;
}


static void copy_read(TbSymbolAcquire_t *acquisition, size_t max_bytes)
{
    if (acquisition->copied == acquisition->bytes) {
        acquisition->phase = ACQUIRE_SOURCE_CHECK;
        return;
    }
    uint64_t remaining = acquisition->bytes - acquisition->copied;
    size_t length = remaining < max_bytes ? (size_t)remaining : max_bytes;
    ssize_t count = pread(acquisition->source_fd, acquisition->buffer, length, (off_t)acquisition->copied);
    if (count < 0) {
        if (errno != EINTR)
            fail(acquisition, TB_SYMBOL_ACQUIRE_IO, errno);
    } else if (!count) {
        fail(acquisition, TB_SYMBOL_ACQUIRE_CHANGED, 0);
    } else {
        acquisition->buffered = (size_t)count;
        acquisition->written = 0;
        acquisition->phase = ACQUIRE_WRITE;
    }
}


static void copy_write(TbSymbolAcquire_t *acquisition, size_t max_bytes)
{
    size_t length = acquisition->buffered - acquisition->written;
    if (length > max_bytes)
        length = max_bytes;
    ssize_t count = pwrite(acquisition->writer_fd, acquisition->buffer + acquisition->written,
                           length, (off_t)acquisition->copied);
    if (count < 0) {
        if (errno != EINTR)
            fail(acquisition, TB_SYMBOL_ACQUIRE_STORAGE, errno);
    } else if (!count) {
        fail(acquisition, TB_SYMBOL_ACQUIRE_STORAGE, EIO);
    } else {
        acquisition->written += (size_t)count;
        acquisition->copied += (uint64_t)count;
        if (acquisition->written == acquisition->buffered)
            acquisition->phase = ACQUIRE_READ;
    }
}


static void check_source(TbSymbolAcquire_t *acquisition, bool path)
{
    struct stat info;
    int result = path ? stat(acquisition->origin, &info) : fstat(acquisition->source_fd, &info);
    if (result) {
        if (errno != EINTR)
            fail(acquisition, path ? TB_SYMBOL_ACQUIRE_CHANGED : TB_SYMBOL_ACQUIRE_IO, errno);
    } else if (!source_matches(&acquisition->source, &info)) {
        fail(acquisition, TB_SYMBOL_ACQUIRE_CHANGED, 0);
    } else {
        acquisition->phase = path ? ACQUIRE_SOURCE_CLOSE : ACQUIRE_PATH_CHECK;
    }
}


static void close_owned(TbSymbolAcquire_t *acquisition, int *fd, AcquirePhase_e next)
{
    /* Linux releases the descriptor even on close errors; retrying could close
     * an unrelated descriptor. An error still rejects the candidate. */
    int owned = *fd;
    *fd = -1;
    if (close(owned))
        fail(acquisition, TB_SYMBOL_ACQUIRE_IO, errno);
    else
        acquisition->phase = next;
}


static void check_backing(TbSymbolAcquire_t *acquisition)
{
    struct stat info;
    if (fstat(acquisition->readonly_fd, &info)) {
        if (errno != EINTR)
            fail(acquisition, TB_SYMBOL_ACQUIRE_STORAGE, errno);
    } else if (!S_ISREG(info.st_mode) || info.st_size < 0 || (uint64_t)info.st_size != acquisition->bytes ||
               info.st_nlink || info.st_uid != geteuid() || (info.st_mode & 0777U) != 0400U) {
        fail(acquisition, TB_SYMBOL_ACQUIRE_INTEGRITY, 0);
    } else {
        acquisition->phase = ACQUIRE_WRITER_CLOSE;
    }
}


static void hash_read(TbSymbolAcquire_t *acquisition, size_t max_bytes)
{
    if (acquisition->hashed == acquisition->bytes) {
        if (!tb_sha256_final(&acquisition->hash, acquisition->bytes, acquisition->digest)) {
            fail(acquisition, TB_SYMBOL_ACQUIRE_INTEGRITY, EIO);
        } else {
            acquisition->phase = ACQUIRE_DONE;
            acquisition->outcome = TB_SYMBOL_SUCCESS;
        }
        return;
    }
    uint64_t remaining = acquisition->bytes - acquisition->hashed;
    size_t length = remaining < max_bytes ? (size_t)remaining : max_bytes;
    ssize_t count = pread(acquisition->readonly_fd, acquisition->buffer, length, (off_t)acquisition->hashed);
    if (count < 0) {
        if (errno != EINTR)
            fail(acquisition, TB_SYMBOL_ACQUIRE_IO, errno);
    } else if (!count || !tb_sha256_update(&acquisition->hash, acquisition->buffer, (size_t)count)) {
        fail(acquisition, TB_SYMBOL_ACQUIRE_INTEGRITY, 0);
    } else {
        acquisition->hashed += (uint64_t)count;
    }
}


TbSymbolOutcome_e tb_symbol_acquire_step(TbSymbolAcquire_t *acquisition, size_t max_bytes)
{
    if (!acquisition)
        return TB_SYMBOL_OPERATIONAL_FAILURE;
    if (!max_bytes || max_bytes > ACQUIRE_BUFFER_BYTES) {
        fail(acquisition, TB_SYMBOL_ACQUIRE_IO, EINVAL);
        return acquisition->outcome;
    }
    if (acquisition->phase == ACQUIRE_DONE || acquisition->phase == ACQUIRE_FAILED)
        return acquisition->outcome;
    AcquirePhase_e before = acquisition->phase;
    uint64_t copied = acquisition->copied;
    uint64_t hashed = acquisition->hashed;
    switch (acquisition->phase) {
    case ACQUIRE_ORIGIN:
        acquisition->origin_fd = open(acquisition->origin, O_PATH | O_CLOEXEC);
        if (acquisition->origin_fd >= 0)
            acquisition->phase = ACQUIRE_INSPECT;
        else if (errno != EINTR)
            fail(acquisition, TB_SYMBOL_ACQUIRE_IO, errno);
        break;
    case ACQUIRE_INSPECT:
        inspect_source(acquisition, false);
        break;
    case ACQUIRE_READER:
        acquisition->source_fd = reopen(acquisition->origin_fd, true);
        if (acquisition->source_fd >= 0)
            acquisition->phase = ACQUIRE_READER_INSPECT;
        else if (errno != EINTR)
            fail(acquisition, TB_SYMBOL_ACQUIRE_IO, errno);
        break;
    case ACQUIRE_READER_INSPECT:
        inspect_source(acquisition, true);
        break;
    case ACQUIRE_OFFER:
        break;
    case ACQUIRE_STORAGE_DIRECTORY:
        acquisition->directory_fd = open(acquisition->storage_directory, O_PATH | O_DIRECTORY | O_CLOEXEC);
        if (acquisition->directory_fd >= 0)
            acquisition->phase = ACQUIRE_STORAGE_PROFILE;
        else if (errno != EINTR)
            fail(acquisition, TB_SYMBOL_ACQUIRE_STORAGE, errno);
        break;
    case ACQUIRE_STORAGE_PROFILE: {
        struct statfs filesystem;
        if (fstatfs(acquisition->directory_fd, &filesystem)) {
            if (errno != EINTR)
                fail(acquisition, TB_SYMBOL_ACQUIRE_STORAGE, errno);
        } else if (!tb_symbol_storage_local_type((unsigned long)filesystem.f_type)) {
            fail(acquisition, TB_SYMBOL_ACQUIRE_STORAGE, EOPNOTSUPP);
        } else {
            acquisition->phase = ACQUIRE_STORAGE;
        }
        break;
    }
    case ACQUIRE_STORAGE:
        acquisition->writer_fd = openat(acquisition->directory_fd, ".",
                                         O_TMPFILE | O_EXCL | O_RDWR | O_CLOEXEC, 0400);
        if (acquisition->writer_fd >= 0)
            acquisition->phase = ACQUIRE_DIRECTORY_CLOSE;
        else if (errno != EINTR)
            fail(acquisition, TB_SYMBOL_ACQUIRE_STORAGE, errno);
        break;
    case ACQUIRE_DIRECTORY_CLOSE:
        close_owned(acquisition, &acquisition->directory_fd, ACQUIRE_READ);
        break;
    case ACQUIRE_READ:
        copy_read(acquisition, max_bytes);
        break;
    case ACQUIRE_WRITE:
        copy_write(acquisition, max_bytes);
        break;
    case ACQUIRE_SOURCE_CHECK:
        check_source(acquisition, false);
        break;
    case ACQUIRE_PATH_CHECK:
        check_source(acquisition, true);
        break;
    case ACQUIRE_SOURCE_CLOSE:
        close_owned(acquisition, &acquisition->source_fd, ACQUIRE_ORIGIN_CLOSE);
        break;
    case ACQUIRE_ORIGIN_CLOSE:
        close_owned(acquisition, &acquisition->origin_fd, ACQUIRE_PROTECT);
        break;
    case ACQUIRE_PROTECT:
        if (!fchmod(acquisition->writer_fd, 0400))
            acquisition->phase = ACQUIRE_READONLY;
        else if (errno != EINTR)
            fail(acquisition, TB_SYMBOL_ACQUIRE_STORAGE, errno);
        break;
    case ACQUIRE_READONLY:
        acquisition->readonly_fd = reopen(acquisition->writer_fd, false);
        if (acquisition->readonly_fd >= 0)
            acquisition->phase = ACQUIRE_BACKING_CHECK;
        else if (errno != EINTR)
            fail(acquisition, TB_SYMBOL_ACQUIRE_STORAGE, errno);
        break;
    case ACQUIRE_BACKING_CHECK:
        check_backing(acquisition);
        break;
    case ACQUIRE_WRITER_CLOSE:
        close_owned(acquisition, &acquisition->writer_fd, ACQUIRE_HASH);
        break;
    case ACQUIRE_HASH:
        hash_read(acquisition, max_bytes);
        break;
    case ACQUIRE_DONE:
    case ACQUIRE_FAILED:
        break;
    }
    if (before != acquisition->phase || copied != acquisition->copied || hashed != acquisition->hashed) {
        if (acquisition->progress == UINT64_MAX)
            fail(acquisition, TB_SYMBOL_ACQUIRE_INTEGRITY, EOVERFLOW);
        else
            ++acquisition->progress;
    }
    return acquisition->outcome;
}


bool tb_symbol_acquire_offer(const TbSymbolAcquire_t *acquisition, uint64_t *bytes)
{
    if (!acquisition || !bytes || acquisition->phase != ACQUIRE_OFFER)
        return false;
    *bytes = acquisition->bytes;
    return true;
}


bool tb_symbol_acquire_grant(TbSymbolAcquire_t *acquisition, uint64_t bytes)
{
    if (!acquisition || acquisition->phase != ACQUIRE_OFFER || bytes != acquisition->bytes)
        return false;
    acquisition->phase = ACQUIRE_STORAGE_DIRECTORY;
    return true;
}


uint64_t tb_symbol_acquire_progress(const TbSymbolAcquire_t *acquisition)
{
    return acquisition ? acquisition->progress : 0;
}


TbSymbolAcquireFailure_e tb_symbol_acquire_failure(const TbSymbolAcquire_t *acquisition, int *system_error)
{
    if (system_error)
        *system_error = acquisition ? acquisition->system_error : EINVAL;
    return acquisition ? acquisition->failure : TB_SYMBOL_ACQUIRE_IO;
}


bool tb_symbol_acquire_take(TbSymbolAcquire_t *acquisition, TbSymbolAcquiredBytes_t *bytes)
{
    if (!acquisition || !bytes || acquisition->phase != ACQUIRE_DONE || acquisition->readonly_fd < 0)
        return false;
    TbSymbolAcquiredBytes_t result = {
        .bytes      = acquisition->bytes,
        .descriptor = acquisition->readonly_fd
    };
    memcpy(result.digest, acquisition->digest, sizeof(result.digest));
    *bytes = result;
    acquisition->readonly_fd = -1;
    return true;
}


void tb_symbol_acquire_destroy(TbSymbolAcquire_t *acquisition)
{
    if (!acquisition)
        return;
    int fds[] = {acquisition->origin_fd, acquisition->source_fd,
                 acquisition->writer_fd, acquisition->readonly_fd, acquisition->directory_fd};
    for (size_t i = 0; i < sizeof(fds) / sizeof(fds[0]); ++i) {
        if (fds[i] >= 0)
            (void)close(fds[i]);
    }
    TbSymbolBudget_t *construction = acquisition->construction;
    free(acquisition);
    if (construction)
        (void)tb_symbol_budget_release(construction, sizeof(TbSymbolAcquire_t));
}
