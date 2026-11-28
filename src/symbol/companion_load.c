#define _POSIX_C_SOURCE 200809L
#include "companion_internal.h"
#include "symbol/protocol.h"

#include <stdlib.h>
#include <string.h>
#include <unistd.h>

struct tb_symbol_companion_load {
    TbSymbolCompanions_t             *owner;
    CompanionBinding_t              *binding;
    TbSymbolCompanionFact_t          *primary;
    TbSymbolCompanionFact_t          *candidate_fact;
    TbSymbolArtifactLease_t          *primary_lease;
    TbSymbolCompanionInspection_t    *inspection;
    TbSymbolCompanionInspectionPlan_t inspection_plan;
    TbSymbolCompanionStorage_t        storage;
    TbSymbolCompanionMatch_e          method;
    TbSymbolCompanionLoadInfo_t       info;
    TbSymbolArtifactCleanup_t         cleanup;
    TbSymbolAcquireOffer_t            offer;
    TbSymbolWorkIdentity_t            request;
    uint64_t                         process_generation;
    uint64_t                         last_request;
    uint64_t                         candidate_content;
    size_t                           bytes;
    size_t                           origin_bytes;
    size_t                           storage_bytes;
    bool                             offered;
    bool                             published;
    bool                             cleanup_prepared;
    char                             paths[];
};

static bool current(const TbSymbolCompanionLoad_t *load)
{
    return tb_symbol_companion_fact_current(load->primary) &&
           tb_symbol_artifact_lease_current(load->primary_lease);
}


static void error(TbSymbolCompanionLoad_t *load, TbSymbolOutcome_e outcome)
{
    if (load->info.outcome == TB_SYMBOL_PENDING)
        load->info.outcome = outcome;
}


static void blocked(TbSymbolCompanionLoad_t *load, TbSymbolOutcome_e outcome)
{
    load->info.phase = TB_SYMBOL_COMPANION_LOAD_BLOCKED;
    load->info.cleanup_outcome = outcome;
}


static void cleanup_begin(TbSymbolCompanionLoad_t *load)
{
    if (load->info.outstanding) {
        load->info.cancel_required = true;
        return;
    }

    tb_symbol_companion_fact_release(load->candidate_fact);
    load->candidate_fact = NULL;
    tb_symbol_companion_fact_release(load->primary);
    load->primary = NULL;
    tb_symbol_artifact_lease_release(load->primary_lease);
    load->primary_lease = NULL;
    load->info.cancel_required = false;

    if (!load->cleanup_prepared) {
        if (load->published) {
            TbSymbolArtifactInfo_t info;
            if (tb_symbol_artifact_info(load->owner->artifacts, load->info.candidate, &info) &&
                !tb_symbol_artifact_close(load->owner->artifacts, load->info.candidate)) {
                blocked(load, TB_SYMBOL_OPERATIONAL_FAILURE);
                return;
            }
            if (!tb_symbol_artifact_evict(load->owner->artifacts, load->candidate_content, &load->cleanup)) {
                blocked(load, TB_SYMBOL_RESOURCE_FAILURE);
                return;
            }
        } else if (!tb_symbol_artifact_abandon(load->owner->artifacts, load->info.candidate)) {
            blocked(load, TB_SYMBOL_OPERATIONAL_FAILURE);
            return;
        }
        load->cleanup_prepared = true;
    }
    load->info.phase = TB_SYMBOL_COMPANION_LOAD_CLEANUP;
}


static bool usable_path(TbSymbolSourcePathText_t path)
{
    if (!path.data || path.bytes < 2 || path.bytes >= TB_SYMBOL_COMPANION_PATH_BYTES || path.data[0] != '/')
        return false;

    for (size_t i = 0; i < path.bytes; ++i) {
        if (!path.data[i] || path.data[i] == '\r' || path.data[i] == '\n')
            return false;
    }

    return true;
}


