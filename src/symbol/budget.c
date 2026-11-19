#include "symbol/budget.h"

bool tb_symbol_budget_init(TbSymbolBudget_t *budget, size_t limit)
{
    if (!budget || !limit)
        return false;

    *budget = (TbSymbolBudget_t){
        .limit = limit
    };
    return true;
}


bool tb_symbol_budget_reserve(TbSymbolBudget_t *budget, size_t bytes)
{
    if (!budget || budget->used > budget->limit || bytes > budget->limit - budget->used)
        return false;

    budget->used += bytes;
    return true;
}


bool tb_symbol_budget_release(TbSymbolBudget_t *budget, size_t bytes)
{
    if (!budget || bytes > budget->used)
        return false;

    budget->used -= bytes;
    return true;
}
