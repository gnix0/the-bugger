#ifndef THE_BUGGER_SYMBOL_STRING_ORDER_H
#define THE_BUGGER_SYMBOL_STRING_ORDER_H

#include "symbol/strings.h"

typedef struct tb_symbol_string_order TbSymbolStringOrder_t;

typedef enum {
    TB_SYMBOL_STRING_BEFORE = -1,
    TB_SYMBOL_STRING_EQUAL = 0,
    TB_SYMBOL_STRING_AFTER = 1
} TbSymbolStringOrder_e;

typedef struct {
    TbSymbolSectionsView_t directory;
    TbSymbolStringView_t   string;
    int                    descriptor;
} TbSymbolStringOrderInput_t;

typedef enum {
    TB_SYMBOL_STRING_ORDER_NONE,
    TB_SYMBOL_STRING_ORDER_REQUEST,
    TB_SYMBOL_STRING_ORDER_SECTION,
    TB_SYMBOL_STRING_ORDER_RANGE,
    TB_SYMBOL_STRING_ORDER_READ,
    TB_SYMBOL_STRING_ORDER_SPAN
} TbSymbolStringOrderFailure_e;

typedef struct {
    TbSymbolStringOrderFailure_e reason;
    unsigned                     input;
    TbSymbolFileFailure_t        read;
} TbSymbolStringOrderFailure_t;

/* Two complete validated string views, not arbitrary guessed spans. Borrow both
 * directories, source proofs, descriptors and pinned backing through destruction.
 * Own checked construction state and two bounded readers; no constructor I/O.
 * Input 0 precedes input 1. Byte agreement never merges records or establishes
 * lexical-context equivalence, runtime correspondence or access authority. */
TbSymbolStringOrder_t *tb_symbol_string_order_create(const TbSymbolStringOrderInput_t inputs[2],
                                                     TbSymbolBudget_t *construction);
/* 1..65536 bytes per reader/operand slice; one filesystem syscall at most.
 * A comparison inspects at most 64 KiB combined across both operands.
 * Compares unsigned bytes through the shorter string's terminator, including
 * empty strings. No locale, Unicode normalization, hashing or summary truncation.
 * Invalid step arguments preserve existing state. Outer job supplies cancellation,
 * monotonic deadlines and captured-context revision checks. */
TbSymbolOutcome_e tb_symbol_string_order_step(TbSymbolStringOrder_t *comparison, size_t max_bytes);
bool tb_symbol_string_order_result(const TbSymbolStringOrder_t *comparison, TbSymbolStringOrder_e *order);
TbSymbolStringOrderFailure_t tb_symbol_string_order_failure(const TbSymbolStringOrder_t *comparison);
uint64_t tb_symbol_string_order_progress(const TbSymbolStringOrder_t *comparison);
void tb_symbol_string_order_destroy(TbSymbolStringOrder_t *comparison);

#endif // THE_BUGGER_SYMBOL_STRING_ORDER_H
