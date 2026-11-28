#ifndef THE_BUGGER_SYMBOL_CALLABLE_H
#define THE_BUGGER_SYMBOL_CALLABLE_H

#include "symbol/inherited_flag.h"
#include "symbol/parameter_source.h"
#include "symbol/type_recipe.h"

typedef enum {
    TB_SYMBOL_CALLABLE_PENDING,
    TB_SYMBOL_CALLABLE_REFERENCES,
    TB_SYMBOL_CALLABLE_FLAG,
    TB_SYMBOL_CALLABLE_TYPE,
    TB_SYMBOL_CALLABLE_READY,
    TB_SYMBOL_CALLABLE_PARTIAL,
    TB_SYMBOL_CALLABLE_CONFLICT,
    TB_SYMBOL_CALLABLE_DEPENDENCY,
    TB_SYMBOL_CALLABLE_UNSUPPORTED,
    TB_SYMBOL_CALLABLE_MALFORMED,
    TB_SYMBOL_CALLABLE_RESOURCE,
    TB_SYMBOL_CALLABLE_OPERATIONAL,
    TB_SYMBOL_CALLABLE_INVALID
} TbSymbolCallableStatus_e;

#define TB_SYMBOL_CALLABLE_SOURCE_LIMIT         (1u << 0u)
#define TB_SYMBOL_CALLABLE_LANGUAGE_LIMIT       (1u << 1u)
#define TB_SYMBOL_CALLABLE_TYPE_LIMIT           (1u << 2u)
#define TB_SYMBOL_CALLABLE_MISSING_TYPE         (1u << 3u)
#define TB_SYMBOL_CALLABLE_ALTERNATIVE_TYPES    (1u << 4u)
#define TB_SYMBOL_CALLABLE_OBJECT_MAPPING       (1u << 5u)
#define TB_SYMBOL_CALLABLE_OBJECT_ARTIFICIAL    (1u << 6u)
#define TB_SYMBOL_CALLABLE_ARTIFICIAL_PARAMETER (1u << 7u)
#define TB_SYMBOL_CALLABLE_OBJECT_TYPE          (1u << 8u)
#define TB_SYMBOL_CALLABLE_CLASS_CONTEXT        (1u << 9u)
#define TB_SYMBOL_CALLABLE_REF_PROFILE          (1u << 10u)
#define TB_SYMBOL_CALLABLE_PROTOTYPE            (1u << 11u)

typedef enum {
    TB_SYMBOL_CALLABLE_EXPLICIT_PARAMETER,
    TB_SYMBOL_CALLABLE_OBJECT_PARAMETER,
    TB_SYMBOL_CALLABLE_ARTIFICIAL_ARGUMENT,
    TB_SYMBOL_CALLABLE_VARIADIC
} TbSymbolCallableParameterKind_e;

typedef enum {
    TB_SYMBOL_CALLABLE_ORDINARY,
    TB_SYMBOL_CALLABLE_STATIC_MEMBER,
    TB_SYMBOL_CALLABLE_INSTANCE_MEMBER,
    TB_SYMBOL_CALLABLE_UNKNOWN_MEMBER
} TbSymbolCallableMember_e;

typedef struct {
    TbSymbolInheritedReferenceView_t references;
    TbSymbolTypeRecipeView_t         recipe;
    TbSymbolTypeRecipeStatus_e       recipe_status;
    bool                             recipe_available;
    bool                             absent;
    bool                             alternatives;
} TbSymbolCallableType_t;

/* Source order and exact physical identities are retained, including object,
 * artificial and unspecified entries. Only a proved OBJECT_PARAMETER can be
 * omitted by a later source-argument renderer. No argument is removed here. */
typedef struct {
    TbSymbolReferenceTarget_t       record;
    TbSymbolGraphView_t             graph;
    TbSymbolInheritedFlagView_t     artificial;
    TbSymbolCallableType_t          type;
    TbSymbolCallableParameterKind_e kind;
    bool                            artificial_available;
} TbSymbolCallableParameter_t;

typedef struct {
    TbSymbolParameterSourceView_t source;
    TbSymbolQualifiedView_t       name;
    TbSymbolConstantView_t        language;
} TbSymbolCallableInput_t;

