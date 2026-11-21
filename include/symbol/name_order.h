#ifndef THE_BUGGER_SYMBOL_NAME_ORDER_H
#define THE_BUGGER_SYMBOL_NAME_ORDER_H

#include "symbol/string_order.h"

typedef struct tb_symbol_name_order TbSymbolNameOrder_t;

typedef struct {
    TbSymbolSectionsView_t      directory;
    const TbSymbolStringView_t *components;
    size_t                      count;
    int                         descriptor;
} TbSymbolNameOrderInput_t;

typedef struct {
    unsigned                     input;
    size_t                       component;
    TbSymbolStringOrderFailure_e reason;
    TbSymbolFileFailure_t        read;
} TbSymbolNameOrderFailure_t;

/* Compare two complete validated sequences of name strings joined with exact
 * :: bytes. A zero-component sequence is empty. Components may themselves
 * contain ::; their boundaries do not affect byte equality. Borrow both arrays,
 * directories, source proofs and descriptor/content leases through destruction.
 * All borrowed capacities are independently accounted by the caller. Own state
 * and two bounded readers use construction. No allocation of whole names and
 * no constructor I/O. This establishes value order only, never scope/entity
 * equivalence, complete qualification, runtime binding or access permission. */
TbSymbolNameOrder_t *tb_symbol_name_order_create(const TbSymbolNameOrderInput_t inputs[2],
                                                 TbSymbolBudget_t *construction);
/* 1..65536 bytes per read/operand slice, at most one filesystem syscall per call.
 * Profile one component or inspect at most 64 KiB combined per action. Outer
 * job owns cancellation, captured revisions and monotonic absolute deadlines.
 * Combined length overflow and read resource failures remain resource failures;
 * reader rejection is operational, distinct from unsupported metadata sections. */
TbSymbolOutcome_e tb_symbol_name_order_step(TbSymbolNameOrder_t *comparison, size_t max_bytes);
bool tb_symbol_name_order_result(const TbSymbolNameOrder_t *comparison, TbSymbolStringOrder_e *order);
TbSymbolNameOrderFailure_t tb_symbol_name_order_failure(const TbSymbolNameOrder_t *comparison);
uint64_t tb_symbol_name_order_progress(const TbSymbolNameOrder_t *comparison);
void tb_symbol_name_order_destroy(TbSymbolNameOrder_t *comparison);

#endif // THE_BUGGER_SYMBOL_NAME_ORDER_H
