#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Reject Linux O_PATH descriptors. */
#include "symbol/equal.h"

#include <errno.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum {
    EQUALITY_HALF_BYTES = 32768
};

typedef enum {
    EQUALITY_LEFT_FLAGS,
    EQUALITY_LEFT_STAT,
    EQUALITY_RIGHT_FLAGS,
    EQUALITY_RIGHT_STAT,
    EQUALITY_LEFT_READ,
    EQUALITY_RIGHT_READ,
    EQUALITY_COMPARE,
    EQUALITY_LEFT_CHECK,
    EQUALITY_RIGHT_CHECK,
    EQUALITY_DONE
} EqualityPhase_e;

struct tb_symbol_equality {
    TbSymbolBudget_t          *construction;
    int                        descriptors[2];
    struct stat                initial[2];
    uint64_t                   bytes;
    uint64_t                   offset;
    uint64_t                   progress;
    size_t                     chunk;
    size_t                     filled;
    size_t                     compared;
    EqualityPhase_e            phase;
    TbSymbolOutcome_e          outcome;
    TbSymbolEqualityFailure_t  failure;
    bool                       equal;
    unsigned char              buffers[2][EQUALITY_HALF_BYTES];
};

TbSymbolEquality_t *tb_symbol_equal_create(int left_fd, int right_fd, uint64_t bytes,
                                          TbSymbolBudget_t *construction)
{
    if (left_fd < 0 || right_fd < 0 || bytes > INT64_MAX ||
        (construction && !tb_symbol_budget_reserve(construction, sizeof(TbSymbolEquality_t)))) {
        return NULL;
    }
    TbSymbolEquality_t *comparison = calloc(1, sizeof(*comparison));
    if (!comparison) {
        if (construction)
            (void)tb_symbol_budget_release(construction, sizeof(TbSymbolEquality_t));
        return NULL;
    }
    comparison->construction   = construction;
    comparison->descriptors[0] = left_fd;
    comparison->descriptors[1] = right_fd;
    comparison->bytes          = bytes;
    comparison->equal          = true;
    return comparison;
}


static void fail(TbSymbolEquality_t *comparison, TbSymbolEqualityFailure_t failure,
                 TbSymbolOutcome_e outcome)
{
    comparison->failure = failure;
    comparison->outcome = outcome;
    comparison->phase = EQUALITY_DONE;
}


static bool unchanged(const struct stat *left, const struct stat *right)
{
    return left->st_dev == right->st_dev && left->st_ino == right->st_ino &&
           left->st_size == right->st_size && left->st_mtim.tv_sec == right->st_mtim.tv_sec &&
           left->st_mtim.tv_nsec == right->st_mtim.tv_nsec && left->st_ctim.tv_sec == right->st_ctim.tv_sec &&
           left->st_ctim.tv_nsec == right->st_ctim.tv_nsec;
}


static void flags(TbSymbolEquality_t *comparison, unsigned input)
{
    int value = fcntl(comparison->descriptors[input], F_GETFL);
    if (value < 0) {
        if (errno != EINTR) {
            fail(comparison, (TbSymbolEqualityFailure_t){
                .input_mask   = 1U << input,
                .system_error = errno
            }, TB_SYMBOL_OPERATIONAL_FAILURE);
        }
    } else if (((unsigned)value & (unsigned)O_PATH) ||
               ((unsigned)value & (unsigned)O_ACCMODE) != O_RDONLY) {
        fail(comparison, (TbSymbolEqualityFailure_t){
            .input_mask   = 1U << input,
            .system_error = 0
        }, TB_SYMBOL_UNSUPPORTED);
    } else {
        comparison->phase = input ? EQUALITY_RIGHT_STAT : EQUALITY_LEFT_STAT;
    }
}


static void inspect(TbSymbolEquality_t *comparison, unsigned input, bool final)
{
    struct stat info;
    if (fstat(comparison->descriptors[input], &info)) {
        if (errno != EINTR) {
            fail(comparison, (TbSymbolEqualityFailure_t){
                .input_mask   = 1U << input,
                .system_error = errno
            }, TB_SYMBOL_OPERATIONAL_FAILURE);
        }
    } else if (!S_ISREG(info.st_mode) || info.st_size < 0 || (uint64_t)info.st_size != comparison->bytes ||
               (final && !unchanged(&comparison->initial[input], &info))) {
        fail(comparison, (TbSymbolEqualityFailure_t){
            .input_mask   = 1U << input,
            .system_error = 0
        }, TB_SYMBOL_MALFORMED);
    } else if (final) {
        comparison->phase = input ? EQUALITY_DONE : EQUALITY_RIGHT_CHECK;
        if (input)
            comparison->outcome = TB_SYMBOL_SUCCESS;
    } else {
        comparison->initial[input] = info;
        comparison->phase = input ? EQUALITY_LEFT_READ : EQUALITY_RIGHT_FLAGS;
    }
}