TbSymbolOutcome_e tb_symbol_companion_load_begin(TbSymbolCompanions_t *owner,
    TbSymbolCompanionFact_t *primary, TbSymbolSourcePathText_t candidate, TbSymbolCompanionMatch_e method,
    TbSymbolCompanionStorage_t storage, TbSymbolCompanionLoad_t **result)
{
    if (!owner || !primary || primary->owner != owner || !result || !storage.path ||
        !storage.file_bytes || storage.file_bytes > INT64_MAX ||
        (method != TB_SYMBOL_COMPANION_BY_DEBUGLINK && method != TB_SYMBOL_COMPANION_BY_BUILD_ID)) {
        return TB_SYMBOL_MALFORMED;
    }
    if (!usable_path(candidate))
        return TB_SYMBOL_UNSUPPORTED;

    size_t storage_length = strnlen(storage.path, TB_SYMBOL_COMPANION_PATH_BYTES);
    if (storage_length == TB_SYMBOL_COMPANION_PATH_BYTES)
        return TB_SYMBOL_RESOURCE_FAILURE;

    if (!usable_path((TbSymbolSourcePathText_t) {
        .data  = (const unsigned char *)storage.path,
        .bytes = storage_length
    })) {
        return TB_SYMBOL_MALFORMED;
    }
    if (!tb_symbol_companion_fact_current(primary))
        return TB_SYMBOL_STALE;

    size_t bytes = sizeof(TbSymbolCompanionLoad_t) + candidate.bytes + 1 + storage_length + 1;
    if (!tb_symbol_budget_reserve(owner->budgets.construction, bytes))
        return TB_SYMBOL_RESOURCE_FAILURE;
    TbSymbolCompanionLoad_t *load = calloc(1, bytes);
    if (!load) {
        (void)tb_symbol_budget_release(owner->budgets.construction, bytes);
        return TB_SYMBOL_RESOURCE_FAILURE;
    }

    TbSymbolOutcome_e outcome = TB_SYMBOL_RESOURCE_FAILURE;
    load->owner = owner;
    load->primary_lease = tb_symbol_artifact_lease(owner->artifacts, primary->capture.context);
    if (!load->primary_lease || !tb_symbol_companion_fact_retain(primary))
        goto failed;
    load->primary = primary;
    outcome = tb_symbol_companion_binding_create(owner, primary->capture.context, &load->binding);
    if (outcome != TB_SYMBOL_SUCCESS)
        goto failed;

    memcpy(load->paths, candidate.data, candidate.bytes);
    load->paths[candidate.bytes] = 0;
    load->origin_bytes = candidate.bytes + 1;
    memcpy(load->paths + load->origin_bytes, storage.path, storage_length + 1);
    load->storage_bytes = storage_length + 1;
    load->storage = storage;
    load->storage.path = load->paths + load->origin_bytes;
    load->method = method;
    load->bytes = bytes;
    load->info.relation = TB_SYMBOL_COMPANION_RELATION_UNEXAMINED;
    load->info.primary = primary->capture.context;
    load->info.candidate = tb_symbol_artifact_prepare(owner->artifacts, load->paths);
    if (!load->info.candidate) {
        outcome = TB_SYMBOL_RESOURCE_FAILURE;
        goto failed;
    }

    load->binding->pending = load;
    ++owner->load_count;
    *result = load;

    return TB_SYMBOL_SUCCESS;

failed:
    if (load->binding)
        tb_symbol_companion_binding_free(load->binding);
    tb_symbol_companion_fact_release(load->primary);
    tb_symbol_artifact_lease_release(load->primary_lease);
    free(load);
    (void)tb_symbol_budget_release(owner->budgets.construction, bytes);

    return outcome;
}


static bool identity_matches(const TbSymbolCompanionLoad_t *load, TbSymbolWorkIdentity_t identity,
    uint64_t process_generation)
{
    return load && load->info.outstanding && process_generation == load->process_generation &&
           tb_symbol_companion_same_work(identity, load->request);
}


