#ifndef THE_BUGGER_TEST_SYMBOL_INLINE_DETAILS_H
#define THE_BUGGER_TEST_SYMBOL_INLINE_DETAILS_H

#include "symbol/inline_details.h"

void test_symbol_inline_details_run(TbSymbolUnitsView_t units, TbSymbolDiesView_t tree,
                                    TbSymbolAbbrevView_t schema, uint64_t unit_index,
                                    TbSymbolBudget_t *budget);
void test_symbol_inline_details_report(void);

#endif // THE_BUGGER_TEST_SYMBOL_INLINE_DETAILS_H
