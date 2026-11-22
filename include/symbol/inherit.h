#ifndef THE_BUGGER_SYMBOL_INHERIT_H
#define THE_BUGGER_SYMBOL_INHERIT_H

#include "symbol/graph.h"

typedef enum {
    TB_SYMBOL_INHERIT_NAME           = 0x03,
    TB_SYMBOL_INHERIT_LINKAGE_NAME   = 0x6e,
    TB_SYMBOL_INHERIT_TYPE           = 0x49,
    TB_SYMBOL_INHERIT_OBJECT_POINTER = 0x64,
    TB_SYMBOL_INHERIT_DECL_FILE      = 0x3a,
    TB_SYMBOL_INHERIT_DECL_LINE      = 0x3b,
    TB_SYMBOL_INHERIT_DECL_COLUMN    = 0x39
} TbSymbolInheritField_e;

typedef enum {
    TB_SYMBOL_INHERIT_PENDING,
    TB_SYMBOL_INHERIT_SOURCE,
    TB_SYMBOL_INHERIT_UNIQUE,
    TB_SYMBOL_INHERIT_MULTIPLE,
    TB_SYMBOL_INHERIT_ABSENT,
    TB_SYMBOL_INHERIT_CONFLICT,
    TB_SYMBOL_INHERIT_DEPENDENCY,
    TB_SYMBOL_INHERIT_RESOURCE,
    TB_SYMBOL_INHERIT_UNSUPPORTED,
    TB_SYMBOL_INHERIT_INVALID
} TbSymbolInheritStatus_e;

typedef enum {
    TB_SYMBOL_INHERIT_UNVISITED,
    TB_SYMBOL_INHERIT_QUEUED,
    TB_SYMBOL_INHERIT_EXAMINED,
    TB_SYMBOL_INHERIT_OWNER
} TbSymbolInheritVisit_e;

typedef struct {
    TbSymbolInheritVisit_e    visit;
    TbSymbolAttributeResult_t selection;
    TbSymbolDieAttribute_t    attribute;
} TbSymbolInheritSlot_t;

typedef struct {
    size_t                    node_index;
    TbSymbolAttributeResult_t selection;
    TbSymbolDieAttribute_t    attribute;
} TbSymbolInheritOwner_t;

typedef struct {
    uint64_t name;
    size_t   owner_count;
} TbSymbolInheritResult_t;

typedef struct {
    TbSymbolReferenceTarget_t record;
    TbSymbolGraphContext_t    context;
    uint64_t                  name;
    size_t                    node_index;
} TbSymbolInheritNeed_t;

typedef struct {
    TbSymbolReferenceTarget_t record;
    TbSymbolAttributeResult_t selection;
    bool                     selection_available;
} TbSymbolInheritFailure_t;

/* Initialize before use; private working state. Borrows a completed graph,
 * caller-accounted workspace and pinned context until discard. A supplied source
 * tree is borrowed until the next SOURCE request or completion/discard.
 * No allocation/I/O/discovery, callbacks or recursion. Caller owns cancellation,
 * absolute deadlines and context revision checks. */
typedef struct {
    TbSymbolGraphView_t           graph;
    TbSymbolInheritSlot_t        *slots;
    const TbSymbolDieAttribute_t *attributes;
    TbSymbolAttributes_t          lookup;
    TbSymbolInheritStatus_e       status;
    TbSymbolInheritFailure_t      failure;
    uint64_t                      name;
    uint64_t                      progress;
    size_t                        initializing;
    size_t                        finding;
    size_t                        active;
    size_t                        scan;
    size_t                        owners;
    unsigned                      edge;
    unsigned                      phase;
    bool                          initialized;
} TbSymbolInherit_t;

/* name is explicitly limited to name, standard linkage name, type, object pointer
 * and the three declaration coordinates. Flags, sibling, runtime locations and all other
 * fields are unsupported. Workspace has at least graph.count accounted slots;
 * checked capacity failure and invalid initialization leave old state intact.
 * The graph must be an unchanged successful graph_view result. */
TbSymbolInheritStatus_e tb_symbol_inherit_init(TbSymbolInherit_t *inherit, TbSymbolGraphView_t graph,
                                               uint64_t name, TbSymbolInheritSlot_t *slots,
                                               size_t capacity);
/* At most 1..256 bounded initialization, attribute or graph-lookup actions. */
TbSymbolInheritStatus_e tb_symbol_inherit_step(TbSymbolInherit_t *inherit, size_t max_steps);
bool tb_symbol_inherit_need(const TbSymbolInherit_t *inherit, TbSymbolInheritNeed_t *need);
/* Explicit complete source for the requested node. Wrong supplied context keeps
 * the pending request unchanged; NULL finalizes an unavailable dependency. */
TbSymbolInheritStatus_e tb_symbol_inherit_source(TbSymbolInherit_t *inherit,
                                                 const TbSymbolUnitsView_t *units,
                                                 const TbSymbolDiesView_t *tree, uint64_t unit_index);
/* Available only after complete selection. A direct field stops inheritance on
 * that path; absent fields follow all three captured relationship kinds. Equal
 * physical owners reached by multiple paths occur once. Distinct physical
 * owners remain MULTIPLE even if their values might agree. No value-class, reference-target,
 * field-value agreement, type equivalence or semantic naming claim follows.
 * Every owner retains its own CU, never the selected root's declaration-file
 * numbering. The graph retains the independent derivation paths.
 * No successful candidate prefix is available on failure. */
bool tb_symbol_inherit_result(const TbSymbolInherit_t *inherit, TbSymbolInheritResult_t *result);
/* Select by graph node index, not by candidate ordinal. Final selection only;
 * returns false without changing output when the node is not an owner. */
bool tb_symbol_inherit_owner(const TbSymbolInherit_t *inherit, size_t node_index,
                              TbSymbolInheritOwner_t *owner);
bool tb_symbol_inherit_failure(const TbSymbolInherit_t *inherit, TbSymbolInheritFailure_t *failure);
uint64_t tb_symbol_inherit_progress(const TbSymbolInherit_t *inherit);

#endif // THE_BUGGER_SYMBOL_INHERIT_H
