#define _POSIX_C_SOURCE 200809L
#include "companion_internal.h"

#include <stdlib.h>
#include <string.h>

bool tb_symbol_companion_same_work(TbSymbolWorkIdentity_t left, TbSymbolWorkIdentity_t right)
{
    return left.request == right.request && left.consumer == right.consumer &&
           left.revision == right.revision && left.generation == right.generation;
}


bool tb_symbol_companion_same_backing(TbSymbolStorageInfo_t left, TbSymbolStorageInfo_t right)
{
    return left.bytes == right.bytes && left.key.device == right.key.device &&
           left.key.inode == right.key.inode;
}


static unsigned digit(uint64_t context, unsigned depth)
{
    return (unsigned)((context >> ((15 - depth) * 4U)) & UINT64_C(15));
}


CompanionBinding_t *tb_symbol_companion_binding_find(const TbSymbolCompanions_t *owner, uint64_t context)
{
    if (!owner || !context)
        return NULL;

    const CompanionNode_t *node = &owner->bindings;
    for (unsigned depth = 0; depth < 15; ++depth) {
        node = node->children[digit(context, depth)];
        if (!node)
            return NULL;
    }

    return node->children[digit(context, 15)];
}


static void *allocate(TbSymbolBudget_t *budget, size_t bytes)
{
    if (!tb_symbol_budget_reserve(budget, bytes))
        return NULL;

    void *result = calloc(1, bytes);
    if (!result)
        (void)tb_symbol_budget_release(budget, bytes);

    return result;
}


static bool insert(TbSymbolCompanions_t *owner, CompanionBinding_t *binding)
{
    CompanionNode_t *nodes[16] = {&owner->bindings};
    unsigned         created = 16;
    for (unsigned depth = 0; depth < 15; ++depth) {
        unsigned index = digit(binding->context, depth);
        CompanionNode_t *parent = nodes[depth];
        if (!parent->children[index]) {
            CompanionNode_t *node = allocate(owner->budgets.frontend, sizeof(*node));
            if (!node) {
                /* Roll back only the suffix allocated by this insertion. */
                for (unsigned i = depth; i > created; --i) {
                    free(nodes[i]);
                    (void)tb_symbol_budget_release(owner->budgets.frontend, sizeof(CompanionNode_t));
                }
                if (created < 16) {
                    nodes[created]->children[digit(binding->context, created)] = NULL;
                    --nodes[created]->count;
                }
                return false;
            }
            if (created == 16)
                created = depth;
            parent->children[index] = node;
            ++parent->count;
        }
        nodes[depth + 1] = parent->children[index];
    }

    nodes[15]->children[digit(binding->context, 15)] = binding;
    ++nodes[15]->count;

    return true;
}


TbSymbolOutcome_e tb_symbol_companion_binding_create(TbSymbolCompanions_t *owner, uint64_t context,
    CompanionBinding_t **result)
{
    CompanionBinding_t *old = tb_symbol_companion_binding_find(owner, context);
    if (old)
        return old->companion ? TB_SYMBOL_UNSUPPORTED : TB_SYMBOL_RESOURCE_FAILURE;
    if (owner->binding_count == owner->limits.attachments || owner->identities->sequence == UINT64_MAX)
        return TB_SYMBOL_RESOURCE_FAILURE;

    CompanionBinding_t *binding = allocate(owner->budgets.frontend, sizeof(*binding));
    if (!binding)
        return TB_SYMBOL_RESOURCE_FAILURE;

    binding->owner    = owner;
    binding->context  = context;
    binding->identity = ++owner->identities->sequence;
    if (!insert(owner, binding)) {
        free(binding);
        (void)tb_symbol_budget_release(owner->budgets.frontend, sizeof(*binding));
        return TB_SYMBOL_RESOURCE_FAILURE;
    }

    binding->next = owner->first;
    if (owner->first)
        owner->first->previous = binding;
    owner->first = binding;
    ++owner->binding_count;
    *result = binding;

    return TB_SYMBOL_SUCCESS;
}


