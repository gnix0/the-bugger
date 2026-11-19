#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Private Linux storage fixtures. */
#include "symbol/storage.h"
#include "../support/check.h"

#include <dirent.h>
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


static TbSymbolOutcome_e finish(TbSymbolStorage_t *storage)
{
    for (unsigned i = 0; i < 16; ++i) {
        TbSymbolOutcome_e result = tb_symbol_storage_step(storage);
        if (result != TB_SYMBOL_PENDING)
            return result;
    }
    CHECK(false);
    return TB_SYMBOL_PENDING;
}


static void only_file(const char *directory, char *path, size_t capacity)
{
    DIR *entries = opendir(directory);
    CHECK(entries);
    struct dirent *entry;
    size_t count = 0;
    while ((entry = readdir(entries))) {
        if (entry->d_name[0] == '.')
            continue;
        ++count;
        int length = snprintf(path, capacity, "%s/%s", directory, entry->d_name);
        CHECK(length > 0 && (size_t)length < capacity);
    }
    CHECK(count == 1 && !closedir(entries));
}


static void retained(const char *path)
{
    TbSymbolBudget_t root;
    CHECK(tb_symbol_budget_init(&root, 4096));
    TbSymbolStorage_t *storage = tb_symbol_storage_create(path, NULL, &root);
    CHECK(storage && root.used && !tb_symbol_storage_created(storage));
    struct stat native;
    CHECK(lstat(path, &native) == -1 && errno == ENOENT);
    CHECK(finish(storage) == TB_SYMBOL_SUCCESS && tb_symbol_storage_created(storage));
    TbSymbolStorageKey_t namespace;
    CHECK(tb_symbol_storage_key(storage, &namespace));
    CHECK(!stat(path, &native) && (native.st_mode & 0777) == 0700);
    CHECK(tb_symbol_storage_writer(storage, 0) == -1 && errno == EINVAL);
    int writer = tb_symbol_storage_writer(storage, 1);
    CHECK(writer >= 0 && (fcntl(writer, F_GETFD) & FD_CLOEXEC));
    CHECK(write(writer, "old", 3) == 3 && !fchmod(writer, 0400));
    CHECK(tb_symbol_storage_writer(storage, 1) == -1 && errno == EEXIST);
    CHECK(!close(writer));
    TbSymbolStorageInfo_t file;
    CHECK(tb_symbol_storage_info(storage, 1, &file) && file.bytes == 3);
    int reader = tb_symbol_storage_reader(storage, 1, &file);
    CHECK(reader >= 0 && (fcntl(reader, F_GETFL) & O_ACCMODE) == O_RDONLY);
    CHECK(fcntl(reader, F_GETFD) & FD_CLOEXEC);
    CHECK(!tb_symbol_storage_remove(storage) && errno == ENOTEMPTY);
    TbSymbolStorageKey_t wrong = file.key;
    ++wrong.inode;
    CHECK(!tb_symbol_storage_unlink(storage, 1, &wrong) && errno == ESTALE);
    TbSymbolStorageInfo_t mismatch = file;
    ++mismatch.bytes;
    CHECK(tb_symbol_storage_reader(storage, 1, &mismatch) == -1 && errno == ESTALE);
    mismatch = file;
    mismatch.key = wrong;
    CHECK(tb_symbol_storage_reader(storage, 1, &mismatch) == -1 && errno == ESTALE);

    /* A restarted helper reconstructs only disposable namespace metadata. */
    tb_symbol_storage_destroy(storage);
    storage = tb_symbol_storage_create(path, &namespace, &root);
    CHECK(storage && finish(storage) == TB_SYMBOL_SUCCESS && !tb_symbol_storage_created(storage));
    int reopened = tb_symbol_storage_reader(storage, 1, &file);
    CHECK(reopened >= 0);
    char bytes[3];
    CHECK(read(reopened, bytes, sizeof(bytes)) == 3 && !memcmp(bytes, "old", 3));
    CHECK(!close(reopened));
    CHECK(!close(reader));
    CHECK(tb_symbol_storage_unlink(storage, 1, &file.key));
    CHECK(tb_symbol_storage_unlink(storage, 1, &file.key));
    CHECK(tb_symbol_storage_remove(storage));
    CHECK(tb_symbol_storage_writer(storage, 2) == -1 && errno == EINVAL);
    CHECK(!mkdir(path, 0700));
    CHECK(tb_symbol_storage_remove(storage));
    CHECK(!stat(path, &native));
    tb_symbol_storage_destroy(storage);
    CHECK(!root.used && !rmdir(path));
}


