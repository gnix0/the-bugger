#ifndef THE_BUGGER_SYMBOL_EQUAL_H
#define THE_BUGGER_SYMBOL_EQUAL_H

#include "symbol/schedule.h"

/* Helper-only comparison of two already frozen, pinned read-only backings.
 * Descriptors are borrowed until destruction; the caller preserves both exact
 * content dependencies. No digest/name/inode shortcut establishes equality.
 * Creation performs no I/O. Failure identifies affected input dependencies. */
typedef struct tb_symbol_equality TbSymbolEquality_t;

typedef struct {
    unsigned input_mask;
    int      system_error;
} TbSymbolEqualityFailure_t;

TbSymbolEquality_t *tb_symbol_equal_create(int left_fd, int right_fd, uint64_t bytes,
                                          TbSymbolBudget_t *construction);
/* One filesystem call or bounded comparison per step. max_bytes is 2..65536;
 * comparison charges inspection of both inputs and handles changing slices. */
TbSymbolOutcome_e tb_symbol_equal_step(TbSymbolEquality_t *comparison, size_t max_bytes);
/* A positive result requires all captured bytes and lengths to be checked.
 * Failure/pending leaves output untouched. This result grants no live authority. */
bool tb_symbol_equal_result(const TbSymbolEquality_t *comparison, bool *equal);
TbSymbolEqualityFailure_t tb_symbol_equal_failure(const TbSymbolEquality_t *comparison);
uint64_t tb_symbol_equal_progress(const TbSymbolEquality_t *comparison);
void tb_symbol_equal_destroy(TbSymbolEquality_t *comparison);

#endif // THE_BUGGER_SYMBOL_EQUAL_H
