#ifndef THE_BUGGER_SYMBOL_TYPE_RECIPE_H
#define THE_BUGGER_SYMBOL_TYPE_RECIPE_H

#include "symbol/inherited_reference.h"
#include "symbol/qualified.h"

typedef enum {
    TB_SYMBOL_TYPE_BASE,
    TB_SYMBOL_TYPE_ALIAS,
    TB_SYMBOL_TYPE_RECORD,
    TB_SYMBOL_TYPE_ENUM,
    TB_SYMBOL_TYPE_POINTER,
    TB_SYMBOL_TYPE_LVALUE_REFERENCE,
    TB_SYMBOL_TYPE_RVALUE_REFERENCE,
    TB_SYMBOL_TYPE_CONST,
    TB_SYMBOL_TYPE_VOLATILE,
    TB_SYMBOL_TYPE_RESTRICT,
    TB_SYMBOL_TYPE_UNKNOWN
} TbSymbolTypeRecipeKind_e;

typedef enum {
    TB_SYMBOL_TYPE_PENDING,
    TB_SYMBOL_TYPE_GRAPH,
    TB_SYMBOL_TYPE_NAME,
    TB_SYMBOL_TYPE_REFERENCES,
    TB_SYMBOL_TYPE_READY,
    TB_SYMBOL_TYPE_PARTIAL,
    TB_SYMBOL_TYPE_UNSUPPORTED,
    TB_SYMBOL_TYPE_CONFLICT,
    TB_SYMBOL_TYPE_DEPENDENCY,
    TB_SYMBOL_TYPE_MALFORMED,
    TB_SYMBOL_TYPE_RESOURCE,
    TB_SYMBOL_TYPE_OPERATIONAL,
    TB_SYMBOL_TYPE_INVALID
} TbSymbolTypeRecipeStatus_e;

typedef enum {
    TB_SYMBOL_TYPE_COMPLETE,
    TB_SYMBOL_TYPE_UNSUPPORTED_CONSTRUCTOR,
    TB_SYMBOL_TYPE_MISSING_TYPE,
    TB_SYMBOL_TYPE_ALTERNATIVE_TARGETS
} TbSymbolTypeRecipePartial_e;

/* One exact physical type description. Constructors are outermost first.
 * Names and reference facts remain borrowed complete child views. Named records
 * and aliases are opaque leaves: no member/layout/type-equivalence claim. */
typedef struct {
    TbSymbolReferenceTarget_t        record;
    TbSymbolGraphView_t              graph;
    TbSymbolQualifiedView_t          name;
    TbSymbolInheritedReferenceView_t references;
    TbSymbolTypeRecipeKind_e         kind;
    bool                             name_available;
    bool                             references_available;
} TbSymbolTypeRecipeNode_t;

typedef struct {
    TbSymbolReferenceTarget_t       selected;
    const TbSymbolTypeRecipeNode_t *nodes;
    size_t                          count;
    TbSymbolTypeRecipePartial_e     partial;
    bool                            complete;
} TbSymbolTypeRecipeView_t;

typedef struct {
    TbSymbolTypeRecipeStatus_e         status;
    TbSymbolReferenceTarget_t          record;
    TbSymbolQualifiedStatus_e          name_status;
    TbSymbolInheritedReferenceStatus_e reference_status;
    size_t                             node_index;
    size_t                             other_node_index;
} TbSymbolTypeRecipeFailure_t;

/* Caller owns/accounts state and full arena. Borrow every successful child
 * graph, name and reference view, including all dependencies, unchanged through
 * discard. Invalid initialization preserves old state/arena. No allocation,
 * I/O, discovery, callbacks, rendering, recursion or production size default.
 * Outer job owns captured revisions, cancellation, interests and deadlines. */
typedef struct {
    TbSymbolGraphView_t         pending_graph;
    TbSymbolReferenceTarget_t   next;
    TbSymbolTypeRecipeNode_t   *nodes;
    TbSymbolTypeRecipeFailure_t failure;
    TbSymbolTypeRecipeStatus_e  status;
    TbSymbolTypeRecipePartial_e partial;
    uint64_t                    progress;
    size_t                      capacity;
    size_t                      count;
    size_t                      checking;
    unsigned                    phase;
    bool                        alternatives;
    bool                        initialized;
} TbSymbolTypeRecipe_t;

TbSymbolTypeRecipeStatus_e tb_symbol_type_recipe_init(TbSymbolTypeRecipe_t *recipe,
                                                       TbSymbolGraphView_t selected,
                                                       TbSymbolTypeRecipeNode_t *nodes, size_t capacity);
/* 1..256 bounded classification, owner-target comparison or cycle checks. */
TbSymbolTypeRecipeStatus_e tb_symbol_type_recipe_step(TbSymbolTypeRecipe_t *recipe, size_t max_steps);
/* GRAPH/NAME/REFERENCES only; exact captured subject for the next child. */
bool tb_symbol_type_recipe_need(const TbSymbolTypeRecipe_t *recipe, TbSymbolReferenceTarget_t *record);
TbSymbolTypeRecipeStatus_e tb_symbol_type_recipe_graph(TbSymbolTypeRecipe_t *recipe,
                                                       const TbSymbolGraphView_t *graph);
TbSymbolTypeRecipeStatus_e tb_symbol_type_recipe_name(TbSymbolTypeRecipe_t *recipe,
                                                      TbSymbolReferenceTarget_t record,
                                                      TbSymbolQualifiedStatus_e status,
                                                      const TbSymbolQualifiedView_t *name);
TbSymbolTypeRecipeStatus_e tb_symbol_type_recipe_references(TbSymbolTypeRecipe_t *recipe,
                                                            TbSymbolReferenceTarget_t record,
                                                            TbSymbolInheritedReferenceStatus_e status,
                                                            const TbSymbolInheritedReferenceView_t *refs);
/* READY/PARTIAL only. PARTIAL is a validated explicit unsupported terminal,
 * never a malformed/resource/dependency failure disguised as a prefix. */
bool tb_symbol_type_recipe_view(const TbSymbolTypeRecipe_t *recipe, TbSymbolTypeRecipeView_t *view);
bool tb_symbol_type_recipe_failure(const TbSymbolTypeRecipe_t *recipe, TbSymbolTypeRecipeFailure_t *failure);
uint64_t tb_symbol_type_recipe_progress(const TbSymbolTypeRecipe_t *recipe);
/* End the borrowed result/work lifetime. Caller retains its arenas and child
 * ownership; this performs no child cancellation, allocation or I/O. */
void tb_symbol_type_recipe_discard(TbSymbolTypeRecipe_t *recipe);

#endif // THE_BUGGER_SYMBOL_TYPE_RECIPE_H
