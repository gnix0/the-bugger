#ifndef THE_BUGGER_SYMBOL_BUDGET_H
#define THE_BUGGER_SYMBOL_BUDGET_H

#include <stdbool.h>
#include <stddef.h>

/* Single-event-loop owned allocation accounting, not RSS or storage admission.
 * Shared allocations have one owner/charge. Reserve before allocating; undo a
 * failed allocation, and release only after the allocation is relinquished. */
typedef struct tb_symbol_budget {
    size_t                   limit;
    size_t                   used;
    struct tb_symbol_budget *parent;
} TbSymbolBudget_t;

bool tb_symbol_budget_init(TbSymbolBudget_t *budget, size_t limit);
/* At most four accounts per chain. Parent must outlive child; initialize before
 * acquiring ownership and never move/reinitialize an account with live charges. */
bool tb_symbol_budget_init_child(TbSymbolBudget_t *budget, size_t limit, TbSymbolBudget_t *parent);
bool tb_symbol_budget_reserve(TbSymbolBudget_t *budget, size_t bytes);
bool tb_symbol_budget_release(TbSymbolBudget_t *budget, size_t bytes);

#endif // THE_BUGGER_SYMBOL_BUDGET_H