static size_t encode(void *arguments, const uint64_t *words, size_t word_count,
    const char *first, size_t first_bytes, const char *second, size_t second_bytes)
{
    unsigned char *output = arguments;
    size_t cursor = 0;
    for (size_t i = 0; i < word_count; ++i) {
        tb_symbol_protocol_put(output + cursor, words[i], 8);
        cursor += 8;
    }
    tb_symbol_protocol_put(output + cursor, first_bytes, 4);
    cursor += 4;
    if (second) {
        tb_symbol_protocol_put(output + cursor, second_bytes, 4);
        cursor += 4;
    }
    memcpy(output + cursor, first, first_bytes);
    cursor += first_bytes;
    if (second) {
        memcpy(output + cursor, second, second_bytes);
        cursor += second_bytes;
    }

    return cursor;
}


TbSymbolOutcome_e tb_symbol_companion_load_request(TbSymbolCompanionLoad_t *load,
    TbSymbolWorkIdentity_t identity, uint64_t process_generation, void *arguments, size_t capacity,
    TbSymbolHelperRequest_t *request)
{
    if (!load || !arguments || !request || !identity.request || !identity.consumer || !identity.generation ||
        !process_generation || identity.request <= load->last_request) {
        return TB_SYMBOL_MALFORMED;
    }
    if (load->info.outstanding)
        return TB_SYMBOL_RESOURCE_FAILURE;
    if (load->info.phase == TB_SYMBOL_COMPANION_LOAD_DONE)
        return TB_SYMBOL_STALE;
    if (load->info.phase == TB_SYMBOL_COMPANION_LOAD_BLOCKED)
        return TB_SYMBOL_UNSUPPORTED;

    if (load->info.phase != TB_SYMBOL_COMPANION_LOAD_CLEANUP && !current(load)) {
        error(load, TB_SYMBOL_STALE);
        cleanup_begin(load);
    }
    if (load->info.phase == TB_SYMBOL_COMPANION_LOAD_READY ||
        load->info.phase == TB_SYMBOL_COMPANION_LOAD_BLOCKED) {
        return TB_SYMBOL_UNSUPPORTED;
    }

    uint64_t words[8];
    size_t   count;
    uint32_t operation;
    if (load->info.phase == TB_SYMBOL_COMPANION_LOAD_ACQUIRE) {
        count = 4;
        operation = TB_SYMBOL_OPERATION_ACQUIRE;
    } else if (load->info.phase == TB_SYMBOL_COMPANION_LOAD_INSPECT) {
        count = 8;
        operation = TB_SYMBOL_OPERATION_COMPANION_INSPECT;
    } else {
        count = load->published || load->offered ? 5 : 3;
        operation = count == 5 ? TB_SYMBOL_OPERATION_REMOVE_FILE : TB_SYMBOL_OPERATION_DISCARD_EMPTY;
    }
    size_t needed = count * 8 + 4 + load->storage_bytes;
    if (operation == TB_SYMBOL_OPERATION_ACQUIRE)
        needed += 4 + load->origin_bytes;
    if (capacity < needed)
        return TB_SYMBOL_RESOURCE_FAILURE;

    if (operation == TB_SYMBOL_OPERATION_ACQUIRE) {
        words[0] = load->storage.file_bytes;
        words[1] = load->info.candidate;
        words[2] = load->storage.key.device;
        words[3] = load->storage.key.inode;
    } else if (operation == TB_SYMBOL_OPERATION_COMPANION_INSPECT) {
        TbSymbolOutcome_e outcome = tb_symbol_companion_inspect_begin(load->owner, load->info.candidate,
            identity, process_generation, &load->inspection_plan, &load->inspection);
        if (outcome != TB_SYMBOL_SUCCESS) {
            error(load, outcome);
            cleanup_begin(load);
            return outcome;
        }
        TbSymbolArtifactCapture_t capture = load->inspection_plan.capture;
        words[0] = capture.backing.bytes;
        words[1] = capture.slot;
        words[2] = load->storage.key.device;
        words[3] = load->storage.key.inode;
        words[4] = capture.backing.key.device;
        words[5] = capture.backing.key.inode;
        words[6] = TB_SYMBOL_COMPANION_PATH_BYTES;
        words[7] = TB_SYMBOL_COMPANION_ID_BYTES;
    } else {
        words[0] = load->published ? load->cleanup.slot : load->info.candidate;
        words[1] = load->storage.key.device;
        words[2] = load->storage.key.inode;
        if (count == 5) {
            TbSymbolStorageKey_t key = load->published ? load->cleanup.key : load->offer.key;
            words[3] = key.device;
            words[4] = key.inode;
        }
    }

    bool acquiring = operation == TB_SYMBOL_OPERATION_ACQUIRE;
    size_t length = encode(arguments, words, count,
        acquiring ? load->paths : load->storage.path,
        acquiring ? load->origin_bytes : load->storage_bytes,
        acquiring ? load->storage.path : NULL, acquiring ? load->storage_bytes : 0);
    *request = (TbSymbolHelperRequest_t) {
        .identity  = identity,
        .operation = operation,
        .priority  = TB_SYMBOL_CONSTRUCTION,
        .heavy     = true,
        .arguments = arguments,
        .length    = length
    };
    load->request = identity;
    load->process_generation = process_generation;
    load->last_request = identity.request;
    load->info.outstanding = true;

    return TB_SYMBOL_SUCCESS;
}


