#ifndef THE_BUGGER_TEST_SYMBOL_CALLABLE_H
#define THE_BUGGER_TEST_SYMBOL_CALLABLE_H

#include "symbol/parameter_source.h"
#include "symbol/qualified.h"
#include "symbol/type_recipe.h"

typedef struct {
    TbSymbolUnitsView_t  units;
    TbSymbolDiesView_t   tree;
    TbSymbolAbbrevView_t schema;
    uint64_t            unit_index;
} TestSymbolCallableSource_t;

typedef void (*test_symbol_callable_provider_fn)(void *context, TbSymbolReferenceTarget_t record);

typedef struct {
    test_symbol_callable_provider_fn name;
    test_symbol_callable_provider_fn type;
    void                            *context;
} TestSymbolCallableProviders_t;

/* Adopt successful metadata while the provider's source leases are still live.
 * Every copied allocation is charged and stays alive until composition ends. */
void test_symbol_callable_capture_name(TbSymbolQualifiedStatus_e status,
                                       const TbSymbolQualifiedView_t *name);
void test_symbol_callable_capture_type(TbSymbolTypeRecipeStatus_e status,
                                       const TbSymbolTypeRecipeView_t *type);
size_t test_symbol_callable_owned(void);
void test_symbol_callable_run(TestSymbolCallableSource_t source, TbSymbolParameterSourceView_t parameters,
                               TestSymbolCallableProviders_t providers, TbSymbolBudget_t *construction);
void test_symbol_callable_report(void);

#endif // THE_BUGGER_TEST_SYMBOL_CALLABLE_H
