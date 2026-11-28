#ifndef THE_BUGGER_SYMBOL_INLINE_SCOPE_H
#define THE_BUGGER_SYMBOL_INLINE_SCOPE_H

#include "symbol/constants.h"
#include "symbol/references.h"

typedef enum {
    TB_SYMBOL_INLINE_SCOPE_PENDING,
    TB_SYMBOL_INLINE_SCOPE_SCHEMA,
    TB_SYMBOL_INLINE_SCOPE_READY,
    TB_SYMBOL_INLINE_SCOPE_RESOURCE,
    TB_SYMBOL_INLINE_SCOPE_DEPENDENCY,
    TB_SYMBOL_INLINE_SCOPE_CONFLICT,
    TB_SYMBOL_INLINE_SCOPE_UNSUPPORTED,
    TB_SYMBOL_INLINE_SCOPE_MALFORMED,
    TB_SYMBOL_INLINE_SCOPE_INVALID
} TbSymbolInlineScopeStatus_e;

/* Every examined physical subprogram retains its unique direct selection.
 * constant has a value only for FOUND; absence remains explicit. */
typedef struct {
    TbSymbolReferenceTarget_t record;
    TbSymbolConstantStatus_e  status;
    TbSymbolConstantView_t    constant;
    TbSymbolAttributeResult_t selection;
} TbSymbolInlineScopeFact_t;

typedef struct {
    TbSymbolReferenceTarget_t        selected;
    uint64_t                         selected_tag;
    TbSymbolReferenceTarget_t        abstract_root;
    TbSymbolReferenceTarget_t        concrete_root;
    const TbSymbolInlineScopeFact_t *facts;
    size_t                           count;
    bool                             abstract_available;
    bool                             concrete_available;
    bool                             selected_abstract;
    bool                             selected_concrete;
} TbSymbolInlineScopeView_t;

typedef struct {
    TbSymbolInlineScopeStatus_e status;
    TbSymbolReferenceTarget_t   record;
    uint64_t                    entry_index;
    TbSymbolConstantStatus_e    constant_status;
    TbSymbolAttributeResult_t   selection;
    TbSymbolConstantView_t      constant;
    bool                        value_available;
    bool                        record_available;
    bool                        constant_available;
    bool                        selection_available;
} TbSymbolInlineScopeFailure_t;

/* Caller-owned/accounted state and full-capacity fact storage. Borrows complete
 * tree/unit and any supplied exact abbreviation schema through discard. No
 * allocation, I/O, inheritance, discovery or recursion. This physical ancestry
 * claim is independent from specification/origin declaration contexts. */
typedef struct {
    TbSymbolDiesView_t          tree;
    TbSymbolUnitsView_t         units;
    TbSymbolConstant_t          constant;
    TbSymbolInlineScopeView_t   view;
    TbSymbolInlineScopeFact_t  *facts;
    TbSymbolInlineScopeStatus_e status;
    size_t                      capacity;
    size_t                      count;
    uint64_t                    unit_index;
    uint64_t                    selected;
    uint64_t                    active;
    uint64_t                    progress;
    bool                        inspecting;
    bool                        initialized;
} TbSymbolInlineScope_t;

TbSymbolInlineScopeStatus_e tb_symbol_inline_scope_init(TbSymbolInlineScope_t *scope,
                                                        TbSymbolUnitsView_t units,
                                                        TbSymbolDiesView_t tree,
                                                        uint64_t unit_index, uint64_t die_index,
                                                        TbSymbolInlineScopeFact_t *facts,
                                                        size_t capacity);
/* One ancestor or at most 256 constant-decoder actions. Outer job owns request
 * identity, revisions, cancellation, interest and absolute deadlines. */
TbSymbolInlineScopeStatus_e tb_symbol_inline_scope_step(TbSymbolInlineScope_t *scope, size_t max_steps);
TbSymbolInlineScopeStatus_e tb_symbol_inline_scope_schema(TbSymbolInlineScope_t *scope,
                                                          const TbSymbolAbbrevView_t *schema);
/* READY only. Walks the entire physical ancestry to establish the nearest
 * abstract root and nearest concrete-inline marker independently. Nonzero
 * standard inline codes, including declared_not_inlined, denote abstract roots.
 * Neither marker proves emitted ranges, concrete runtime existence, nonlocality,
 * definition role, source-language qualification or permission to act. */
bool tb_symbol_inline_scope_view(const TbSymbolInlineScope_t *scope, TbSymbolInlineScopeView_t *view);
/* Diagnostic normalized values retain their raw/abbreviation provenance; an
 * unknown integer code remains unsupported as an inline classification. */
bool tb_symbol_inline_scope_failure(const TbSymbolInlineScope_t *scope,
                                    TbSymbolInlineScopeFailure_t *failure);
uint64_t tb_symbol_inline_scope_progress(const TbSymbolInlineScope_t *scope);

#endif // THE_BUGGER_SYMBOL_INLINE_SCOPE_H
