#ifndef THE_BUGGER_SYMBOL_ARTIFACT_H
#define THE_BUGGER_SYMBOL_ARTIFACT_H

#include "symbol/acquire.h"
#include "symbol/admission.h"

typedef struct tb_symbol_artifacts TbSymbolArtifacts_t;
typedef struct tb_symbol_artifact_lease TbSymbolArtifactLease_t;
typedef struct tb_symbol_artifact_match TbSymbolArtifactMatch_t;

/* Frontend identity domain outlives registries and every retained historical fact.
 * Initialize once; never reset or copy it while those identities remain visible. */
typedef struct {
    uint64_t sequence;
} TbSymbolArtifactIds_t;

typedef struct {
    uint64_t file_bytes;
    uint64_t aggregate_bytes;
} TbSymbolArtifactConfig_t;

void tb_symbol_artifact_defaults(TbSymbolArtifactConfig_t *config);
/* Positive decimal MiB startup settings; invalid input leaves config unchanged. */
bool tb_symbol_artifact_environment(TbSymbolArtifactConfig_t *config);

typedef enum {
    TB_SYMBOL_ARTIFACT_PREPARED,
    TB_SYMBOL_ARTIFACT_RESERVED,
    TB_SYMBOL_ARTIFACT_STAGED,
    TB_SYMBOL_ARTIFACT_RETAINED,
    TB_SYMBOL_ARTIFACT_DISCARDING,
    TB_SYMBOL_ARTIFACT_CLOSING,
    TB_SYMBOL_ARTIFACT_UNAVAILABLE
} TbSymbolArtifactState_e;

typedef struct {
    uint64_t                context;
    uint64_t                revision;
    uint64_t                content;
    uint64_t                content_revision;
    uint64_t                bytes;
    const char             *origin;
    TbSymbolArtifactState_e state;
} TbSymbolArtifactInfo_t;

typedef struct {
    uint64_t             content;
    uint64_t             slot;
    uint64_t             bytes;
    TbSymbolStorageKey_t key;
} TbSymbolArtifactCleanup_t;

typedef struct {
    TbSymbolWorkIdentity_t request;
    uint64_t               process_generation;
    uint64_t               content;
    uint64_t               content_revision;
    uint64_t               left_slot;
    uint64_t               right_slot;
    uint64_t               bytes;
    TbSymbolStorageKey_t   left_key;
    TbSymbolStorageKey_t   right_key;
} TbSymbolArtifactMatchPlan_t;

/* Frontend ownership only. No acquisition, debugger operation or filesystem
 * deletion is implicit. The caller drives supervised operations and presents
 * their validated captured receipts. Retained means exact frozen bytes, not
 * ELF/DWARF validation, coherent source acquisition or runtime authority.
 * The allocation account must outlive the registry and every lease. Identity
 * ownership remains with the frontend across registry replacement. */
TbSymbolArtifacts_t *tb_symbol_artifacts_create(uint64_t file_bytes, uint64_t aggregate_bytes,
                                               TbSymbolArtifactIds_t *identities, TbSymbolBudget_t *frontend);
bool tb_symbol_artifacts_destroy(TbSymbolArtifacts_t *artifacts);
TbSymbolAdmissionUsage_t tb_symbol_artifacts_usage(const TbSymbolArtifacts_t *artifacts);
/* Returns a nonzero, nonreused context/slot identity within the frontend identity domain.
 * A complete candidate owner and logical ticket are allocated before discovery.
 * Origin spelling is provenance only. Equal names never share a context. */
uint64_t tb_symbol_artifact_prepare(TbSymbolArtifacts_t *artifacts, const char *origin);
TbSymbolOutcome_e tb_symbol_artifact_offer(TbSymbolArtifacts_t *artifacts, uint64_t context,
                                          TbSymbolAcquireOffer_t offer);
/* Caller has validated the helper result's exact captured scope and complete
 * digest, and retired acquisition/frame ownership. Checks descriptor identity
 * and protection; adopts it only on success. The offer must already have been
 * fully reserved before granting file growth. */
TbSymbolOutcome_e tb_symbol_artifact_accept(TbSymbolArtifacts_t *artifacts, uint64_t context,
                                           const TbSymbolAcquiredBytes_t *bytes);
