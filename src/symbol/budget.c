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


bool tb_symbol_budget_init_child(TbSymbolBudget_t *budget, size_t limit, TbSymbolBudget_t *parent)
{
    if (!budget || !parent || !limit)
        return false;

    unsigned depth = 1;
    for (TbSymbolBudget_t *account = parent; account; account = account->parent) {
        if (account == budget || ++depth > 4)
            return false;
    }
    *budget = (TbSymbolBudget_t){
        .limit  = limit,
        .parent = parent
    };
    return true;
}


bool tb_symbol_budget_reserve(TbSymbolBudget_t *budget, size_t bytes)
{
    if (!budget)
        return false;

    unsigned depth = 0;
    for (TbSymbolBudget_t *account = budget; account; account = account->parent) {
        if (++depth > 4 || account->used > account->limit || bytes > account->limit - account->used)
            return false;
    }
    for (TbSymbolBudget_t *account = budget; account; account = account->parent)
        account->used += bytes;
    return true;
}


bool tb_symbol_budget_release(TbSymbolBudget_t *budget, size_t bytes)
{
    if (!budget)
        return false;

    unsigned depth = 0;
    for (TbSymbolBudget_t *account = budget; account; account = account->parent) {
        if (++depth > 4 || bytes > account->used)
            return false;
    }
    for (TbSymbolBudget_t *account = budget; account; account = account->parent)
        account->used -= bytes;
    return true;
}
