#ifndef THE_BUGGER_SYMBOL_QUALIFIED_H
#define THE_BUGGER_SYMBOL_QUALIFIED_H

#include "symbol/constants.h"
#include "symbol/inherited_text.h"
#include "symbol/name_order.h"
#include "symbol/scope_graph.h"

typedef struct tb_symbol_qualified TbSymbolQualified_t;

typedef enum {
    TB_SYMBOL_QUALIFIED_PENDING,
    TB_SYMBOL_QUALIFIED_SOURCE,
    TB_SYMBOL_QUALIFIED_READY,
    TB_SYMBOL_QUALIFIED_UNNAMED,
    TB_SYMBOL_QUALIFIED_UNSUPPORTED,
    TB_SYMBOL_QUALIFIED_CONFLICT,
    TB_SYMBOL_QUALIFIED_DEPENDENCY,
    TB_SYMBOL_QUALIFIED_MALFORMED,
    TB_SYMBOL_QUALIFIED_RESOURCE,
    TB_SYMBOL_QUALIFIED_OPERATIONAL,
    TB_SYMBOL_QUALIFIED_INVALID
} TbSymbolQualifiedStatus_e;

typedef enum {
    TB_SYMBOL_QUALIFIED_NAME,
    TB_SYMBOL_QUALIFIED_LANGUAGE
} TbSymbolQualifiedNeedKind_e;

typedef struct {
    TbSymbolQualifiedNeedKind_e kind;
    size_t                      node_index;
    size_t                      anchor_index;
    TbSymbolReferenceTarget_t   record;
    TbSymbolGraphContext_t      context;
} TbSymbolQualifiedNeed_t;

/* Each recipe retains a value representative, never a canonical physical
 * entity. parent_node == SIZE_MAX denotes an empty unit-boundary prefix.
 * All incoming physical derivations remain in the retained scope graph. */
typedef struct {
    TbSymbolInheritedTextView_t name;
    size_t                      parent_node;
    size_t                      part_count;
    uint64_t                    bytes;
    bool                        complete;
} TbSymbolQualifiedNode_t;

typedef struct {
    TbSymbolReferenceTarget_t      selected;
    TbSymbolScopeView_t            scope;
    const TbSymbolQualifiedNode_t *nodes;
    size_t                         count;
    const TbSymbolConstantView_t  *languages;
    size_t                         language_count;
} TbSymbolQualifiedView_t;

typedef enum {
    TB_SYMBOL_QUALIFIED_NONE,
    TB_SYMBOL_QUALIFIED_REQUEST,
    TB_SYMBOL_QUALIFIED_ALLOCATION,
    TB_SYMBOL_QUALIFIED_SCOPE,
    TB_SYMBOL_QUALIFIED_NAME_VALUE,
    TB_SYMBOL_QUALIFIED_LANGUAGE_VALUE,
    TB_SYMBOL_QUALIFIED_PARENT_PROFILE,
    TB_SYMBOL_QUALIFIED_LENGTH,
    TB_SYMBOL_QUALIFIED_AGREEMENT,
    TB_SYMBOL_QUALIFIED_COMPARISON
} TbSymbolQualifiedFailure_e;

typedef struct {
    TbSymbolQualifiedFailure_e reason;
    size_t                     node_index;
    size_t                     anchor_index;
    size_t                     other_anchor;
    TbSymbolNameOrderFailure_t comparison;
} TbSymbolQualifiedFailure_t;

/* Completed unchanged scope graph and directory only. Borrow every underlying
 * graph/context view, all supplied successful inherited-name fact arrays and
 * their source evidence, and the descriptor/content lease through destruction.
 * Providers must adopt pointer-free completed facts before freeing child scratch
 * and retain all original source dependencies independently. Owned state, node
 * slots, language facts, two full-capacity recipe workspaces and one transient
 * comparison use construction; no production sizing default is selected.
 * No constructor I/O, discovery or recursion. Outer job owns request/consumer
 * identities, captured revisions, cancellation and absolute deadlines. */
TbSymbolQualified_t *tb_symbol_qualified_create(TbSymbolSectionsView_t directory, TbSymbolScopeView_t scope,
                                                int descriptor, TbSymbolBudget_t *construction);
/* One bounded metadata or comparison action. max_bytes is 1..65536; comparison
 * retains its 64 KiB combined checkpoint. Invalid arguments preserve state. */
TbSymbolQualifiedStatus_e tb_symbol_qualified_step(TbSymbolQualified_t *qualified, size_t max_bytes);
bool tb_symbol_qualified_need(const TbSymbolQualified_t *qualified, TbSymbolQualifiedNeed_t *need);
/* Explicit final child outcomes only. Wrong selected record/context preserves
 * the request. FOUND borrows the entire unchanged fact array, not just its first
 * value representative. Missing/empty identifiers remain explicitly unnamed. */
TbSymbolQualifiedStatus_e tb_symbol_qualified_name(TbSymbolQualified_t               *qualified,
                                                   TbSymbolInheritedTextStatus_e      status,
                                                   const TbSymbolInheritedTextView_t *name);
/* Exact terminal-owner unit root only. Unknown/absent language cannot establish
 * a supported qualification profile. A copied constant fact retains its separate
 * direct/abbreviation provenance and source dependencies. */
TbSymbolQualifiedStatus_e tb_symbol_qualified_language(TbSymbolQualified_t          *qualified,
                                                       TbSymbolConstantStatus_e      status,
                                                       const TbSymbolConstantView_t *language);
/* Root recipe only, after complete dependency/value agreement. Known C unit
 * boundaries and C++ namespace/class/struct/union contexts form the initial
 * metadata profile. Components remain opaque byte strings, including embedded
 * :: spellings. No canonical source-language expression, type equivalence,
 * nonlocality, definition role, runtime mapping or action authority follows. */
bool tb_symbol_qualified_view(const TbSymbolQualified_t *qualified, TbSymbolQualifiedView_t *view);
TbSymbolQualifiedFailure_t tb_symbol_qualified_failure(const TbSymbolQualified_t *qualified);
uint64_t tb_symbol_qualified_progress(const TbSymbolQualified_t *qualified);
void tb_symbol_qualified_destroy(TbSymbolQualified_t *qualified);

#endif // THE_BUGGER_SYMBOL_QUALIFIED_H
