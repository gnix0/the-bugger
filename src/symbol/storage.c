#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Protected Linux filesystem namespace. */
#include "symbol/storage.h"

#include <errno.h>
#include <fcntl.h>
#include <inttypes.h>
#include <linux/magic.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <sys/vfs.h>
#include <unistd.h>

typedef enum {
    STORAGE_CREATE,
    STORAGE_OPEN,
    STORAGE_INSPECT,
    STORAGE_PROFILE,
    STORAGE_READY,
    STORAGE_REMOVED,
    STORAGE_FAILED
} StoragePhase_e;

struct tb_symbol_storage {
    TbSymbolBudget_t      *construction;
    const char            *path;
    TbSymbolStorageKey_t   key;
    uint64_t               progress;
    StoragePhase_e         phase;
    TbSymbolOutcome_e      outcome;
    int                    directory;
    int                    error;
    bool                   reopening;
    bool                   created;
    bool                   identified;
};

_Static_assert(sizeof(dev_t) <= sizeof(uint64_t), "Storage keys require a representable device ID");
_Static_assert(sizeof(ino_t) <= sizeof(uint64_t), "Storage keys require a representable inode ID");

bool tb_symbol_storage_local_type(unsigned long type)
{
    return type == EXT4_SUPER_MAGIC || type == XFS_SUPER_MAGIC || type == BTRFS_SUPER_MAGIC ||
           type == TMPFS_MAGIC;
}


static TbSymbolStorageKey_t key_for(const struct stat *info)
{
    return (TbSymbolStorageKey_t){
        .device = (uint64_t)info->st_dev,
        .inode  = (uint64_t)info->st_ino
    };
}


static bool same_key(TbSymbolStorageKey_t left, TbSymbolStorageKey_t right)
{
    return left.device == right.device && left.inode == right.inode;
}


static void fail(TbSymbolStorage_t *storage, int error)
{
    storage->error = error;
    storage->phase = STORAGE_FAILED;
    storage->outcome = error == EOPNOTSUPP ? TB_SYMBOL_UNSUPPORTED : TB_SYMBOL_OPERATIONAL_FAILURE;
    if (error == ENOSPC || error == EDQUOT || error == ENOMEM || error == EMFILE || error == ENFILE)
        storage->outcome = TB_SYMBOL_RESOURCE_FAILURE;
}


TbSymbolStorage_t *tb_symbol_storage_create(const char *path, const TbSymbolStorageKey_t *expected,
                                           TbSymbolBudget_t *construction)
{
    if (!path || !*path ||
        (construction && !tb_symbol_budget_reserve(construction, sizeof(TbSymbolStorage_t)))) {
        return NULL;
    }
    TbSymbolStorage_t *storage = calloc(1, sizeof(*storage));
    if (!storage) {
        if (construction)
            (void)tb_symbol_budget_release(construction, sizeof(TbSymbolStorage_t));
        return NULL;
    }
    *storage = (TbSymbolStorage_t){
        .construction = construction,
        .path         = path,
        .key          = expected ? *expected : (TbSymbolStorageKey_t){0},
        .phase        = expected ? STORAGE_OPEN : STORAGE_CREATE,
        .directory    = -1,
        .reopening    = expected != NULL
    };
    return storage;
}