void tb_symbol_companion_binding_free(CompanionBinding_t *binding)
{
    TbSymbolCompanions_t *owner = binding->owner;
    CompanionNode_t *nodes[16] = {&owner->bindings};
    for (unsigned depth = 0; depth < 15; ++depth)
        nodes[depth + 1] = nodes[depth]->children[digit(binding->context, depth)];

    nodes[15]->children[digit(binding->context, 15)] = NULL;
    --nodes[15]->count;
    for (unsigned depth = 15; depth && !nodes[depth]->count; --depth) {
        free(nodes[depth]);
        (void)tb_symbol_budget_release(owner->budgets.frontend, sizeof(CompanionNode_t));
        nodes[depth - 1]->children[digit(binding->context, depth - 1)] = NULL;
        --nodes[depth - 1]->count;
    }

    if (owner->cursor == binding)
        owner->cursor = binding->next ? binding->next : owner->first;
    if (binding->previous)
        binding->previous->next = binding->next;
    else
        owner->first = binding->next;
    if (binding->next)
        binding->next->previous = binding->previous;
    if (owner->cursor == binding)
        owner->cursor = NULL;

    --owner->binding_count;
    free(binding);
    (void)tb_symbol_budget_release(owner->budgets.frontend, sizeof(CompanionBinding_t));
}


static bool contained(const TbSymbolBudget_t *budget, const TbSymbolBudget_t *frontend)
{
    for (unsigned i = 0; budget && i < 4; ++i, budget = budget->parent) {
        if (budget == frontend)
            return true;
    }

    return false;
}


TbSymbolCompanions_t *tb_symbol_companions_create(TbSymbolArtifacts_t *artifacts,
    TbSymbolCompanionIds_t *identities, TbSymbolCompanionBudgets_t budgets, TbSymbolCompanionLimits_t limits)
{
    if (!artifacts || !identities || !budgets.frontend || !budgets.evidence || !budgets.construction ||
        !contained(budgets.evidence, budgets.frontend) ||
        !contained(budgets.construction, budgets.frontend) ||
        !limits.facts || !limits.inspections || limits.inspections > TB_SYMBOL_MAX_JOBS ||
        !limits.attachments || !limits.scopes) {
        return NULL;
    }

    TbSymbolCompanions_t *owner = allocate(budgets.frontend, sizeof(*owner));
    if (!owner)
        return NULL;

    owner->artifacts  = artifacts;
    owner->identities = identities;
    owner->budgets    = budgets;
    owner->limits     = limits;

    return owner;
}


bool tb_symbol_companions_destroy(TbSymbolCompanions_t *owner)
{
    if (!owner)
        return true;
    if (owner->binding_count || owner->fact_count || owner->inspection_count || owner->scope_count ||
        owner->load_count) {
        return false;
    }

    TbSymbolBudget_t *budget = owner->budgets.frontend;
    free(owner);
    (void)tb_symbol_budget_release(budget, sizeof(TbSymbolCompanions_t));

    return true;
}


TbSymbolOutcome_e tb_symbol_companion_inspect_begin(TbSymbolCompanions_t *owner, uint64_t context,
    TbSymbolWorkIdentity_t request, uint64_t process_generation,
    TbSymbolCompanionInspectionPlan_t *plan, TbSymbolCompanionInspection_t **result)
{
    if (!owner || !plan || !result || !request.request || !request.consumer || !request.generation ||
        !process_generation) {
        return TB_SYMBOL_MALFORMED;
    }
    if (owner->inspection_count == owner->limits.inspections ||
        owner->fact_count >= owner->limits.facts ||
        owner->inspection_count >= owner->limits.facts - owner->fact_count ||
        owner->identities->sequence == UINT64_MAX) {
        return TB_SYMBOL_RESOURCE_FAILURE;
    }

    TbSymbolArtifactInfo_t info;
    if (!tb_symbol_artifact_info(owner->artifacts, context, &info) ||
        info.state != TB_SYMBOL_ARTIFACT_RETAINED) {
        return TB_SYMBOL_STALE;
    }

    size_t length = strnlen(info.origin, TB_SYMBOL_COMPANION_PATH_BYTES);
    if (length == TB_SYMBOL_COMPANION_PATH_BYTES)
        return TB_SYMBOL_RESOURCE_FAILURE;

    size_t bytes = sizeof(TbSymbolCompanionInspection_t) + length + 1;
    TbSymbolCompanionInspection_t *inspection = allocate(owner->budgets.construction, bytes);
    if (!inspection)
        return TB_SYMBOL_RESOURCE_FAILURE;
    inspection->lease = tb_symbol_artifact_lease(owner->artifacts, context);
    if (!inspection->lease) {
        free(inspection);
        (void)tb_symbol_budget_release(owner->budgets.construction, bytes);
        return TB_SYMBOL_RESOURCE_FAILURE;
    }

    inspection->owner                   = owner;
    inspection->identity                = ++owner->identities->sequence;
    inspection->bytes                   = bytes;
    inspection->origin_bytes            = length + 1;
    inspection->plan.request            = request;
    inspection->plan.process_generation = process_generation;
    (void)tb_symbol_artifact_lease_capture(inspection->lease, &inspection->plan.capture);
    memcpy(inspection->origin, info.origin, length + 1);
    ++owner->inspection_count;
    *plan = inspection->plan;
    *result = inspection;

    return TB_SYMBOL_SUCCESS;
}