TbSymbolOutcome_e tb_symbol_companion_load_offer(TbSymbolCompanionLoad_t *load,
    TbSymbolWorkIdentity_t identity, uint64_t process_generation, TbSymbolAcquireOffer_t offer)
{
    if (!identity_matches(load, identity, process_generation) ||
        load->info.phase != TB_SYMBOL_COMPANION_LOAD_ACQUIRE || load->offered) {
        return TB_SYMBOL_STALE;
    }

    load->offered = true;
    load->offer = offer;
    TbSymbolOutcome_e outcome = tb_symbol_artifact_offer(load->owner->artifacts, load->info.candidate, offer);
    if (!current(load))
        error(load, TB_SYMBOL_STALE);
    if (outcome != TB_SYMBOL_SUCCESS)
        error(load, outcome);
    if (load->info.outcome != TB_SYMBOL_PENDING) {
        load->info.cancel_required = true;
        return load->info.outcome;
    }

    return TB_SYMBOL_SUCCESS;
}


static TbSymbolOutcome_e acquired(TbSymbolCompanionLoad_t *load, TbSymbolHelperResult_t *result)
{
    if (!load->offered || result->length != 56 || !result->payload || result->descriptor < 0)
        return TB_SYMBOL_MALFORMED;

    TbSymbolAcquiredBytes_t bytes = {
        .bytes      = tb_symbol_protocol_get(result->payload, 8),
        .descriptor = result->descriptor,
        .key = {
            .device = tb_symbol_protocol_get(result->payload + 8, 8),
            .inode  = tb_symbol_protocol_get(result->payload + 16, 8)
        }
    };
    memcpy(bytes.digest, result->payload + 24, sizeof(bytes.digest));
    TbSymbolOutcome_e outcome = tb_symbol_artifact_accept(load->owner->artifacts,
        load->info.candidate, &bytes);
    if (outcome != TB_SYMBOL_SUCCESS)
        return outcome;

    result->descriptor = -1;
    if (!tb_symbol_artifact_publish(load->owner->artifacts, load->info.candidate))
        return TB_SYMBOL_OPERATIONAL_FAILURE;
    load->published = true;
    TbSymbolArtifactInfo_t info;
    if (!tb_symbol_artifact_info(load->owner->artifacts, load->info.candidate, &info))
        return TB_SYMBOL_OPERATIONAL_FAILURE;
    load->candidate_content = info.content;
    load->info.phase = TB_SYMBOL_COMPANION_LOAD_INSPECT;

    return TB_SYMBOL_SUCCESS;
}


