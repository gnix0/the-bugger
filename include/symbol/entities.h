#ifndef THE_BUGGER_SYMBOL_ENTITIES_H
#define THE_BUGGER_SYMBOL_ENTITIES_H

#include "symbol/attributes.h"

typedef enum {
    TB_SYMBOL_ENTITY_OTHER,
    TB_SYMBOL_ENTITY_FUNCTION,
    TB_SYMBOL_ENTITY_DATA,
    TB_SYMBOL_ENTITY_INLINE_INSTANCE
} TbSymbolEntityKind_e;

typedef enum {
    TB_SYMBOL_ROLE_OTHER,
    TB_SYMBOL_ROLE_DEFINITION,
    TB_SYMBOL_ROLE_DECLARATION,
    TB_SYMBOL_ROLE_INLINE_INSTANCE
} TbSymbolEntityRole_e;

typedef enum {
    TB_SYMBOL_ENTITY_NAME,
    TB_SYMBOL_ENTITY_LINKAGE,
    TB_SYMBOL_ENTITY_DECLARATION,
    TB_SYMBOL_ENTITY_EXTERNAL,
    TB_SYMBOL_ENTITY_ARTIFICIAL,
    TB_SYMBOL_ENTITY_INLINE,
    TB_SYMBOL_ENTITY_SPECIFICATION,
    TB_SYMBOL_ENTITY_ORIGIN,
    TB_SYMBOL_ENTITY_TYPE,
    TB_SYMBOL_ENTITY_FIELDS
} TbSymbolEntityField_e;

typedef enum {
    TB_SYMBOL_ENTITY_PENDING,
    TB_SYMBOL_ENTITY_READY,
    TB_SYMBOL_ENTITY_CONFLICT,
    TB_SYMBOL_ENTITY_MALFORMED,
    TB_SYMBOL_ENTITY_INVALID
} TbSymbolEntityStatus_e;

/* attribute is meaningful only for exactly one physical occurrence. */
typedef struct {
    TbSymbolAttributeResult_t selection;
    TbSymbolDieAttribute_t    attribute;
} TbSymbolEntityField_t;

typedef struct {
    bool present;
    bool value;
} TbSymbolEntityFlag_t;

typedef struct {
    TbSymbolStorageInfo_t backing;
    uint64_t             section_index;
    uint64_t             unit_offset;
    uint64_t             record_offset;
    uint64_t             parent_index;
    uint64_t             tag;
    uint64_t             abbrev_section;
    uint64_t             abbrev_offset;
    TbSymbolEntityKind_e  kind;
    TbSymbolEntityField_t fields[TB_SYMBOL_ENTITY_FIELDS];
    TbSymbolEntityFlag_t  declaration;
    TbSymbolEntityFlag_t  external;
    TbSymbolEntityFlag_t  artificial;
} TbSymbolEntityView_t;

/* Initialize before use; do not modify fields directly. Embedded state is
 * accounted/owned by the caller; the complete tree and its context remain
 * borrowed until completion/discard. Failed initialization leaves state intact. */
typedef struct {
    const TbSymbolDieAttribute_t *attributes;
    uint64_t                     cursor;
    uint64_t                     end;
    TbSymbolEntityView_t          view;
    TbSymbolEntityStatus_e        status;
    TbSymbolEntityField_e         failure_field;
    bool                         initialized;
} TbSymbolEntity_t;

TbSymbolEntityStatus_e tb_symbol_entity_init(TbSymbolEntity_t *entity, TbSymbolDiesView_t tree,
                                             uint64_t die_index);
/* Inspect 1..256 attributes, then validate selected direct fields. Every known
 * field must be unique even when duplicate values agree. No I/O or allocation.
 * Caller supplies cancellation, deadlines and captured-context checks. */
TbSymbolEntityStatus_e tb_symbol_entity_step(TbSymbolEntity_t *entity, size_t max_steps);
/* READY only. These are direct physical facts, without inheritance or reference
 * target validation. Only the three flag fields receive class/value validation;
 * other selected fields retain raw attributes for their separate interpreters.
 * Flag present records physical attribute presence, including
 * an explicit false value; missing flags stay absent. Kind follows the tag:
 * FUNCTION does not prove an emitted standalone code location; DATA does not
 * prove nonlocality, storage, a runtime address or access authority. OTHER is
 * neither a new search category nor proof of unsupported artifact coverage. */
bool tb_symbol_entity_view(const TbSymbolEntity_t *entity, TbSymbolEntityView_t *view);
/* Complete selected-field diagnostics only on CONFLICT/MALFORMED. */
bool tb_symbol_entity_failure(const TbSymbolEntity_t *entity, TbSymbolEntityField_e *field,
                              TbSymbolAttributeResult_t *selection);

/* Complete direct metadata only. Declaration is never inherited across a
 * specification/origin reference. DEFINITION grants neither executable/storage
 * existence nor location/action authority; inline instances remain separate. */
bool tb_symbol_entity_role(const TbSymbolEntity_t *entity, TbSymbolEntityRole_e *role);

#endif // THE_BUGGER_SYMBOL_ENTITIES_H