static void replaced_file(const char *path, const char *parent)
{
    TbSymbolStorage_t *storage = tb_symbol_storage_create(path, NULL, NULL);
    CHECK(storage && finish(storage) == TB_SYMBOL_SUCCESS);
    int writer = tb_symbol_storage_writer(storage, 1);
    CHECK(writer >= 0 && write(writer, "old", 3) == 3 && !fchmod(writer, 0400) && !close(writer));
    TbSymbolStorageInfo_t file;
    CHECK(tb_symbol_storage_info(storage, 1, &file));
    char leaf[512];
    char moved[512];
    only_file(path, leaf, sizeof(leaf));
    CHECK(snprintf(moved, sizeof(moved), "%s/kept", parent) > 0);
    CHECK(!rename(leaf, moved));
    writer = open(leaf, O_RDWR | O_CREAT | O_EXCL | O_CLOEXEC, 0400);
    CHECK(writer >= 0 && write(writer, "new", 3) == 3 && !fchmod(writer, 0400) && !close(writer));
    CHECK(tb_symbol_storage_reader(storage, 1, &file) == -1 && errno == ESTALE);
    CHECK(!tb_symbol_storage_unlink(storage, 1, &file.key) && errno == ESTALE);
    TbSymbolStorageInfo_t replacement;
    CHECK(tb_symbol_storage_info(storage, 1, &replacement));
    int reader = tb_symbol_storage_reader(storage, 1, &replacement);
    char bytes[3];
    CHECK(reader >= 0 && read(reader, bytes, sizeof(bytes)) == 3 && !memcmp(bytes, "new", 3));
    CHECK(!close(reader));
    CHECK(tb_symbol_storage_unlink(storage, 1, &replacement.key));
    CHECK(!unlink(moved));
    CHECK(tb_symbol_storage_remove(storage));
    tb_symbol_storage_destroy(storage);
}


static void replaced_namespace(const char *path, const char *parent)
{
    TbSymbolStorage_t *storage = tb_symbol_storage_create(path, NULL, NULL);
    CHECK(storage && finish(storage) == TB_SYMBOL_SUCCESS);
    TbSymbolStorageKey_t key;
    CHECK(tb_symbol_storage_key(storage, &key));
    char moved[512];
    CHECK(snprintf(moved, sizeof(moved), "%s/moved", parent) > 0);
    CHECK(!rename(path, moved) && !mkdir(path, 0700));
    CHECK(!tb_symbol_storage_remove(storage) && errno == ESTALE);
    TbSymbolStorage_t *reopened = tb_symbol_storage_create(path, &key, NULL);
    CHECK(reopened && finish(reopened) == TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(tb_symbol_storage_error(reopened) == ESTALE);
    tb_symbol_storage_destroy(reopened);
    CHECK(!rmdir(path) && !rename(moved, path));
    CHECK(tb_symbol_storage_remove(storage));
    tb_symbol_storage_destroy(storage);
}


static void cancellation(const char *path)
{
    for (unsigned stop = 0; stop < 5; ++stop) {
        TbSymbolBudget_t root;
        CHECK(tb_symbol_budget_init(&root, 4096));
        TbSymbolStorage_t *storage = tb_symbol_storage_create(path, NULL, &root);
        CHECK(storage);
        for (unsigned i = 0; i < stop; ++i)
            CHECK(tb_symbol_storage_step(storage) != TB_SYMBOL_OPERATIONAL_FAILURE);
        bool created = tb_symbol_storage_created(storage);
        tb_symbol_storage_destroy(storage);
        CHECK(!root.used);
        if (created) {
            struct stat native;
            CHECK(!stat(path, &native));
            CHECK(!rmdir(path));
        }
    }
}


static void failures(const char *path)
{
    TbSymbolBudget_t root;
    CHECK(tb_symbol_budget_init(&root, 4096));
    fail_allocation = true;
    CHECK(!tb_symbol_storage_create(path, NULL, &root) && !root.used);
    fail_allocation = false;
    root.limit = 1;
    CHECK(!tb_symbol_storage_create(path, NULL, &root) && !root.used);
    root.limit = 4096;
    CHECK(!mkdir(path, 0700));
    TbSymbolStorage_t *storage = tb_symbol_storage_create(path, NULL, &root);
    CHECK(storage && finish(storage) == TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(!tb_symbol_storage_created(storage) && tb_symbol_storage_error(storage) == EEXIST);
    tb_symbol_storage_destroy(storage);
    CHECK(!root.used && !rmdir(path));

    storage = tb_symbol_storage_create(path, NULL, &root);
    CHECK(storage && finish(storage) == TB_SYMBOL_SUCCESS);
    TbSymbolStorageKey_t key;
    CHECK(tb_symbol_storage_key(storage, &key));
    TbSymbolStorageKey_t wrong = key;
    ++wrong.inode;
    TbSymbolStorage_t *other = tb_symbol_storage_create(path, &wrong, &root);
    CHECK(other && finish(other) == TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(tb_symbol_storage_error(other) == ESTALE);
    CHECK(!tb_symbol_storage_remove(other));
    tb_symbol_storage_destroy(other);
    CHECK(!chmod(path, 0755));
    other = tb_symbol_storage_create(path, &key, &root);
    CHECK(other && finish(other) == TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(tb_symbol_storage_error(other) == EACCES);
    tb_symbol_storage_destroy(other);
    CHECK(!chmod(path, 0700));
    CHECK(tb_symbol_storage_remove(storage));
    tb_symbol_storage_destroy(storage);
    CHECK(!root.used);
}


int main(void)
{
    char parent[] = "/tmp/tb-symbol-storage-XXXXXX";
    CHECK(mkdtemp(parent));
    char path[256];
    CHECK(snprintf(path, sizeof(path), "%s/pool", parent) > 0);
    retained(path);
    replaced_file(path, parent);
    replaced_namespace(path, parent);
    cancellation(path);
    failures(path);
    CHECK(!rmdir(parent));
    return 0;
}