TbSymbolOutcome_e tb_symbol_storage_step(TbSymbolStorage_t *storage)
{
    if (!storage)
        return TB_SYMBOL_OPERATIONAL_FAILURE;
    StoragePhase_e before = storage->phase;
    switch (storage->phase) {
    case STORAGE_CREATE:
        if (!mkdir(storage->path, 0700)) {
            storage->created = true;
            storage->phase = STORAGE_OPEN;
        } else if (errno != EINTR) {
            fail(storage, errno);
        }
        break;
    case STORAGE_OPEN:
        storage->directory = open(storage->path, O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
        if (storage->directory >= 0)
            storage->phase = STORAGE_INSPECT;
        else if (errno != EINTR)
            fail(storage, errno);
        break;
    case STORAGE_INSPECT: {
        struct stat info;
        if (fstat(storage->directory, &info)) {
            if (errno != EINTR)
                fail(storage, errno);
        } else if (!S_ISDIR(info.st_mode) || info.st_uid != geteuid() || (info.st_mode & 0777U) != 0700U) {
            fail(storage, EACCES);
        } else if (storage->reopening && !same_key(storage->key, key_for(&info))) {
            fail(storage, ESTALE);
        } else {
            storage->key = key_for(&info);
            storage->identified = true;
            storage->phase = STORAGE_PROFILE;
        }
        break;
    }
    case STORAGE_PROFILE: {
        struct statfs filesystem;
        if (fstatfs(storage->directory, &filesystem)) {
            if (errno != EINTR)
                fail(storage, errno);
        } else if (!tb_symbol_storage_local_type((unsigned long)filesystem.f_type)) {
            fail(storage, EOPNOTSUPP);
        } else {
            storage->phase = STORAGE_READY;
            storage->outcome = TB_SYMBOL_SUCCESS;
        }
        break;
    }
    case STORAGE_READY:
    case STORAGE_REMOVED:
    case STORAGE_FAILED:
        break;
    }
    /* Initialization has four forward-only transitions; terminal phases never
     * advance, and EINTR does not count as progress. */
    if (storage->phase != before)
        ++storage->progress;
    return storage->outcome;
}


bool tb_symbol_storage_key(const TbSymbolStorage_t *storage, TbSymbolStorageKey_t *key)
{
    if (!storage || !key || !storage->identified)
        return false;
    *key = storage->key;
    return true;
}


bool tb_symbol_storage_created(const TbSymbolStorage_t *storage)
{
    return storage && storage->created;
}


int tb_symbol_storage_error(const TbSymbolStorage_t *storage)
{
    return storage ? storage->error : EINVAL;
}


uint64_t tb_symbol_storage_progress(const TbSymbolStorage_t *storage)
{
    return storage ? storage->progress : 0;
}


void tb_symbol_storage_destroy(TbSymbolStorage_t *storage)
{
    if (!storage)
        return;
    if (storage->directory >= 0)
        (void)close(storage->directory);
    TbSymbolBudget_t *construction = storage->construction;
    free(storage);
    if (construction)
        (void)tb_symbol_budget_release(construction, sizeof(TbSymbolStorage_t));
}


static bool leaf_name(const TbSymbolStorage_t *storage, uint64_t slot, char *name, size_t capacity)
{
    if (!storage || storage->phase != STORAGE_READY || !slot) {
        errno = EINVAL;
        return false;
    }
    int length = snprintf(name, capacity, "artifact-%016" PRIx64, slot);
    if (length < 0 || (size_t)length >= capacity) {
        errno = EOVERFLOW;
        return false;
    }
    return true;
}


static bool file_info(const struct stat *native, TbSymbolStorageInfo_t *info)
{
    if (!S_ISREG(native->st_mode) || native->st_size < 0 || native->st_nlink != 1 ||
        native->st_uid != geteuid() || (native->st_mode & 0777U) != 0400U) {
        errno = ESTALE;
        return false;
    }
    *info = (TbSymbolStorageInfo_t){
        .key   = key_for(native),
        .bytes = (uint64_t)native->st_size
    };
    return true;
}


int tb_symbol_storage_writer(TbSymbolStorage_t *storage, uint64_t slot)
{
    char name[48];
    if (!leaf_name(storage, slot, name, sizeof(name)))
        return -1;
    return openat(storage->directory, name, O_RDWR | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0400);
}


bool tb_symbol_storage_info(TbSymbolStorage_t *storage, uint64_t slot, TbSymbolStorageInfo_t *info)
{
    char name[48];
    if (!info || !leaf_name(storage, slot, name, sizeof(name))) {
        errno = EINVAL;
        return false;
    }
    struct stat native;
    return !fstatat(storage->directory, name, &native, AT_SYMLINK_NOFOLLOW) && file_info(&native, info);
}


int tb_symbol_storage_reader(TbSymbolStorage_t *storage, uint64_t slot,
                             const TbSymbolStorageInfo_t *expected)
{
    char name[48];
    if (!expected || !leaf_name(storage, slot, name, sizeof(name))) {
        errno = EINVAL;
        return -1;
    }
    int fd = openat(storage->directory, name, O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    struct stat native;
    TbSymbolStorageInfo_t actual;
    int error;
    if (fstat(fd, &native) || !file_info(&native, &actual)) {
        error = errno;
    } else if (!same_key(actual.key, expected->key) || actual.bytes != expected->bytes) {
        error = ESTALE;
    } else {
        return fd;
    }
    (void)close(fd);
    errno = error;
    return -1;
}


bool tb_symbol_storage_unlink(TbSymbolStorage_t *storage, uint64_t slot,
                              const TbSymbolStorageKey_t *expected)
{
    char name[48];
    if (!expected || !leaf_name(storage, slot, name, sizeof(name))) {
        errno = EINVAL;
        return false;
    }
    struct stat native;
    if (fstatat(storage->directory, name, &native, AT_SYMLINK_NOFOLLOW))
        return errno == ENOENT;
    if (!S_ISREG(native.st_mode) || native.st_uid != geteuid() || native.st_nlink != 1 ||
        !same_key(key_for(&native), *expected)) {
        errno = ESTALE;
        return false;
    }
    return !unlinkat(storage->directory, name, 0);
}


bool tb_symbol_storage_remove(TbSymbolStorage_t *storage)
{
    if (!storage || !storage->identified) {
        errno = EINVAL;
        return false;
    }
    if (storage->phase == STORAGE_REMOVED)
        return true;
    struct stat native;
    if (lstat(storage->path, &native)) {
        if (errno != ENOENT)
            return false;
        storage->phase = STORAGE_REMOVED;
        return true;
    }
    if (!same_key(key_for(&native), storage->key) || !S_ISDIR(native.st_mode)) {
        errno = ESTALE;
        return false;
    }
    if (rmdir(storage->path))
        return false;
    storage->phase = STORAGE_REMOVED;
    return true;
}


bool tb_symbol_storage_discard_empty(TbSymbolStorage_t *storage, uint64_t slot)
{
    char name[48];
    if (!leaf_name(storage, slot, name, sizeof(name)))
        return false;
    struct stat native;
    if (fstatat(storage->directory, name, &native, AT_SYMLINK_NOFOLLOW))
        return errno == ENOENT;
    if (!S_ISREG(native.st_mode) || native.st_uid != geteuid() || native.st_nlink != 1 || native.st_size) {
        errno = ESTALE;
        return false;
    }
    return !unlinkat(storage->directory, name, 0);
}