typedef struct {
    TbSymbolCallableInput_t            input;
    TbSymbolCallableType_t             result;
    TbSymbolInheritedReferenceView_t   object;
    TbSymbolInheritedFlagView_t        reference;
    TbSymbolInheritedFlagView_t        rvalue_reference;
    TbSymbolInheritedFlagView_t        prototyped;
    const TbSymbolCallableParameter_t *parameters;
    size_t                             parameter_count;
    size_t                             object_parameter;
    TbSymbolCallableMember_e           member;
    unsigned                           reasons;
    bool                               method_const;
    bool                               method_volatile;
    bool                               method_cv_available;
    bool                               ref_qualifier_available;
    bool                               children_available;
    bool                               root_fields_available;
} TbSymbolCallableView_t;

typedef struct {
    TbSymbolCallableStatus_e  kind;
    TbSymbolReferenceTarget_t record;
    TbSymbolGraphView_t       graph;
    uint64_t                  field;
} TbSymbolCallableNeed_t;

typedef struct {
    TbSymbolCallableStatus_e status;
    TbSymbolCallableNeed_t   need;
    size_t                   parameter_index;
} TbSymbolCallableFailure_t;

/* Caller owns/accounts state and the full arena; inputs, child views and their
 * complete dependency closure stay borrowed unchanged through discard. State,
 * arena and child storage must not overlap. Keep initialized state stationary.
 * No allocation, I/O, discovery,
 * recursion, rendering or runtime authority. No production capacity default.
 * A complete result describes this metadata grammar, not ABI/type equivalence
 * or a GDB expression. An absent return type is retained as metadata absence,
 * without choosing void, constructor or destructor syntax. */
typedef struct {
    TbSymbolCallableView_t       view;
    TbSymbolCallableParameter_t *parameters;
    TbSymbolCallableNeed_t       need;
    TbSymbolCallableFailure_t    failure;
    TbSymbolCallableStatus_e     status;
    TbSymbolLanguageFamily_e     language;
    TbSymbolCallableType_t      *active_type;
    uint64_t                     progress;
    size_t                       parameter_index;
    size_t                       scan;
    size_t                       fact_index;
    size_t                       object_fact;
    size_t                       object_scope;
    size_t                       object_formal;
    size_t                       mapped_parameter;
    size_t                       anchor_index;
    size_t                       class_node;
    unsigned                     phase;
    unsigned                     type_next;
    bool                         object_failed;
    bool                         class_seen;
    bool                         ordinary_seen;
    bool                         dwarf4;
    bool                         initialized;
} TbSymbolCallable_t;
/* language is the validated direct language fact for the selected source's unit
 * root (root callable's unit when no source was selected). name is complete and
 * selects the exact root callable. Rejected initialization preserves state/arena. */
TbSymbolCallableStatus_e tb_symbol_callable_init(TbSymbolCallable_t *state,
                                                TbSymbolCallableInput_t input,
                                                TbSymbolCallableParameter_t *parameters, size_t capacity);
/* 1..256 bounded node, mapping, flag or constructor actions. Outer jobs own
 * revisions, consumer interest, cancellation, absolute deadlines and publication. */
TbSymbolCallableStatus_e tb_symbol_callable_step(TbSymbolCallable_t *state, size_t max_steps);
bool tb_symbol_callable_need(const TbSymbolCallable_t *state, TbSymbolCallableNeed_t *need);
/* Wrong subject/context/field preserves the pending request. Explicit terminal
 * child failures are propagated, never converted to empty success. Successful
 * child storage remains borrowed through discard. Unsupported type recipes
 * retain a partial reason and typed child status; dependency/resource/malformed
 * outcomes expose no successful prefix. */
TbSymbolCallableStatus_e tb_symbol_callable_references(TbSymbolCallable_t *state,
    TbSymbolReferenceTarget_t record, TbSymbolInheritedReferenceStatus_e status,
    const TbSymbolInheritedReferenceView_t *references);
TbSymbolCallableStatus_e tb_symbol_callable_flag(TbSymbolCallable_t *state,
    TbSymbolReferenceTarget_t record, TbSymbolInheritedFlagStatus_e status,
    const TbSymbolInheritedFlagView_t *flag);
TbSymbolCallableStatus_e tb_symbol_callable_type(TbSymbolCallable_t *state,
    TbSymbolReferenceTarget_t record, TbSymbolTypeRecipeStatus_e status,
    const TbSymbolTypeRecipeView_t *type);
bool tb_symbol_callable_view(const TbSymbolCallable_t *state, TbSymbolCallableView_t *view);
bool tb_symbol_callable_failure(const TbSymbolCallable_t *state, TbSymbolCallableFailure_t *failure);
uint64_t tb_symbol_callable_progress(const TbSymbolCallable_t *state);
void tb_symbol_callable_discard(TbSymbolCallable_t *state);

#endif // THE_BUGGER_SYMBOL_CALLABLE_H