static TbSymbolOutcome_e cleanup_finish(TbSymbolCompanionLoad_t *load, TbSymbolHelperResult_t *result)
{
    TbSymbolOutcome_e outcome = result->outcome;
    if (outcome == TB_SYMBOL_SUCCESS && (result->length || result->descriptor >= 0))
        outcome = TB_SYMBOL_MALFORMED;
    if (outcome != TB_SYMBOL_SUCCESS) {
        load->info.cleanup_system_error = result->system_error;
        load->info.cleanup_detail = result->detail;
        blocked(load, outcome);
        return outcome;
    }

    bool cleaned = load->published ?
        tb_symbol_artifact_backing_cleaned(load->owner->artifacts, load->cleanup) :
        tb_symbol_artifact_cleaned(load->owner->artifacts, load->info.candidate,
            load->offered ? &load->offer.key : NULL);
    if (!cleaned) {
        blocked(load, TB_SYMBOL_OPERATIONAL_FAILURE);
        return TB_SYMBOL_OPERATIONAL_FAILURE;
    }
    load->info.cleanup_outcome = TB_SYMBOL_SUCCESS;
    load->info.cleanup_system_error = 0;
    load->info.cleanup_detail = 0;
    load->info.phase = TB_SYMBOL_COMPANION_LOAD_DONE;
    tb_symbol_companion_binding_free(load->binding);
    load->binding = NULL;

    return load->info.outcome;
}


TbSymbolOutcome_e tb_symbol_companion_load_finish(TbSymbolCompanionLoad_t *load,
    TbSymbolWorkIdentity_t identity, uint64_t process_generation, TbSymbolHelperResult_t *result)
{
    if (!identity_matches(load, identity, process_generation))
        return TB_SYMBOL_STALE;
    if (!result || result->outcome <= TB_SYMBOL_PENDING || result->outcome > TB_SYMBOL_OPERATIONAL_FAILURE)
        return TB_SYMBOL_MALFORMED;

    load->info.outstanding = false;
    TbSymbolOutcome_e outcome = result->outcome;
    if (load->info.phase == TB_SYMBOL_COMPANION_LOAD_CLEANUP) {
        outcome = cleanup_finish(load, result);
    } else if (load->info.phase == TB_SYMBOL_COMPANION_LOAD_INSPECT) {
        if (result->descriptor >= 0)
            outcome = TB_SYMBOL_MALFORMED;
        outcome = tb_symbol_companion_inspect_finish(load->inspection, identity, process_generation,
            outcome, result->payload, result->length, &load->candidate_fact);
        load->inspection = NULL;
        if (outcome == TB_SYMBOL_SUCCESS)
            load->info.phase = TB_SYMBOL_COMPANION_LOAD_READY;
    } else {
        if (!load->offered && (result->detail & TB_SYMBOL_DETAIL_HAS_KEY)) {
            /* No grant was authorized: retain the identified empty staging slot
             * for exact cleanup even when acquisition failed before its offer. */
            load->offer = (TbSymbolAcquireOffer_t) {
                .key = result->storage_key
            };
            TbSymbolOutcome_e recorded = tb_symbol_artifact_offer(load->owner->artifacts,
                load->info.candidate, load->offer);
            load->offered = recorded == TB_SYMBOL_SUCCESS || recorded == TB_SYMBOL_RESOURCE_FAILURE;
        }
        if (outcome == TB_SYMBOL_SUCCESS && load->info.outcome == TB_SYMBOL_PENDING && current(load))
            outcome = acquired(load, result);
    }

    if (result->descriptor >= 0) {
        (void)close(result->descriptor);
        result->descriptor = -1;
    }
    if (load->info.phase != TB_SYMBOL_COMPANION_LOAD_CLEANUP &&
        load->info.phase != TB_SYMBOL_COMPANION_LOAD_BLOCKED &&
        load->info.phase != TB_SYMBOL_COMPANION_LOAD_DONE) {
        if (!current(load))
            error(load, TB_SYMBOL_STALE);
        if (outcome != TB_SYMBOL_SUCCESS)
            error(load, outcome);
        if (load->info.outcome != TB_SYMBOL_PENDING) {
            load->info.system_error = result->system_error;
            load->info.detail = result->detail;
            cleanup_begin(load);
            return load->info.outcome;
        }
    }

    return outcome;
}


