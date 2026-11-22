#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Descriptor protection validation. */
#include "symbol/artifact.h"
#include "symbol/wire.h"

#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct artifact_node {
    void    *children[16];
    unsigned count;
} ArtifactNode_t;

typedef enum {
    BACKING_CANDIDATE,
    BACKING_PINNED,
    BACKING_CACHED,
    BACKING_CLEANUP
} BackingState_e;

typedef struct {
    TbSymbolBackingCharge_t *charge;
    uint64_t                 identity;
    uint64_t                 revision;
    uint64_t                 slot;
    uint64_t                 bytes;
    uint64_t                 contexts;
    uint64_t                 leases;
    TbSymbolStorageKey_t     key;
    unsigned char            digest[TB_SHA256_DIGEST_BYTES];
    BackingState_e           state;
    int                      descriptor;
    bool                     available;
} ArtifactBacking_t;

typedef struct {
    ArtifactBacking_t       *backing;
    ArtifactBacking_t       *shared;
    TbSymbolArtifactMatch_t *match;
    TbSymbolReservation_t   *ticket;
    char                    *origin;
    size_t                   origin_bytes;
    uint64_t                 identity;
    uint64_t                 revision;
    TbSymbolArtifactState_e  state;
    bool                     offered;
    bool                     abandoned;
} ArtifactContext_t;

struct tb_symbol_artifacts {
    TbSymbolBudget_t          *frontend;
    TbSymbolAdmissionLedger_t *ledger;
    ArtifactNode_t             contexts;
    ArtifactNode_t             backings;
    TbSymbolArtifactIds_t     *identities;
    uint64_t                   context_count;
    uint64_t                   backing_count;
};

struct tb_symbol_artifact_lease {
    TbSymbolArtifacts_t *owner;
    ArtifactBacking_t   *backing;
    uint64_t             context;
    uint64_t             revision;
    uint64_t             content_revision;
};

struct tb_symbol_artifact_match {
    TbSymbolArtifacts_t          *owner;
    ArtifactBacking_t            *left;
    uint64_t                      context;
    uint64_t                      revision;
    TbSymbolArtifactMatchPlan_t    plan;
};

void tb_symbol_artifact_defaults(TbSymbolArtifactConfig_t *config)
{
    if (config) {
        *config = (TbSymbolArtifactConfig_t) {
            .file_bytes      = TB_SYMBOL_FILE_BYTES_DEFAULT,
            .aggregate_bytes = TB_SYMBOL_ADMISSION_BYTES_DEFAULT
        };
    }
}


static bool environment_bytes(const char *name, uint64_t *bytes)
{
    const char *value = getenv(name);
    if (!value)
        return true;

    if (!*value)
        return false;

    uint64_t number = 0;
    for (; *value; ++value) {
        if (*value < '0' || *value > '9')
            return false;

        unsigned digit_value = (unsigned)(*value - '0');
        if (number > (UINT64_MAX - digit_value) / 10)
            return false;

        number = number * 10 + digit_value;
    }

    const uint64_t mib = UINT64_C(1024) * 1024;
    if (!number || number > UINT64_MAX / mib)
        return false;

    *bytes = number * mib;

    return true;
}


bool tb_symbol_artifact_environment(TbSymbolArtifactConfig_t *config)
{
    if (!config)
        return false;

    TbSymbolArtifactConfig_t candidate = *config;
    if (!environment_bytes("TB_SYMBOL_FILE_MIB", &candidate.file_bytes) ||
        !environment_bytes("TB_SYMBOL_ADMISSION_MIB", &candidate.aggregate_bytes) ||
        !candidate.file_bytes || !candidate.aggregate_bytes ||
        candidate.file_bytes > candidate.aggregate_bytes) {
        return false;
    }

    *config = candidate;

    return true;
}


static bool reserve(TbSymbolArtifacts_t *owner, size_t bytes)
{
    return !owner->frontend || tb_symbol_budget_reserve(owner->frontend, bytes);
}


