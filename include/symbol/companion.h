#ifndef THE_BUGGER_SYMBOL_COMPANION_H
#define THE_BUGGER_SYMBOL_COMPANION_H

#include "symbol/artifact.h"
#include "symbol/companion_scan.h"

typedef struct tb_symbol_companions TbSymbolCompanions_t;
typedef struct tb_symbol_companion_fact TbSymbolCompanionFact_t;
typedef struct tb_symbol_companion_inspection TbSymbolCompanionInspection_t;
typedef struct tb_symbol_companion_scope TbSymbolCompanionScopeCapture_t;

typedef struct {
    uint64_t sequence;
} TbSymbolCompanionIds_t;

typedef struct {
    TbSymbolBudget_t *frontend;
    TbSymbolBudget_t *evidence;
    TbSymbolBudget_t *construction;
} TbSymbolCompanionBudgets_t;

typedef struct {
    size_t facts;
    size_t inspections;
    size_t attachments;
    size_t scopes;
} TbSymbolCompanionLimits_t;

/* Required explicit positive capacities; no production defaults before SE10.
 * Evidence/construction accounts must be contained in frontend and follow the
 * engine's subsidiary ceilings. All accounts, artifact registry and identity
 * domain outlive this owner. Create/destroy perform no file or helper operation.
 * One explicitly selected GNU companion per primary; no silent replacement or
 * recursive inventory. Other primaries and independently opened contexts remain
 * independent. Repeated attach requires explicit detach first. */
TbSymbolCompanions_t *tb_symbol_companions_create(TbSymbolArtifacts_t *artifacts,
    TbSymbolCompanionIds_t *identities, TbSymbolCompanionBudgets_t budgets, TbSymbolCompanionLimits_t limits);
bool tb_symbol_companions_destroy(TbSymbolCompanions_t *companions);

typedef struct {
    TbSymbolArtifactCapture_t capture;
    TbSymbolWorkIdentity_t    request;
    uint64_t                  process_generation;
} TbSymbolCompanionInspectionPlan_t;

/* Reserves future fact capacity and pins exact current backing before dispatch.
 * Caller builds COMPANION_INSPECT for this plan's actual slot/key, never context
 * as slot. Origin is copied before a possible Close. Failure preserves output. */
TbSymbolOutcome_e tb_symbol_companion_inspect_begin(TbSymbolCompanions_t *companions, uint64_t context,
    TbSymbolWorkIdentity_t request, uint64_t process_generation,
    TbSymbolCompanionInspectionPlan_t *plan, TbSymbolCompanionInspection_t **inspection);
/* Call only after exact helper release/reap proof, or confirmed nonadmission.
 * Wrong identity/generation leaves ticket owned. A matching final outcome consumes
 * it, including cancellation/stale/malformed/resource failure. Payload is a
 * complete validated helper frame with no descriptors; receipt bytes are borrowed
 * through this call. No prefix fact. Evidence outlives helper/cache lifetimes. */
TbSymbolOutcome_e tb_symbol_companion_inspect_finish(TbSymbolCompanionInspection_t *inspection,
    TbSymbolWorkIdentity_t request, uint64_t process_generation, TbSymbolOutcome_e outcome,
    const void *payload, size_t length, TbSymbolCompanionFact_t **fact);

bool tb_symbol_companion_fact_retain(TbSymbolCompanionFact_t *fact);
void tb_symbol_companion_fact_release(TbSymbolCompanionFact_t *fact);
bool tb_symbol_companion_fact_current(const TbSymbolCompanionFact_t *fact);
/* Scoped metadata withdrawal, distinct from backing eviction. Existing captures
 * and relationships become unavailable immediately. Never reactivates a fact. */
bool tb_symbol_companion_fact_withdraw(TbSymbolCompanionFact_t *fact);
/* Borrowed views through fact release; recheck current before each use. Origin
 * and bytes remain frontend-owned, not paths to reopen or runtime authority. */
bool tb_symbol_companion_fact_view(const TbSymbolCompanionFact_t *fact,
    TbSymbolArtifactCapture_t *capture, TbSymbolCompanionInventory_t *inventory, const char **origin);

typedef enum {
    TB_SYMBOL_COMPANION_ORIGIN_ALL,
    TB_SYMBOL_COMPANION_ORIGIN_PRIMARY,
    TB_SYMBOL_COMPANION_ORIGIN_COMPANION
} TbSymbolCompanionOrigin_e;

typedef struct {
    TbSymbolArtifactCapture_t capture;
    uint64_t                  relationship;
    TbSymbolCompanionOrigin_e  origin;
} TbSymbolCompanionScopeMember_t;

/* Captures current inventory atomically: primary + companion by default, or
 * explicit provenance narrowing. Companion-only without an attachment is a
 * valid empty captured inventory. It still captures the parent's membership
 * revision. No discovery/admission or metadata validation is initiated.
 * Invalid attached evidence fails capture rather than silently narrowing it. */
TbSymbolOutcome_e tb_symbol_companion_scope_capture(TbSymbolCompanions_t *companions, uint64_t primary,
    TbSymbolCompanionOrigin_e origin, TbSymbolCompanionScopeCapture_t **scope);
bool tb_symbol_companion_scope_current(const TbSymbolCompanionScopeCapture_t *scope);
size_t tb_symbol_companion_scope_count(const TbSymbolCompanionScopeCapture_t *scope);
bool tb_symbol_companion_scope_member(const TbSymbolCompanionScopeCapture_t *scope, size_t index,
    TbSymbolCompanionScopeMember_t *member);
void tb_symbol_companion_scope_release(TbSymbolCompanionScopeCapture_t *scope);

typedef struct {
    uint64_t relationship;
    uint64_t context;
    uint64_t content;
} TbSymbolCompanionRetired_t;

/* Detach advances the available primary's membership revision before removing
 * the relationship. Closes only its independently owned companion context;
 * ordinary primary/artifact contexts are untouched. Returns the exact retired
 * content to the existing artifact-cache owner for later explicit eviction and
 * supervised cleanup. Leases can delay eviction; backing stays charged. */
TbSymbolOutcome_e tb_symbol_companion_detach(TbSymbolCompanions_t *companions, uint64_t primary,
    TbSymbolCompanionRetired_t *retired);
/* Visits at most one binding per call. Invalid facts/closed or withdrawn contexts
 * already fail current checks; collection reclaims their ownership separately.
 * SUCCESS with found=false is an empty collection step, not a failure. Preserve
 * returned cache obligations before another step. No implicit restart/retry/I/O. */
TbSymbolOutcome_e tb_symbol_companion_collect(TbSymbolCompanions_t *companions,
    TbSymbolCompanionRetired_t *retired, bool *found);

#endif // THE_BUGGER_SYMBOL_COMPANION_H