TbSymbolOutcome_e tb_symbol_companion_load_commit(TbSymbolCompanionLoad_t *load)
{
    if (!load || load->info.phase != TB_SYMBOL_COMPANION_LOAD_READY || load->info.outstanding)
        return TB_SYMBOL_STALE;

    TbSymbolOutcome_e outcome = TB_SYMBOL_STALE;
    if (current(load) && tb_symbol_companion_fact_current(load->candidate_fact)) {
        load->info.relation = tb_symbol_companion_relation(&load->primary->inventory,
            &load->candidate_fact->inventory, load->method);
        outcome = load->info.relation == TB_SYMBOL_COMPANION_RELATION_MATCH ? TB_SYMBOL_SUCCESS :
                                                                           TB_SYMBOL_UNSUPPORTED;
    }
    if (outcome == TB_SYMBOL_SUCCESS) {
        TbSymbolArtifactCapture_t capture;
        (void)tb_symbol_artifact_lease_capture(load->primary_lease, &capture);
        if (!tb_symbol_artifact_revise(load->owner->artifacts, capture.context, capture.revision))
            outcome = TB_SYMBOL_RESOURCE_FAILURE;
    }
    if (outcome != TB_SYMBOL_SUCCESS) {
        error(load, outcome);
        cleanup_begin(load);
        return outcome;
    }

    load->binding->primary = load->primary;
    load->binding->companion = load->candidate_fact;
    load->binding->pending = NULL;
    load->info.relationship = load->binding->identity;
    load->binding = NULL;
    load->primary = NULL;
    load->candidate_fact = NULL;
    tb_symbol_artifact_lease_release(load->primary_lease);
    load->primary_lease = NULL;
    load->info.outcome = TB_SYMBOL_SUCCESS;
    load->info.phase = TB_SYMBOL_COMPANION_LOAD_DONE;

    return TB_SYMBOL_SUCCESS;
}


bool tb_symbol_companion_load_cancel(TbSymbolCompanionLoad_t *load)
{
    if (!load || load->info.phase == TB_SYMBOL_COMPANION_LOAD_DONE ||
        load->info.phase == TB_SYMBOL_COMPANION_LOAD_CLEANUP ||
        load->info.phase == TB_SYMBOL_COMPANION_LOAD_BLOCKED) {
        return false;
    }

    error(load, TB_SYMBOL_CANCELLED);
    cleanup_begin(load);

    return true;
}


bool tb_symbol_companion_load_retry_cleanup(TbSymbolCompanionLoad_t *load)
{
    if (!load || load->info.phase != TB_SYMBOL_COMPANION_LOAD_BLOCKED || load->info.outstanding)
        return false;

    cleanup_begin(load);

    return load->info.phase == TB_SYMBOL_COMPANION_LOAD_CLEANUP;
}


TbSymbolCompanionLoadInfo_t tb_symbol_companion_load_info(const TbSymbolCompanionLoad_t *load)
{
    if (!load) {
        return (TbSymbolCompanionLoadInfo_t) {
            .phase    = TB_SYMBOL_COMPANION_LOAD_DONE,
            .outcome  = TB_SYMBOL_STALE,
            .relation = TB_SYMBOL_COMPANION_RELATION_UNEXAMINED
        };
    }

    TbSymbolCompanionLoadInfo_t info = load->info;
    if (info.outstanding && info.phase != TB_SYMBOL_COMPANION_LOAD_CLEANUP && !current(load))
        info.cancel_required = true;

    return info;
}


bool tb_symbol_companion_load_destroy(TbSymbolCompanionLoad_t *load)
{
    if (!load)
        return true;
    if (load->info.phase != TB_SYMBOL_COMPANION_LOAD_DONE || load->info.outstanding)
        return false;

    TbSymbolCompanions_t *owner = load->owner;
    size_t                bytes = load->bytes;
    --owner->load_count;
    free(load);
    (void)tb_symbol_budget_release(owner->budgets.construction, bytes);

    return true;
}
