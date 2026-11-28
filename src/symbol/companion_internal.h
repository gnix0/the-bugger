#ifndef THE_BUGGER_SYMBOL_COMPANION_INTERNAL_H
#define THE_BUGGER_SYMBOL_COMPANION_INTERNAL_H

#include "symbol/companion_load.h"
#include "symbol/companion_receipt.h"

typedef struct companion_node {
    void    *children[16];
    unsigned count;
} CompanionNode_t;

typedef struct companion_binding {
    struct companion_binding *previous;
    struct companion_binding *next;
    TbSymbolCompanions_t      *owner;
    TbSymbolCompanionFact_t   *primary;
    TbSymbolCompanionFact_t   *companion;
    TbSymbolCompanionLoad_t   *pending;
    uint64_t                  context;
    uint64_t                  identity;
} CompanionBinding_t;

struct tb_symbol_companions {
    TbSymbolArtifacts_t        *artifacts;
    TbSymbolCompanionIds_t     *identities;
    TbSymbolCompanionBudgets_t  budgets;
    TbSymbolCompanionLimits_t   limits;
    CompanionNode_t             bindings;
    CompanionBinding_t        *first;
    CompanionBinding_t        *cursor;
    size_t                     binding_count;
    size_t                     fact_count;
    size_t                     inspection_count;
    size_t                     scope_count;
    size_t                     load_count;
};

struct tb_symbol_companion_fact {
    TbSymbolCompanions_t          *owner;
    TbSymbolArtifactLease_t       *lease;
    TbSymbolArtifactCapture_t      capture;
    TbSymbolCompanionInventory_t   inventory;
    TbSymbolWorkIdentity_t         request;
    uint64_t                       process_generation;
    uint64_t                       identity;
    uint64_t                       references;
    size_t                         bytes;
    bool                           available;
    unsigned char                  data[];
};

struct tb_symbol_companion_inspection {
    TbSymbolCompanions_t               *owner;
    TbSymbolArtifactLease_t            *lease;
    TbSymbolCompanionInspectionPlan_t   plan;
    uint64_t                           identity;
    size_t                             bytes;
    size_t                             origin_bytes;
    char                               origin[];
};

struct tb_symbol_companion_scope {
    TbSymbolCompanions_t           *owner;
    TbSymbolArtifactLease_t        *primary;
    TbSymbolArtifactLease_t        *companion;
    TbSymbolCompanionFact_t        *primary_fact;
    TbSymbolCompanionFact_t        *companion_fact;
    TbSymbolCompanionScopeMember_t  members[2];
    size_t                         count;
};

bool tb_symbol_companion_same_work(TbSymbolWorkIdentity_t left, TbSymbolWorkIdentity_t right);
bool tb_symbol_companion_same_backing(TbSymbolStorageInfo_t left, TbSymbolStorageInfo_t right);
CompanionBinding_t *tb_symbol_companion_binding_find(const TbSymbolCompanions_t *owner, uint64_t context);
TbSymbolOutcome_e tb_symbol_companion_binding_create(TbSymbolCompanions_t *owner, uint64_t context,
    CompanionBinding_t **binding);
void tb_symbol_companion_binding_free(CompanionBinding_t *binding);
bool tb_symbol_companion_binding_current(const CompanionBinding_t *binding);

#endif // THE_BUGGER_SYMBOL_COMPANION_INTERNAL_H