static void release(TbSymbolArtifacts_t *owner, size_t bytes)
{
    if (owner->frontend)
        (void)tb_symbol_budget_release(owner->frontend, bytes);
}


static unsigned digit(uint64_t identity, unsigned depth)
{
    return (unsigned)((identity >> ((15 - depth) * 4U)) & UINT64_C(15));
}


static void *lookup(const ArtifactNode_t *root, uint64_t identity)
{
    if (!identity)
        return NULL;

    const ArtifactNode_t *node = root;
    for (unsigned depth = 0; depth < 15; ++depth) {
        node = node->children[digit(identity, depth)];

        if (!node)
            return NULL;
    }

    return node->children[digit(identity, 15)];
}


static bool insert(TbSymbolArtifacts_t *owner, ArtifactNode_t *root, uint64_t identity, void *value)
{
    ArtifactNode_t *nodes[16] = {root};
    unsigned created = 16;
    for (unsigned depth = 0; depth < 15; ++depth) {
        unsigned index = digit(identity, depth);
        ArtifactNode_t *parent = nodes[depth];
        if (!parent->children[index]) {
            ArtifactNode_t *node = NULL;
            if (reserve(owner, sizeof(*node))) {
                node = calloc(1, sizeof(*node));

                if (!node)
                    release(owner, sizeof(*node));
            }

            if (!node) {
                /* Only this insertion's new suffix is owned by rollback. */
                for (unsigned i = depth; i > created; --i) {
                    free(nodes[i]);
                    release(owner, sizeof(ArtifactNode_t));
                }

                if (created < 16) {
                    ArtifactNode_t *first = nodes[created];

                    first->children[digit(identity, created)] = NULL;
                    --first->count;
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

    ArtifactNode_t *leaf = nodes[15];
    unsigned index = digit(identity, 15);
    if (leaf->children[index])
        return false;

    leaf->children[index] = value;
    ++leaf->count;

    return true;
}


static void erase(TbSymbolArtifacts_t *owner, ArtifactNode_t *root, uint64_t identity)
{
    ArtifactNode_t *nodes[16] = {root};
    for (unsigned depth = 0; depth < 15; ++depth)
        nodes[depth + 1] = nodes[depth]->children[digit(identity, depth)];

    nodes[15]->children[digit(identity, 15)] = NULL;
    --nodes[15]->count;

    for (unsigned depth = 15; depth && !nodes[depth]->count; --depth) {
        free(nodes[depth]);
        release(owner, sizeof(ArtifactNode_t));
        nodes[depth - 1]->children[digit(identity, depth - 1)] = NULL;
        --nodes[depth - 1]->count;
    }
}


TbSymbolArtifacts_t *tb_symbol_artifacts_create(uint64_t file_bytes, uint64_t aggregate_bytes,
                                               TbSymbolArtifactIds_t *identities, TbSymbolBudget_t *frontend)
{
    if (!identities)
        return NULL;

    if (frontend && !tb_symbol_budget_reserve(frontend, sizeof(TbSymbolArtifacts_t)))
        return NULL;

    TbSymbolArtifacts_t *owner = calloc(1, sizeof(*owner));
    if (!owner) {
        if (frontend)
            (void)tb_symbol_budget_release(frontend, sizeof(TbSymbolArtifacts_t));
        return NULL;
    }

    owner->frontend = frontend;
    owner->identities = identities;
    owner->ledger = tb_symbol_admission_create(file_bytes, aggregate_bytes, frontend);

    if (!owner->ledger) {
        free(owner);

        if (frontend)
            (void)tb_symbol_budget_release(frontend, sizeof(TbSymbolArtifacts_t));
        return NULL;
    }

    return owner;
}


bool tb_symbol_artifacts_destroy(TbSymbolArtifacts_t *owner)
{
    if (!owner)
        return true;

    if (owner->context_count || owner->backing_count || !tb_symbol_admission_destroy(owner->ledger))
        return false;

    TbSymbolBudget_t *frontend = owner->frontend;

    free(owner);

    if (frontend)
        (void)tb_symbol_budget_release(frontend, sizeof(TbSymbolArtifacts_t));

    return true;
}


TbSymbolAdmissionUsage_t tb_symbol_artifacts_usage(const TbSymbolArtifacts_t *owner)
{
    return tb_symbol_admission_usage(owner ? owner->ledger : NULL);
}


uint64_t tb_symbol_artifact_prepare(TbSymbolArtifacts_t *owner, const char *origin)
{
    if (!owner || !origin || owner->identities->sequence > UINT64_MAX - 2)
        return 0;

    size_t length = strnlen(origin, TB_SYMBOL_WIRE_MAX_PAYLOAD);
    if (!length || length == TB_SYMBOL_WIRE_MAX_PAYLOAD)
        return 0;

    size_t bytes = sizeof(ArtifactContext_t) + sizeof(ArtifactBacking_t) + length + 1;
    if (!reserve(owner, bytes))
        return 0;

    ArtifactContext_t *context = calloc(1, sizeof(*context));
    ArtifactBacking_t *backing = context ? calloc(1, sizeof(*backing)) : NULL;
    char *copy = backing ? malloc(length + 1) : NULL;
    TbSymbolReservation_t *ticket = copy ? tb_symbol_admission_prepare(owner->ledger) : NULL;
    if (!ticket) {
        free(copy);
        free(backing);
        free(context);
        release(owner, bytes);

        return 0;
    }

    memcpy(copy, origin, length + 1);

    uint64_t identity = ++owner->identities->sequence;
    uint64_t content = ++owner->identities->sequence;

    *backing = (ArtifactBacking_t) {
        .identity   = content,
        .slot       = identity,
        .descriptor = -1
    };

    *context = (ArtifactContext_t) {
        .backing      = backing,
        .ticket       = ticket,
        .origin       = copy,
        .origin_bytes = length + 1,
        .identity     = identity,
        .revision     = 1
    };

    bool inserted = insert(owner, &owner->contexts, identity, context);
    if (!inserted || !insert(owner, &owner->backings, content, backing)) {
        if (inserted)
            erase(owner, &owner->contexts, identity);
        (void)tb_symbol_admission_abort(ticket);
        (void)tb_symbol_admission_cleaned(ticket);
        free(copy);
        free(backing);
        free(context);
        release(owner, bytes);

        return 0;
    }

    ++owner->context_count;
    ++owner->backing_count;

    return identity;
}


TbSymbolOutcome_e tb_symbol_artifact_offer(TbSymbolArtifacts_t *owner, uint64_t identity,
                                          TbSymbolAcquireOffer_t offer)
{
    ArtifactContext_t *context = owner ? lookup(&owner->contexts, identity) : NULL;
    if (!context || context->state != TB_SYMBOL_ARTIFACT_PREPARED || context->offered)
        return TB_SYMBOL_STALE;

    context->offered = true;
    context->backing->key = offer.key;
    context->backing->bytes = offer.bytes;

    if (!tb_symbol_admission_assign(context->ticket, offer.bytes))
        return TB_SYMBOL_RESOURCE_FAILURE;

    context->state = TB_SYMBOL_ARTIFACT_RESERVED;

    return TB_SYMBOL_SUCCESS;
}


static bool same_key(TbSymbolStorageKey_t left, TbSymbolStorageKey_t right)
{
    return left.device == right.device && left.inode == right.inode;
}


static TbSymbolOutcome_e validate_descriptor(const TbSymbolAcquiredBytes_t *bytes)
{
    if (bytes->descriptor < 0)
        return TB_SYMBOL_MALFORMED;

    int flags = fcntl(bytes->descriptor, F_GETFL);
    int fd_flags = fcntl(bytes->descriptor, F_GETFD);
    if (flags < 0 || fd_flags < 0)
        return TB_SYMBOL_OPERATIONAL_FAILURE;

    if (((unsigned)flags & (unsigned)O_PATH) || ((unsigned)flags & (unsigned)O_ACCMODE) != O_RDONLY ||
        !((unsigned)fd_flags & (unsigned)FD_CLOEXEC)) {
        return TB_SYMBOL_MALFORMED;
    }

    struct stat info;
    if (fstat(bytes->descriptor, &info))
        return TB_SYMBOL_OPERATIONAL_FAILURE;

    bool valid = S_ISREG(info.st_mode) && info.st_uid == geteuid() && info.st_nlink == 1 &&
                 (info.st_mode & 0777U) == 0400U && info.st_size >= 0 &&
                 (uint64_t)info.st_size == bytes->bytes && (uint64_t)info.st_dev == bytes->key.device &&
                 (uint64_t)info.st_ino == bytes->key.inode;

    return valid ? TB_SYMBOL_SUCCESS : TB_SYMBOL_MALFORMED;
}


TbSymbolOutcome_e tb_symbol_artifact_accept(TbSymbolArtifacts_t *owner, uint64_t identity,
                                           const TbSymbolAcquiredBytes_t *bytes)
{
    ArtifactContext_t *context = owner ? lookup(&owner->contexts, identity) : NULL;
    if (!context || context->state != TB_SYMBOL_ARTIFACT_RESERVED)
        return TB_SYMBOL_STALE;

    if (!bytes || bytes->bytes != context->backing->bytes || !same_key(bytes->key, context->backing->key))
        return TB_SYMBOL_MALFORMED;

    TbSymbolOutcome_e result = validate_descriptor(bytes);
    if (result != TB_SYMBOL_SUCCESS)
        return result;

    context->backing->descriptor = bytes->descriptor;
    memcpy(context->backing->digest, bytes->digest, sizeof(bytes->digest));
    context->state = TB_SYMBOL_ARTIFACT_STAGED;

    return TB_SYMBOL_SUCCESS;
}


bool tb_symbol_artifact_publish(TbSymbolArtifacts_t *owner, uint64_t identity)
{
    ArtifactContext_t *context = owner ? lookup(&owner->contexts, identity) : NULL;
    if (!context || context->state != TB_SYMBOL_ARTIFACT_STAGED || context->match || context->shared)
        return false;

    TbSymbolBackingCharge_t *charge = tb_symbol_admission_commit(context->ticket, NULL);
    if (!charge)
        return false;

    context->backing->charge = charge;
    context->backing->contexts = 1;
    context->backing->state = BACKING_PINNED;
    context->backing->available = true;
    context->backing->revision = 1;
    context->state = TB_SYMBOL_ARTIFACT_RETAINED;

    return true;
}


bool tb_symbol_artifact_info(const TbSymbolArtifacts_t *owner, uint64_t identity,
                             TbSymbolArtifactInfo_t *info)
{
    ArtifactContext_t *context = owner ? lookup(&owner->contexts, identity) : NULL;
    if (!context || !info)
        return false;

    *info = (TbSymbolArtifactInfo_t) {
        .context          = identity,
        .revision         = context->revision,
        .content          = context->state == TB_SYMBOL_ARTIFACT_RETAINED ? context->backing->identity : 0,
        .content_revision = context->state == TB_SYMBOL_ARTIFACT_RETAINED ? context->backing->revision : 0,
        .bytes            = context->backing->bytes,
        .origin           = context->origin,
        .state            = context->state == TB_SYMBOL_ARTIFACT_RETAINED && !context->backing->available
                              ? TB_SYMBOL_ARTIFACT_UNAVAILABLE : context->state
    };

    return true;
}


TbSymbolArtifactLease_t *tb_symbol_artifact_lease(TbSymbolArtifacts_t *owner, uint64_t identity)
{
    ArtifactContext_t *context = owner ? lookup(&owner->contexts, identity) : NULL;
    if (!context || context->state != TB_SYMBOL_ARTIFACT_RETAINED || !context->backing->available ||
        context->backing->leases == UINT64_MAX || !reserve(owner, sizeof(TbSymbolArtifactLease_t))) {
        return NULL;
    }

    TbSymbolArtifactLease_t *lease = calloc(1, sizeof(*lease));
    if (!lease) {
        release(owner, sizeof(*lease));
        return NULL;
    }

    *lease = (TbSymbolArtifactLease_t) {
        .owner            = owner,
        .backing          = context->backing,
        .context          = identity,
        .revision         = context->revision,
        .content_revision = context->backing->revision
    };

    ++context->backing->leases;

    return lease;
}


bool tb_symbol_artifact_lease_current(const TbSymbolArtifactLease_t *lease)
{
    ArtifactContext_t *context = lease ? lookup(&lease->owner->contexts, lease->context) : NULL;

    return context && context->state == TB_SYMBOL_ARTIFACT_RETAINED &&
           context->revision == lease->revision && context->backing == lease->backing &&
           lease->backing->available && lease->backing->revision == lease->content_revision;
}


int tb_symbol_artifact_lease_descriptor(const TbSymbolArtifactLease_t *lease)
{
    return lease ? lease->backing->descriptor : -1;
}


void tb_symbol_artifact_lease_release(TbSymbolArtifactLease_t *lease)
{
    if (!lease)
        return;

    --lease->backing->leases;

    TbSymbolArtifacts_t *owner = lease->owner;

    free(lease);
    release(owner, sizeof(TbSymbolArtifactLease_t));
}


static void context_free(TbSymbolArtifacts_t *owner, ArtifactContext_t *context)
{
    erase(owner, &owner->contexts, context->identity);
    --owner->context_count;

    size_t bytes = sizeof(*context) + context->origin_bytes;

    free(context->origin);
    free(context);
    release(owner, bytes);
}


static void backing_free(TbSymbolArtifacts_t *owner, ArtifactBacking_t *backing)
{
    erase(owner, &owner->backings, backing->identity);
    --owner->backing_count;
    free(backing);
    release(owner, sizeof(ArtifactBacking_t));
}


bool tb_symbol_artifact_close(TbSymbolArtifacts_t *owner, uint64_t identity)
{
    ArtifactContext_t *context = owner ? lookup(&owner->contexts, identity) : NULL;
    if (!context || context->state == TB_SYMBOL_ARTIFACT_CLOSING)
        return false;

    if (context->state != TB_SYMBOL_ARTIFACT_RETAINED) {
        context->state = TB_SYMBOL_ARTIFACT_CLOSING;
        ++context->revision;
        return true;
    }

    if (!tb_symbol_admission_close(context->ticket))
        return false;

    if (!--context->backing->contexts)
        context->backing->state = BACKING_CACHED;
    context_free(owner, context);

    return true;
}


bool tb_symbol_artifact_abandon(TbSymbolArtifacts_t *owner, uint64_t identity)
{
    ArtifactContext_t *context = owner ? lookup(&owner->contexts, identity) : NULL;
    if (!context || context->state == TB_SYMBOL_ARTIFACT_RETAINED || context->abandoned || context->match)
        return false;

    if (context->state != TB_SYMBOL_ARTIFACT_CLOSING) {
        context->state = TB_SYMBOL_ARTIFACT_CLOSING;
        ++context->revision;
    }

    if (!tb_symbol_admission_abort(context->ticket))
        return false;

    if (context->backing->descriptor >= 0) {
        (void)close(context->backing->descriptor);
        context->backing->descriptor = -1;
    }

    if (context->shared) {
        --context->shared->leases;
        context->shared = NULL;
    }
    context->abandoned = true;

    return true;
}


bool tb_symbol_artifact_cleaned(TbSymbolArtifacts_t *owner, uint64_t identity,
                                const TbSymbolStorageKey_t *key)
{
    ArtifactContext_t *context = owner ? lookup(&owner->contexts, identity) : NULL;
    if (!context || !context->abandoned || context->offered != (key != NULL) ||
        (key && !same_key(*key, context->backing->key))) {
        return false;
    }

    if (!tb_symbol_admission_cleaned(context->ticket))
        return false;

    backing_free(owner, context->backing);
    context_free(owner, context);

    return true;
}


bool tb_symbol_artifact_evict(TbSymbolArtifacts_t *owner, uint64_t identity,
                              TbSymbolArtifactCleanup_t *cleanup)
{
    ArtifactBacking_t *backing = owner ? lookup(&owner->backings, identity) : NULL;
    if (!backing || !cleanup || backing->state != BACKING_CACHED || backing->leases ||
        !tb_symbol_admission_evict(backing->charge)) {
        return false;
    }

    (void)close(backing->descriptor);
    backing->descriptor = -1;
    backing->state = BACKING_CLEANUP;
    *cleanup = (TbSymbolArtifactCleanup_t) {
        .content = identity,
        .slot    = backing->slot,
        .bytes   = backing->bytes,
        .key     = backing->key
    };

    return true;
}


bool tb_symbol_artifact_backing_cleaned(TbSymbolArtifacts_t *owner, TbSymbolArtifactCleanup_t cleanup)
{
    ArtifactBacking_t *backing = owner ? lookup(&owner->backings, cleanup.content) : NULL;
    if (!backing || backing->state != BACKING_CLEANUP || backing->slot != cleanup.slot ||
        backing->bytes != cleanup.bytes || !same_key(backing->key, cleanup.key) ||
        !tb_symbol_admission_backing_cleaned(backing->charge)) {
        return false;
    }

    backing_free(owner, backing);

    return true;
}


static bool same_request(TbSymbolWorkIdentity_t left, TbSymbolWorkIdentity_t right)
{
    return left.request == right.request && left.consumer == right.consumer &&
           left.revision == right.revision && left.generation == right.generation;
}


TbSymbolOutcome_e tb_symbol_artifact_match_begin(TbSymbolArtifacts_t *owner, uint64_t identity,
                                                uint64_t content, TbSymbolWorkIdentity_t request,
                                                uint64_t process_generation,
                                                TbSymbolArtifactMatchPlan_t *plan,
                                                TbSymbolArtifactMatch_t **result)
{
    ArtifactContext_t *context = owner ? lookup(&owner->contexts, identity) : NULL;
    ArtifactBacking_t *left = owner ? lookup(&owner->backings, content) : NULL;
    if (!context || !left || context->state != TB_SYMBOL_ARTIFACT_STAGED || context->match ||
        context->shared || !left->available ||
        (left->state != BACKING_PINNED && left->state != BACKING_CACHED)) {
        return TB_SYMBOL_STALE;
    }

    if (!plan || !result || !request.request || !request.consumer || !request.generation ||
        !process_generation) {
        return TB_SYMBOL_MALFORMED;
    }

    if (left == context->backing || left->bytes != context->backing->bytes ||
        memcmp(left->digest, context->backing->digest, sizeof(left->digest)) != 0) {
        return TB_SYMBOL_UNSUPPORTED;
    }

    TbSymbolArtifactMatch_t *match = NULL;
    if (left->leases != UINT64_MAX && reserve(owner, sizeof(*match))) {
        match = calloc(1, sizeof(*match));

        if (!match)
            release(owner, sizeof(*match));
    }

    if (!match) {
        context->state = TB_SYMBOL_ARTIFACT_CLOSING;
        ++context->revision;
        return TB_SYMBOL_RESOURCE_FAILURE;
    }

    *match = (TbSymbolArtifactMatch_t) {
        .owner    = owner,
        .left     = left,
        .context  = identity,
        .revision = context->revision,
        .plan     = {
            .request            = request,
            .process_generation = process_generation,
            .content            = content,
            .content_revision   = left->revision,
            .left_slot          = left->slot,
            .right_slot         = context->backing->slot,
            .bytes              = left->bytes,
            .left_key           = left->key,
            .right_key          = context->backing->key
        }
    };

    ++left->leases;
    context->match = match;
    *plan = match->plan;
    *result = match;

    return TB_SYMBOL_SUCCESS;
}


TbSymbolOutcome_e tb_symbol_artifact_match_finish(TbSymbolArtifactMatch_t *match,
                                                 TbSymbolWorkIdentity_t request, uint64_t process_generation,
                                                 TbSymbolOutcome_e outcome, bool equal)
{
    if (!match || !same_request(request, match->plan.request) ||
        process_generation != match->plan.process_generation || outcome == TB_SYMBOL_PENDING ||
        outcome > TB_SYMBOL_OPERATIONAL_FAILURE || outcome < TB_SYMBOL_PENDING) {
        return TB_SYMBOL_STALE;
    }

    TbSymbolArtifacts_t *owner = match->owner;
    ArtifactContext_t *context = lookup(&owner->contexts, match->context);
    bool current = context && context->state == TB_SYMBOL_ARTIFACT_STAGED &&
                   context->revision == match->revision && context->match == match;
    if (current && (!match->left->available || match->left->revision != match->plan.content_revision))
        outcome = TB_SYMBOL_STALE;

    if (context)
        context->match = NULL;

    if (current && outcome == TB_SYMBOL_SUCCESS && equal) {
        context->shared = match->left;
    } else {
        --match->left->leases;

        if (current && outcome != TB_SYMBOL_SUCCESS) {
            context->state = TB_SYMBOL_ARTIFACT_CLOSING;
            ++context->revision;
        }
    }
    free(match);
    release(owner, sizeof(TbSymbolArtifactMatch_t));

    return current ? outcome : TB_SYMBOL_STALE;
}


static TbSymbolArtifactCleanup_t cleanup_for(const ArtifactBacking_t *backing)
{
    return (TbSymbolArtifactCleanup_t) {
        .content = backing->identity,
        .slot    = backing->slot,
        .bytes   = backing->bytes,
        .key     = backing->key
    };
}


static bool same_cleanup(TbSymbolArtifactCleanup_t cleanup, const ArtifactBacking_t *backing)
{
    return cleanup.content == backing->identity && cleanup.slot == backing->slot &&
           cleanup.bytes == backing->bytes && same_key(cleanup.key, backing->key);
}


bool tb_symbol_artifact_share_begin(TbSymbolArtifacts_t *owner, uint64_t identity,
                                    TbSymbolArtifactCleanup_t *cleanup)
{
    ArtifactContext_t *context = owner ? lookup(&owner->contexts, identity) : NULL;
    if (!context || !cleanup || context->state != TB_SYMBOL_ARTIFACT_STAGED || !context->shared ||
        !context->shared->available) {
        return false;
    }

    (void)close(context->backing->descriptor);
    context->backing->descriptor = -1;
    context->state = TB_SYMBOL_ARTIFACT_DISCARDING;
    *cleanup = cleanup_for(context->backing);

    return true;
}


bool tb_symbol_artifact_share_cleaned(TbSymbolArtifacts_t *owner, uint64_t identity,
                                      TbSymbolArtifactCleanup_t cleanup)
{
    ArtifactContext_t *context = owner ? lookup(&owner->contexts, identity) : NULL;
    if (!context || context->state != TB_SYMBOL_ARTIFACT_DISCARDING || !context->shared ||
        !context->shared->available || !same_cleanup(cleanup, context->backing)) {
        return false;
    }

    TbSymbolBackingCharge_t *charge = tb_symbol_admission_commit(context->ticket, context->shared->charge);
    if (!charge)
        return false;

    ArtifactBacking_t *shared = context->shared;

    backing_free(owner, context->backing);
    ++shared->contexts;
    --shared->leases;
    shared->state = BACKING_PINNED;
    context->backing = shared;
    context->shared = NULL;
    context->state = TB_SYMBOL_ARTIFACT_RETAINED;

    return true;
}


bool tb_symbol_artifact_withdraw(TbSymbolArtifacts_t *owner, uint64_t content)
{
    ArtifactBacking_t *backing = owner ? lookup(&owner->backings, content) : NULL;
    if (!backing || !backing->available ||
        (backing->state != BACKING_PINNED && backing->state != BACKING_CACHED)) {
        return false;
    }

    backing->available = false;
    ++backing->revision;

    return true;
}