static TbSymbolOutcome_e finish_fact(TbSymbolCompanionInspection_t *inspection,
    const void *payload, size_t length, TbSymbolCompanionFact_t **result)
{
    TbSymbolCompanionInventory_t inventory;
    if (!payload || !tb_symbol_companion_receipt_decode(payload, length, &inventory) ||
        !tb_symbol_companion_same_backing(inventory.backing, inspection->plan.capture.backing)) {
        return TB_SYMBOL_MALFORMED;
    }

    TbSymbolCompanions_t *owner = inspection->owner;
    size_t bytes = sizeof(TbSymbolCompanionFact_t) + inspection->origin_bytes + length;
    TbSymbolCompanionFact_t *fact = allocate(owner->budgets.evidence, bytes);
    if (!fact)
        return TB_SYMBOL_RESOURCE_FAILURE;

    fact->owner              = owner;
    fact->lease              = inspection->lease;
    fact->capture            = inspection->plan.capture;
    fact->request            = inspection->plan.request;
    fact->process_generation = inspection->plan.process_generation;
    fact->identity           = inspection->identity;
    fact->references         = 1;
    fact->bytes              = bytes;
    fact->available          = true;
    memcpy(fact->data, inspection->origin, inspection->origin_bytes);
    unsigned char *receipt = fact->data + inspection->origin_bytes;
    memcpy(receipt, payload, length);
    (void)tb_symbol_companion_receipt_decode(receipt, length, &fact->inventory);

    inspection->lease = NULL;
    ++owner->fact_count;
    *result = fact;

    return TB_SYMBOL_SUCCESS;
}


TbSymbolOutcome_e tb_symbol_companion_inspect_finish(TbSymbolCompanionInspection_t *inspection,
    TbSymbolWorkIdentity_t request, uint64_t process_generation, TbSymbolOutcome_e outcome,
    const void *payload, size_t length, TbSymbolCompanionFact_t **fact)
{
    if (!inspection || !tb_symbol_companion_same_work(request, inspection->plan.request) ||
        process_generation != inspection->plan.process_generation) {
        return TB_SYMBOL_STALE;
    }
    if (!fact || outcome <= TB_SYMBOL_PENDING || outcome > TB_SYMBOL_OPERATIONAL_FAILURE)
        return TB_SYMBOL_MALFORMED;

    if (!tb_symbol_artifact_lease_current(inspection->lease))
        outcome = TB_SYMBOL_STALE;
    if (outcome == TB_SYMBOL_SUCCESS)
        outcome = finish_fact(inspection, payload, length, fact);

    TbSymbolCompanions_t *owner = inspection->owner;
    size_t                bytes = inspection->bytes;
    tb_symbol_artifact_lease_release(inspection->lease);
    --owner->inspection_count;
    free(inspection);
    (void)tb_symbol_budget_release(owner->budgets.construction, bytes);

    return outcome;
}


bool tb_symbol_companion_fact_retain(TbSymbolCompanionFact_t *fact)
{
    if (!fact || fact->references == UINT64_MAX)
        return false;

    ++fact->references;

    return true;
}


