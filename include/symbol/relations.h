#ifndef THE_BUGGER_SYMBOL_RELATIONS_H
#define THE_BUGGER_SYMBOL_RELATIONS_H

#include "symbol/attributes.h"
#include "symbol/references.h"

typedef enum {
    TB_SYMBOL_RELATION_SPECIFICATION,
    TB_SYMBOL_RELATION_ORIGIN,
    TB_SYMBOL_RELATION_EXTENSION
} TbSymbolRelationKind_e;

typedef enum {
    TB_SYMBOL_RELATION_PENDING,
    TB_SYMBOL_RELATION_READY,
    TB_SYMBOL_RELATION_ABSENT,
    TB_SYMBOL_RELATION_CONFLICT,
    TB_SYMBOL_RELATION_MALFORMED,
    TB_SYMBOL_RELATION_INVALID,
    TB_SYMBOL_RELATION_DEPENDENCY,
    TB_SYMBOL_RELATION_UNSUPPORTED,
    TB_SYMBOL_RELATION_RANGE,
    TB_SYMBOL_RELATION_MISSING_TARGET,
    TB_SYMBOL_RELATION_TAG_MISMATCH,
    TB_SYMBOL_RELATION_SELF_CYCLE,
    TB_SYMBOL_RELATION_FOUND
} TbSymbolRelationStatus_e;

typedef struct {
    uint64_t               unit_index;
    uint64_t               die_index;
    TbSymbolRelationKind_e kind;
} TbSymbolRelationSelection_t;

typedef struct {
    TbSymbolRelationKind_e     kind;
    uint64_t                  source_tag;
    uint64_t                  source_unit_index;
    uint16_t                  source_version;
    TbSymbolDwarfUnitKind_e    source_unit_kind;
    uint64_t                  abbrev_section;
    uint64_t                  abbrev_offset;
    TbSymbolAttributeResult_t attribute;
    TbSymbolDieAttribute_t    field;
    TbSymbolReference_t       reference;
} TbSymbolRelationSource_t;

typedef struct {
    TbSymbolRelationSource_t source;
    TbSymbolReferenceNeed_t  target;
    uint16_t                target_version;
} TbSymbolRelationNeed_t;

typedef struct {
    TbSymbolRelationSource_t  source;
    TbSymbolReferenceTarget_t target;
    uint64_t                 target_tag;
    uint16_t                 target_version;
    TbSymbolDwarfUnitKind_e   target_unit_kind;
    uint64_t                 target_abbrev_section;
    uint64_t                 target_abbrev_offset;
    uint64_t                 target_parent_record;
    uint64_t                 target_parent_tag;
} TbSymbolRelationEdge_t;

/* Initialize before use; do not modify fields directly. Borrows complete views
 * and their context until completion/discard. Caller accounts embedded storage,
 * cancellation, deadlines and captured revisions. No allocation or I/O. */
typedef struct {
    TbSymbolUnitsView_t         units;
    TbSymbolDiesView_t          tree;
    TbSymbolRelationSelection_t selection;
    TbSymbolAttributes_t        lookup;
    TbSymbolRelationSource_t    source;
    TbSymbolRelationStatus_e    status;
    TbSymbolReferenceStatus_e   reference_status;
    bool                       initialized;
} TbSymbolRelation_t;

TbSymbolRelationStatus_e tb_symbol_relation_init(TbSymbolRelation_t *relation, TbSymbolUnitsView_t units,
                                                 TbSymbolDiesView_t tree,
                                                 TbSymbolRelationSelection_t selection);
/* Full selected attribute scope, 1..256 attributes per call. Equal duplicates
 * conflict. READY describes the raw source reference, not its target. */
TbSymbolRelationStatus_e tb_symbol_relation_step(TbSymbolRelation_t *relation, size_t max_steps);
bool tb_symbol_relation_source(const TbSymbolRelation_t *relation, TbSymbolRelationSource_t *source);
/* Final query diagnostics only; output unchanged for PENDING/INVALID.
 * Reference status is meaningful only for a unique attribute. */
bool tb_symbol_relation_diagnostic(const TbSymbolRelation_t *relation, TbSymbolAttributeResult_t *attribute,
                                   TbSymbolReferenceStatus_e *reference);
/* Source must come from a completed query. NULL info explicitly means missing
 * dependency for ref_addr. A complete selected empty inventory is not absence.
 * READY selects a required unit. Supplementary/signature links and vendor or
 * unrecognized tags have unsupported semantics in this profile. No implicit
 * inventory selection, discovery or decode. */
TbSymbolRelationStatus_e tb_symbol_relation_need(TbSymbolRelationSource_t source,
                                                 const TbSymbolUnitsView_t *info,
                                                 TbSymbolRelationNeed_t *need);
/* Need must come from relation_need. NULL tree means an unavailable dependency.
 * FOUND proves an exact DIE and this profile's tag pair: specification tags match
 * or a variable refers to a DWARF 4 member inside a class/structure/union;
 * abstract origin also permits inlined_subroutine -> subprogram; namespace
 * extensions require namespace -> namespace. Declaration flags are not inherited
 * or required on the target. A self edge is reported separately. Other cycles,
 * inheritance, applicable relocation correspondence and semantic equivalence
 * require their own validation; no canonical identity or action authority follows.
 * All failed outputs remain unchanged. */
TbSymbolRelationStatus_e tb_symbol_relation_target(TbSymbolRelationNeed_t need,
                                                   const TbSymbolDiesView_t *tree,
                                                   TbSymbolRelationEdge_t *edge);

#endif // THE_BUGGER_SYMBOL_RELATIONS_H
