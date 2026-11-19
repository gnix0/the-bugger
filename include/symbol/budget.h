#ifndef THE_BUGGER_SYMBOL_BUDGET_H
#define THE_BUGGER_SYMBOL_BUDGET_H

#include <stdbool.h>
#include <stddef.h>

/* Single-event-loop owned allocation accounting, not RSS or storage admission.
 * Shared allocations have one owner/charge. Reserve before allocating; undo a
 * failed allocation, and release only after the allocation is relinquished. */
typedef struct {
    size_t limit;
    size_t used;
} TbSymbolBudget_t;

bool tb_symbol_budget_init(TbSymbolBudget_t *budget, size_t limit);
bool tb_symbol_budget_reserve(TbSymbolBudget_t *budget, size_t bytes);
bool tb_symbol_budget_release(TbSymbolBudget_t *budget, size_t bytes);

#endif // THE_BUGGER_SYMBOL_BUDGET_H