void tb_symbol_companion_fact_release(TbSymbolCompanionFact_t *fact)
{
    if (!fact || --fact->references)
        return;

    TbSymbolCompanions_t *owner = fact->owner;
    size_t                bytes = fact->bytes;
    tb_symbol_artifact_lease_release(fact->lease);
    --owner->fact_count;
    free(fact);
    (void)tb_symbol_budget_release(owner->budgets.evidence, bytes);
}


bool tb_symbol_companion_fact_current(const TbSymbolCompanionFact_t *fact)
{
    return fact && fact->available && tb_symbol_artifact_lease_content_current(fact->lease);
}


bool tb_symbol_companion_fact_withdraw(TbSymbolCompanionFact_t *fact)
{
    if (!fact || !fact->available)
        return false;

    fact->available = false;

    return true;
}


bool tb_symbol_companion_fact_view(const TbSymbolCompanionFact_t *fact,
    TbSymbolArtifactCapture_t *capture, TbSymbolCompanionInventory_t *inventory, const char **origin)
{
    if (!capture || !inventory || !origin || !tb_symbol_companion_fact_current(fact))
        return false;

    *capture = fact->capture;
    *inventory = fact->inventory;
    *origin = (const char *)fact->data;

    return true;
}


bool tb_symbol_companion_binding_current(const CompanionBinding_t *binding)
{
    return binding && binding->companion && tb_symbol_companion_fact_current(binding->primary) &&
           tb_symbol_companion_fact_current(binding->companion);
}


TbSymbolOutcome_e tb_symbol_companion_scope_capture(TbSymbolCompanions_t *owner, uint64_t context,
    TbSymbolCompanionOrigin_e origin, TbSymbolCompanionScopeCapture_t **result)
{
    if (!owner || !result || origin < TB_SYMBOL_COMPANION_ORIGIN_ALL ||
        origin > TB_SYMBOL_COMPANION_ORIGIN_COMPANION) {
        return TB_SYMBOL_MALFORMED;
    }
    if (owner->scope_count == owner->limits.scopes)
        return TB_SYMBOL_RESOURCE_FAILURE;

    TbSymbolArtifactInfo_t info;
    if (!tb_symbol_artifact_info(owner->artifacts, context, &info) ||
        info.state != TB_SYMBOL_ARTIFACT_RETAINED) {
        return TB_SYMBOL_STALE;
    }
    CompanionBinding_t *binding = tb_symbol_companion_binding_find(owner, context);
    bool include = origin != TB_SYMBOL_COMPANION_ORIGIN_PRIMARY && binding && binding->companion;
    if (include && !tb_symbol_companion_binding_current(binding))
        return TB_SYMBOL_STALE;

    TbSymbolCompanionScopeCapture_t *scope = allocate(owner->budgets.frontend, sizeof(*scope));
    if (!scope)
        return TB_SYMBOL_RESOURCE_FAILURE;
    scope->owner = owner;
    ++owner->scope_count;
    scope->primary = tb_symbol_artifact_lease(owner->artifacts, context);
    if (!scope->primary) {
        tb_symbol_companion_scope_release(scope);
        return TB_SYMBOL_RESOURCE_FAILURE;
    }
    if (origin != TB_SYMBOL_COMPANION_ORIGIN_COMPANION) {
        scope->members[scope->count].origin = TB_SYMBOL_COMPANION_ORIGIN_PRIMARY;
        (void)tb_symbol_artifact_lease_capture(scope->primary, &scope->members[scope->count++].capture);
    }
    if (include) {
        scope->companion = tb_symbol_artifact_lease(owner->artifacts, binding->companion->capture.context);
        if (!scope->companion || !tb_symbol_companion_fact_retain(binding->primary)) {
            tb_symbol_companion_scope_release(scope);
            return TB_SYMBOL_RESOURCE_FAILURE;
        }
        scope->primary_fact = binding->primary;
        if (!tb_symbol_companion_fact_retain(binding->companion)) {
            tb_symbol_companion_scope_release(scope);
            return TB_SYMBOL_RESOURCE_FAILURE;
        }
        scope->companion_fact = binding->companion;
        TbSymbolCompanionScopeMember_t *member = &scope->members[scope->count++];
        member->origin = TB_SYMBOL_COMPANION_ORIGIN_COMPANION;
        member->relationship = binding->identity;
        (void)tb_symbol_artifact_lease_capture(scope->companion, &member->capture);
    }
    *result = scope;

    return TB_SYMBOL_SUCCESS;
}


