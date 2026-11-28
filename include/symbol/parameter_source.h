#ifndef THE_BUGGER_SYMBOL_PARAMETER_SOURCE_H
#define THE_BUGGER_SYMBOL_PARAMETER_SOURCE_H

#include "symbol/parameter_links.h"

typedef enum {
    TB_SYMBOL_PARAMETER_SOURCE_PENDING,
    TB_SYMBOL_PARAMETER_SOURCE_SOURCE,
    TB_SYMBOL_PARAMETER_SOURCE_READY,
    TB_SYMBOL_PARAMETER_SOURCE_PARTIAL,
    TB_SYMBOL_PARAMETER_SOURCE_CONFLICT,
    TB_SYMBOL_PARAMETER_SOURCE_MALFORMED,
    TB_SYMBOL_PARAMETER_SOURCE_DEPENDENCY,
    TB_SYMBOL_PARAMETER_SOURCE_RESOURCE,
    TB_SYMBOL_PARAMETER_SOURCE_INVALID
} TbSymbolParameterSourceStatus_e;

typedef enum {
    TB_SYMBOL_PARAMETER_SOURCE_NO_LIST      = 1u << 0u,
    TB_SYMBOL_PARAMETER_SOURCE_ALTERNATIVES = 1u << 1u,
    TB_SYMBOL_PARAMETER_SOURCE_CHILD        = 1u << 2u,
    TB_SYMBOL_PARAMETER_SOURCE_UNLINKED     = 1u << 3u
} TbSymbolParameterSourceReason_e;

/* One slot per callable, retained through discard. A signature-bearing class
 * ancestor makes its member inventory potentially reduced, even when nonempty.
 * first_signature and first_child are tree indexes, UINT64_MAX when absent.
 * selected_scope is SIZE_MAX when no unique source list was established.
 * targets[0..1] are specification/origin callable indexes, SIZE_MAX if absent. */
typedef struct {
    TbSymbolDiesView_t tree;
    size_t             targets[2];
    size_t             selected_scope;
    uint64_t           first_signature;
    uint64_t           signature_attribute;
    uint64_t           first_child;
    unsigned           reasons;
    bool               reduced;
    bool               variadic;
    bool               resolved;
} TbSymbolParameterSourceSlot_t;

/* reasons includes root selection limitations and missing correspondences in
 * any captured inventory selecting another source. first_unlinked_* are SIZE_MAX
 * without a gap. Slot reasons retain their own selection/mapping limitations. */
typedef struct {
    TbSymbolParameterLinksView_t         correspondence;
    const TbSymbolParameterSourceSlot_t *slots;
    size_t                               selected_scope;
    size_t                               first_unlinked_scope;
    size_t                               first_unlinked_parameter;
    unsigned                             reasons;
} TbSymbolParameterSourceView_t;

/* Cursor diagnostics: scope_index may equal the captured scope count at an
 * exhausted pass; entry/attribute indexes are UINT64_MAX when unavailable. */
typedef struct {
    TbSymbolParameterSourceStatus_e status;
    size_t                          scope_index;
    uint64_t                        entry_index;
    uint64_t                        attribute_index;
} TbSymbolParameterSourceFailure_t;

/* Caller owns/accounts state and full slot capacity; no production default.
 * Borrow a completed correspondence view, its complete dependency closure and
 * every supplied source tree unchanged through discard. State, slots and inputs
 * must not overlap. No allocation, I/O, discovery, recursion or live authority.
 * This selects a physical list, not a complete language-level prototype: C empty
 * lists, artificial/object parameters, types and qualifiers need later evidence. */
typedef struct {
    TbSymbolParameterLinksView_t     correspondence;
    TbSymbolParameterSourceSlot_t   *slots;
    TbSymbolParameterSourceStatus_e  status;
    TbSymbolParameterSourceFailure_t failure;
    TbSymbolAttributes_t             signature;
    uint64_t                         progress;
    uint64_t                         cursor;
    uint64_t                         ancestor;
    size_t                           scope_index;
    size_t                           parameter_index;
    size_t                           find;
    size_t                           remaining;
    size_t                           fact_index;
    size_t                           link_index;
    size_t                           first_unlinked_scope;
    size_t                           first_unlinked;
    unsigned                         edge;
    unsigned                         reasons;
    unsigned                         phase;
    bool                             changed;
    bool                             linked;
    bool                             initialized;
} TbSymbolParameterSource_t;

/* Checked rejection preserves state and the complete arena. */
TbSymbolParameterSourceStatus_e tb_symbol_parameter_source_init(
    TbSymbolParameterSource_t *state, TbSymbolParameterLinksView_t correspondence,
    TbSymbolParameterSourceSlot_t *slots, size_t capacity);
/* 1..256 bounded record, attribute, edge-lookup or correspondence actions.
 * Dependency ordering and target lookup may take quadratic total work; outer
 * jobs enforce revisions, consumer interest, cancellation and absolute deadlines. */
TbSymbolParameterSourceStatus_e tb_symbol_parameter_source_step(TbSymbolParameterSource_t *state,
                                                               size_t max_steps);
bool tb_symbol_parameter_source_need(const TbSymbolParameterSource_t *state,
                                      TbSymbolGraphNeed_t *need);
/* Wrong context preserves SOURCE. NULL views explicitly finalize DEPENDENCY.
 * Complete matching unit/tree evidence is required; trees stay borrowed. */
TbSymbolParameterSourceStatus_e tb_symbol_parameter_source_source(
    TbSymbolParameterSource_t *state, const TbSymbolUnitsView_t *units,
    const TbSymbolDiesView_t *tree, uint64_t unit_index);
/* READY/PARTIAL only, after all captured scopes have been inspected. Origin
 * instances never use their possibly omitted local children as a source list.
 * A non-reduced specification list is preferred; a potentially reduced one
 * permits fallback to a non-origin local inventory. Distinct preferred sources
 * remain alternatives. Unknown child constructs and missing explicit parameter
 * links, including intermediate inventories, retain partial reasons. No name/position/type matching. */
bool tb_symbol_parameter_source_view(const TbSymbolParameterSource_t *state,
                                      TbSymbolParameterSourceView_t *view);
bool tb_symbol_parameter_source_failure(const TbSymbolParameterSource_t *state,
                                         TbSymbolParameterSourceFailure_t *failure);
uint64_t tb_symbol_parameter_source_progress(const TbSymbolParameterSource_t *state);
void tb_symbol_parameter_source_discard(TbSymbolParameterSource_t *state);

#endif // THE_BUGGER_SYMBOL_PARAMETER_SOURCE_H