bool tb_symbol_artifact_publish(TbSymbolArtifacts_t *artifacts, uint64_t context);
/* The origin string is borrowed until this context closes. */
bool tb_symbol_artifact_info(const TbSymbolArtifacts_t *artifacts, uint64_t context,
                             TbSymbolArtifactInfo_t *info);

/* Withdraw storage-dependent capabilities after detected backing integrity or
 * correspondence loss. Ownership, leases and charges remain until cleanup.
 * Metadata validation has its own finer dependency scopes in later gates. */
bool tb_symbol_artifact_withdraw(TbSymbolArtifacts_t *artifacts, uint64_t content);

/* Digest/extent only select candidates. A match pins exact left backing while
 * a supervised complete-byte comparison runs. Neither names nor a digest can
 * establish sharing. Request identity is captured separately from this staged
 * artifact's context identity and revision. Finish only after both the validated
 * completion and worker retirement receipt;
 * matching scope consumes the token even on failure/stale context. Wrong scope
 * leaves it owned so the correct retirement can still be delivered. A differing
 * fingerprint/extent is Unsupported for sharing, distinct from resource failure.
 * Construction/comparison failure makes the candidate unavailable for publication. */
TbSymbolOutcome_e tb_symbol_artifact_match_begin(TbSymbolArtifacts_t *artifacts, uint64_t context,
                                                uint64_t content, TbSymbolWorkIdentity_t request,
                                                uint64_t process_generation,
                                                TbSymbolArtifactMatchPlan_t *plan,
                                                TbSymbolArtifactMatch_t **match);
TbSymbolOutcome_e tb_symbol_artifact_match_finish(TbSymbolArtifactMatch_t *match,
                                                 TbSymbolWorkIdentity_t request, uint64_t process_generation,
                                                 TbSymbolOutcome_e outcome, bool equal);
/* Positive comparison retains a pin through candidate cleanup. Begin closes its
 * frontend descriptor while still linked. Cleaned commits sharing atomically
 * without allocating, after exact supervised deletion and worker retirement.
 * Closing during cleanup prevents publication and instead requires abandon. */
bool tb_symbol_artifact_share_begin(TbSymbolArtifacts_t *artifacts, uint64_t context,
                                    TbSymbolArtifactCleanup_t *cleanup);
bool tb_symbol_artifact_share_cleaned(TbSymbolArtifacts_t *artifacts, uint64_t context,
                                      TbSymbolArtifactCleanup_t cleanup);

/* A lease pins backing independently of context lifetime. The descriptor is
 * borrowed: never close it, and retain the lease until every derived descriptor
 * and worker interest has retired. Current-context eligibility is separate. */
TbSymbolArtifactLease_t *tb_symbol_artifact_lease(TbSymbolArtifacts_t *artifacts, uint64_t context);
bool tb_symbol_artifact_lease_current(const TbSymbolArtifactLease_t *lease);
int tb_symbol_artifact_lease_descriptor(const TbSymbolArtifactLease_t *lease);
void tb_symbol_artifact_lease_release(TbSymbolArtifactLease_t *lease);

/* Closing revokes context eligibility immediately. Pending acquisition must be
 * cancelled and its worker ownership retired before abandon. No charge is freed
 * by cancellation alone. A retained close moves its final charge into cache. */
bool tb_symbol_artifact_close(TbSymbolArtifacts_t *artifacts, uint64_t context);
bool tb_symbol_artifact_abandon(TbSymbolArtifacts_t *artifacts, uint64_t context);
/* Confirm supervised slot cleanup after abandon. Known offers require the exact
 * key. NULL is only for an unpublished slot with no offer/growth grant, after
 * successful fresh-empty-slot cleanup; it never asserts zero admitted content. */
bool tb_symbol_artifact_cleaned(TbSymbolArtifacts_t *artifacts, uint64_t context,
                                const TbSymbolStorageKey_t *key);
/* Explicit eviction closes the frontend descriptor while storage is still linked.
 * Dispatch unlink only after this succeeds; confirm its exact cleanup afterward.
 * Failed cleanup stays charged and cannot revive the backing. */
bool tb_symbol_artifact_evict(TbSymbolArtifacts_t *artifacts, uint64_t content,
                              TbSymbolArtifactCleanup_t *cleanup);
bool tb_symbol_artifact_backing_cleaned(TbSymbolArtifacts_t *artifacts,
                                        TbSymbolArtifactCleanup_t cleanup);

#endif // THE_BUGGER_SYMBOL_ARTIFACT_H
