#ifndef THE_BUGGER_SYMBOL_TABLES_H
#define THE_BUGGER_SYMBOL_TABLES_H

#include "symbol/code_attributes.h"
#include "symbol/indexed_tables.h"

typedef struct tb_symbol_tables TbSymbolTables_t;
typedef struct tb_symbol_table_value TbSymbolTableValue_t;

typedef struct {
    uint64_t                   section_index;
    TbSymbolIndexedTableKind_e kind;
    int                        descriptor;
} TbSymbolTablesSelection_t;

typedef struct {
    TbSymbolDwarfLength_t  extent;
    TbSymbolIndexedTable_t table;
    uint64_t              offset;
    bool                  header_available;
} TbSymbolTableContribution_t;

typedef struct {
    const TbSymbolTableContribution_t *entries;
    uint64_t                          count;
    uint64_t                          unsupported;
    uint64_t                          section_index;
    TbSymbolIndexedTableKind_e         kind;
    TbSymbolStorageInfo_t              backing;
} TbSymbolTablesView_t;

typedef enum {
    TB_SYMBOL_TABLES_NONE,
    TB_SYMBOL_TABLES_REQUEST,
    TB_SYMBOL_TABLES_SECTION,
    TB_SYMBOL_TABLES_READ,
    TB_SYMBOL_TABLES_HEADER,
    TB_SYMBOL_TABLES_RESOURCE,
    TB_SYMBOL_TABLES_RECONSTRUCTION,
    TB_SYMBOL_TABLES_ENTRY
} TbSymbolTablesFailure_e;

typedef struct {
    TbSymbolTablesFailure_e reason;
    uint64_t               offset;
    TbSymbolFileFailure_t   read;
    int                    system_error;
} TbSymbolTablesFailure_t;

/* Complete sequential header inventory for an explicitly classified retained
 * .debug_addr or .debug_rnglists section. Constructor performs no I/O. Borrow
 * directory, exact descriptor and content lease through destruction. All owned
 * state, scratch and exact array capacity require construction accounting;
 * no production capacity is selected. List bodies/entries are separate scopes. */
TbSymbolTables_t *tb_symbol_tables_create(TbSymbolSectionsView_t directory,
    TbSymbolTablesSelection_t selection, TbSymbolBudget_t *construction);
/* One reader/header/construction action; max_bytes is 64..65536. Two passes
 * count/reserve then populate. Outer jobs own revisions, interests, cancellation
 * and absolute deadlines. Unknown framed versions retain explicit coverage. */
TbSymbolOutcome_e tb_symbol_tables_step(TbSymbolTables_t *tables, size_t max_bytes);
bool tb_symbol_tables_view(const TbSymbolTables_t *tables, TbSymbolTablesView_t *view);
TbSymbolTablesFailure_t tb_symbol_tables_failure(const TbSymbolTables_t *tables);
uint64_t tb_symbol_tables_progress(const TbSymbolTables_t *tables);
void tb_symbol_tables_destroy(TbSymbolTables_t *tables);

typedef enum {
    TB_SYMBOL_TABLE_BIND_PENDING,
    TB_SYMBOL_TABLE_BIND_FOUND,
    TB_SYMBOL_TABLE_BIND_DEPENDENCY,
    TB_SYMBOL_TABLE_BIND_UNSUPPORTED,
    TB_SYMBOL_TABLE_BIND_MALFORMED,
    TB_SYMBOL_TABLE_BIND_INVALID
} TbSymbolTableBindStatus_e;

typedef struct {
    TbSymbolCodeFact_t     base;
    TbSymbolDwarfUnit_t    unit;
    TbSymbolIndexedTable_t table;
    uint64_t              section_index;
    uint64_t              contribution_index;
} TbSymbolTableBindingView_t;

/* Caller-owned/accounted state, borrowing the complete inventory and exact
 * COMPLETE code root view through discard. No allocation, I/O or inheritance. */
typedef struct {
    TbSymbolTablesView_t       tables;
    TbSymbolTableBindingView_t view;
    uint64_t                  cursor;
    TbSymbolTableBindStatus_e  status;
    bool                      unknown;
    bool                      found;
    bool                      initialized;
} TbSymbolTableBinding_t;

/* Missing base is DEPENDENCY, never inferred zero or first contribution.
 * A base must equal the entries offset of a sequentially proved contribution.
 * Wrong source backing/root preserves previous state. */
TbSymbolTableBindStatus_e tb_symbol_table_binding_init(TbSymbolTableBinding_t *binding,
    TbSymbolTablesView_t tables, TbSymbolCodeView_t root);
/* 1..256 contribution checks; no candidate view before the complete scope. */
TbSymbolTableBindStatus_e tb_symbol_table_binding_step(TbSymbolTableBinding_t *binding, size_t max_steps);
bool tb_symbol_table_binding_view(const TbSymbolTableBinding_t *binding, TbSymbolTableBindingView_t *view);
void tb_symbol_table_binding_discard(TbSymbolTableBinding_t *binding);

typedef struct {
    uint64_t index;
    int      descriptor;
} TbSymbolTableValueSelection_t;

typedef struct {
    TbSymbolTableBindingView_t binding;
    TbSymbolIndexedEntry_t     entry;
} TbSymbolTableValueView_t;

/* Selected entry from a FOUND binding over the same retained backing/directory.
 * Wrong/out-of-range requests return NULL without I/O. State and reader scratch
 * use construction; borrow binding evidence and descriptor through destruction.
 * Unsupported numeric widths/selectors complete explicitly as UNSUPPORTED. */
TbSymbolTableValue_t *tb_symbol_table_value_create(TbSymbolSectionsView_t directory,
    TbSymbolTableBindingView_t binding, TbSymbolTableValueSelection_t selection,
    TbSymbolBudget_t *construction);
TbSymbolOutcome_e tb_symbol_table_value_step(TbSymbolTableValue_t *value, size_t max_bytes);
/* Full numeric entry and exact binding provenance only. A range target is a
 * section offset, not list-boundary proof. Artifact addresses are not runtime
 * mappings, instruction boundaries, source correspondence or access authority. */
bool tb_symbol_table_value_view(const TbSymbolTableValue_t *value, TbSymbolTableValueView_t *view);
TbSymbolTablesFailure_t tb_symbol_table_value_failure(const TbSymbolTableValue_t *value);
uint64_t tb_symbol_table_value_progress(const TbSymbolTableValue_t *value);
void tb_symbol_table_value_destroy(TbSymbolTableValue_t *value);

#endif // THE_BUGGER_SYMBOL_TABLES_H
