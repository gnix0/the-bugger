#ifndef THE_BUGGER_SYMBOL_DIES_H
#define THE_BUGGER_SYMBOL_DIES_H

#include "symbol/abbrev.h"
#include "symbol/form.h"
#include "symbol/units.h"

typedef struct tb_symbol_dies TbSymbolDies_t;

typedef struct {
    uint64_t unit_index;
    int      descriptor;
} TbSymbolDieSelection_t;

typedef struct {
    uint64_t offset;
    uint64_t end_offset;
    uint64_t parent;
    uint64_t code;
    uint64_t tag;
    uint64_t first_attribute;
    uint64_t attribute_count;
    bool     children;
} TbSymbolDieEntry_t;

typedef struct {
    uint64_t           schema_attribute;
    uint64_t           name;
    uint64_t           declared_form;
    uint64_t           offset;
    TbSymbolFormView_t value;
} TbSymbolDieAttribute_t;

typedef struct {
    const TbSymbolDieEntry_t     *entries;
    const TbSymbolDieAttribute_t *attributes;
    uint64_t                     count;
    uint64_t                     attribute_count;
    uint64_t                     max_depth;
    uint64_t                     section_index;
    uint64_t                     unit_offset;
    uint64_t                     abbrev_section;
    uint64_t                     abbrev_offset;
    TbSymbolDwarfUnitKind_e       kind;
    TbSymbolStorageInfo_t         backing;
} TbSymbolDiesView_t;

typedef enum {
    TB_SYMBOL_DIES_NONE,
    TB_SYMBOL_DIES_REQUEST,
    TB_SYMBOL_DIES_READ,
    TB_SYMBOL_DIES_CODE,
    TB_SYMBOL_DIES_FORM,
    TB_SYMBOL_DIES_TREE,
    TB_SYMBOL_DIES_ROOT,
    TB_SYMBOL_DIES_TYPE_OFFSET,
    TB_SYMBOL_DIES_RESOURCE,
    TB_SYMBOL_DIES_RECONSTRUCTION
} TbSymbolDiesFailure_e;

typedef struct {
    TbSymbolDiesFailure_e reason;
    uint64_t              offset;
    uint64_t              code;
    TbSymbolFormStatus_e  form;
    TbSymbolFileFailure_t read;
} TbSymbolDiesFailure_t;

/* Selected supported unit from a complete header inventory, and its complete
 * matching abbreviation table. Borrows directory/schema/descriptor/content lease
 * through destruction. Creation performs no I/O. Caller establishes section
 * classification; backing, version and selected abbreviation offset must match.
 * All owned state, scratch, arrays and parent stack use construction. */
TbSymbolDies_t *tb_symbol_dies_create(TbSymbolSectionsView_t directory, TbSymbolUnitsView_t units,
                                     TbSymbolDieSelection_t selection, const TbSymbolAbbrev_t *abbrev,
                                     TbSymbolBudget_t *construction);
TbSymbolOutcome_e tb_symbol_dies_step(TbSymbolDies_t *dies, size_t max_bytes);
/* Complete structural unit only. Entry/attribute offsets and payload coordinates
 * are section-relative; value.bytes remains an encoded length. parent is an
 * entry index, UINT64_MAX for the root. Null closing/padding entries are not
 * semantic records. Schema and duplicate attributes retain physical order.
 * No reference/string-table target, type/location or name semantics follows.
 * Wider raw values retain spans without fabricated numbers. No prefix on error. */
bool tb_symbol_dies_view(const TbSymbolDies_t *dies, TbSymbolDiesView_t *view);
TbSymbolDiesFailure_t tb_symbol_dies_failure(const TbSymbolDies_t *dies);
uint64_t tb_symbol_dies_progress(const TbSymbolDies_t *dies);
void tb_symbol_dies_destroy(TbSymbolDies_t *dies);

#endif // THE_BUGGER_SYMBOL_DIES_H