bool tb_symbol_companion_scope_current(const TbSymbolCompanionScopeCapture_t *scope)
{
    return scope && tb_symbol_artifact_lease_current(scope->primary) &&
           (!scope->companion || (tb_symbol_artifact_lease_current(scope->companion) &&
            tb_symbol_companion_fact_current(scope->primary_fact) &&
            tb_symbol_companion_fact_current(scope->companion_fact)));
}


size_t tb_symbol_companion_scope_count(const TbSymbolCompanionScopeCapture_t *scope)
{
    return scope ? scope->count : 0;
}


bool tb_symbol_companion_scope_member(const TbSymbolCompanionScopeCapture_t *scope, size_t index,
    TbSymbolCompanionScopeMember_t *member)
{
    if (!member || !scope || index >= scope->count || !tb_symbol_companion_scope_current(scope))
        return false;

    *member = scope->members[index];

    return true;
}


void tb_symbol_companion_scope_release(TbSymbolCompanionScopeCapture_t *scope)
{
    if (!scope)
        return;

    TbSymbolCompanions_t *owner = scope->owner;
    tb_symbol_artifact_lease_release(scope->primary);
    tb_symbol_artifact_lease_release(scope->companion);
    tb_symbol_companion_fact_release(scope->primary_fact);
    tb_symbol_companion_fact_release(scope->companion_fact);
    --owner->scope_count;
    free(scope);
    (void)tb_symbol_budget_release(owner->budgets.frontend, sizeof(TbSymbolCompanionScopeCapture_t));
}


TbSymbolOutcome_e tb_symbol_companion_detach(TbSymbolCompanions_t *owner, uint64_t context,
    TbSymbolCompanionRetired_t *retired)
{
    if (!owner || !retired)
        return TB_SYMBOL_MALFORMED;

    CompanionBinding_t *binding = tb_symbol_companion_binding_find(owner, context);
    if (!binding || !binding->companion)
        return TB_SYMBOL_STALE;
    TbSymbolArtifactInfo_t child;
    bool open = tb_symbol_artifact_info(owner->artifacts, binding->companion->capture.context, &child);
    if (open && child.state != TB_SYMBOL_ARTIFACT_RETAINED && child.state != TB_SYMBOL_ARTIFACT_UNAVAILABLE)
        return TB_SYMBOL_OPERATIONAL_FAILURE;

    TbSymbolArtifactInfo_t primary;
    if (tb_symbol_artifact_info(owner->artifacts, context, &primary) &&
        primary.state == TB_SYMBOL_ARTIFACT_RETAINED &&
        !tb_symbol_artifact_revise(owner->artifacts, context, primary.revision)) {
        return TB_SYMBOL_RESOURCE_FAILURE;
    }
    if (open && !tb_symbol_artifact_close(owner->artifacts, child.context))
        return TB_SYMBOL_OPERATIONAL_FAILURE;

    *retired = (TbSymbolCompanionRetired_t) {
        .relationship = binding->identity,
        .context      = binding->companion->capture.context,
        .content      = binding->companion->capture.content
    };
    tb_symbol_companion_fact_release(binding->primary);
    tb_symbol_companion_fact_release(binding->companion);
    tb_symbol_companion_binding_free(binding);

    return TB_SYMBOL_SUCCESS;
}


TbSymbolOutcome_e tb_symbol_companion_collect(TbSymbolCompanions_t *owner,
    TbSymbolCompanionRetired_t *retired, bool *found)
{
    if (!owner || !retired || !found)
        return TB_SYMBOL_MALFORMED;

    CompanionBinding_t *binding = owner->cursor ? owner->cursor : owner->first;
    if (binding)
        owner->cursor = binding->next ? binding->next : owner->first;
    if (!binding || !binding->companion || tb_symbol_companion_binding_current(binding)) {
        *found = false;
        return TB_SYMBOL_SUCCESS;
    }

    TbSymbolOutcome_e outcome = tb_symbol_companion_detach(owner, binding->context, retired);
    if (outcome == TB_SYMBOL_SUCCESS)
        *found = true;

    return outcome;
}