static void read_input(TbSymbolEquality_t *comparison, bool right, size_t max_bytes)
{
    unsigned input = right ? 1U : 0U;
    if (!input && !comparison->filled) {
        if (comparison->offset == comparison->bytes) {
            comparison->phase = EQUALITY_LEFT_CHECK;
            return;
        }
        uint64_t remaining = comparison->bytes - comparison->offset;
        size_t chunk = max_bytes / 2;
        if (chunk > EQUALITY_HALF_BYTES)
            chunk = EQUALITY_HALF_BYTES;
        comparison->chunk = remaining < chunk ? (size_t)remaining : chunk;
    }
    size_t length = comparison->chunk - comparison->filled;
    if (length > max_bytes)
        length = max_bytes;
    ssize_t count = pread(comparison->descriptors[input], comparison->buffers[input] + comparison->filled,
                          length, (off_t)(comparison->offset + comparison->filled));
    if (count < 0) {
        if (errno != EINTR) {
            fail(comparison, (TbSymbolEqualityFailure_t){
                .input_mask   = 1U << input,
                .system_error = errno
            }, TB_SYMBOL_OPERATIONAL_FAILURE);
        }
    } else if (!count) {
        fail(comparison, (TbSymbolEqualityFailure_t){
            .input_mask   = 1U << input,
            .system_error = 0
        }, TB_SYMBOL_MALFORMED);
    } else {
        comparison->filled += (size_t)count;
        if (comparison->filled == comparison->chunk) {
            comparison->filled = 0;
            comparison->phase = input ? EQUALITY_COMPARE : EQUALITY_RIGHT_READ;
        }
    }
}


TbSymbolOutcome_e tb_symbol_equal_step(TbSymbolEquality_t *comparison, size_t max_bytes)
{
    if (!comparison)
        return TB_SYMBOL_OPERATIONAL_FAILURE;
    if (comparison->outcome != TB_SYMBOL_PENDING)
        return comparison->outcome;
    if (max_bytes < 2 || max_bytes > (size_t)EQUALITY_HALF_BYTES * 2) {
        fail(comparison, (TbSymbolEqualityFailure_t){
            .input_mask   = 3,
            .system_error = EINVAL
        }, TB_SYMBOL_OPERATIONAL_FAILURE);
        return comparison->outcome;
    }
    EqualityPhase_e before = comparison->phase;
    size_t filled = comparison->filled;
    size_t compared = comparison->compared;
    switch (comparison->phase) {
    case EQUALITY_LEFT_FLAGS:
        flags(comparison, 0);
        break;
    case EQUALITY_LEFT_STAT:
        inspect(comparison, 0, false);
        break;
    case EQUALITY_RIGHT_FLAGS:
        flags(comparison, 1);
        break;
    case EQUALITY_RIGHT_STAT:
        inspect(comparison, 1, false);
        break;
    case EQUALITY_LEFT_READ:
        read_input(comparison, false, max_bytes);
        break;
    case EQUALITY_RIGHT_READ:
        read_input(comparison, true, max_bytes);
        break;
    case EQUALITY_COMPARE: {
        size_t count = comparison->chunk - comparison->compared;
        if (count > max_bytes / 2)
            count = max_bytes / 2;
        if (memcmp(comparison->buffers[0] + comparison->compared,
                   comparison->buffers[1] + comparison->compared, count) != 0) {
            comparison->equal = false;
            comparison->phase = EQUALITY_LEFT_CHECK;
        } else {
            comparison->compared += count;
            if (comparison->compared == comparison->chunk) {
                comparison->offset += comparison->chunk;
                comparison->compared = 0;
                comparison->phase = EQUALITY_LEFT_READ;
            }
        }
        break;
    }
    case EQUALITY_LEFT_CHECK:
        inspect(comparison, 0, true);
        break;
    case EQUALITY_RIGHT_CHECK:
        inspect(comparison, 1, true);
        break;
    case EQUALITY_DONE:
        break;
    }
    if (before != comparison->phase || filled != comparison->filled || compared != comparison->compared) {
        if (comparison->progress == UINT64_MAX) {
            fail(comparison, (TbSymbolEqualityFailure_t){
                .input_mask   = 3,
                .system_error = EOVERFLOW
            }, TB_SYMBOL_OPERATIONAL_FAILURE);
        } else {
            ++comparison->progress;
        }
    }
    return comparison->outcome;
}


bool tb_symbol_equal_result(const TbSymbolEquality_t *comparison, bool *equal)
{
    if (!comparison || !equal || comparison->outcome != TB_SYMBOL_SUCCESS)
        return false;
    *equal = comparison->equal;
    return true;
}


TbSymbolEqualityFailure_t tb_symbol_equal_failure(const TbSymbolEquality_t *comparison)
{
    return comparison ? comparison->failure : (TbSymbolEqualityFailure_t){
        .input_mask   = 3,
        .system_error = EINVAL
    };
}


uint64_t tb_symbol_equal_progress(const TbSymbolEquality_t *comparison)
{
    return comparison ? comparison->progress : 0;
}


void tb_symbol_equal_destroy(TbSymbolEquality_t *comparison)
{
    if (!comparison)
        return;
    TbSymbolBudget_t *construction = comparison->construction;
    free(comparison);
    if (construction)
        (void)tb_symbol_budget_release(construction, sizeof(TbSymbolEquality_t));
}
