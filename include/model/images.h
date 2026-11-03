#ifndef THE_BUGGER_IMAGES_H
#define THE_BUGGER_IMAGES_H

#include "session/setup.h"
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef enum {
    TB_IMAGE_UNKNOWN,
    TB_IMAGE_CODE,
    TB_IMAGE_DATA
} TbImageKind_e;

typedef enum {
    TB_IMAGE_UNVERIFIED,
    TB_IMAGE_USER_CONFIRMED,
    TB_IMAGE_BACKEND_VERIFIED
} TbImageTrust_e;

typedef struct {
    uint64_t       start;
    uint64_t       end;
    uint64_t       anchor;
    TbImageKind_e  kind;
    TbImageTrust_e trust;
    char           name[96];
} TbImageRange_t;

typedef struct {
    char     path[4096];
    uint64_t bias;
    uint64_t revision;
    size_t   ranges;
} TbImageReview_t;

typedef struct tb_images TbImages_t;

TbImages_t *tb_images_create(void);
void tb_images_destroy(TbImages_t *images);
void tb_images_reset(TbImages_t *images);
/* Metadata only. GDB remains responsible for reading/decoding target bytes. */
bool tb_images_load(TbImages_t *images, const char *path, uint64_t bias);
/* Local metadata only. A review binds the file identity and additive bias to
 * linked virtual addresses. Confirmed mappings do not prove live byte equality
 * or authorize any memory access. Stale reviews cannot confirm changed files. */
bool tb_images_review(TbImages_t *images, const char *path, uint64_t bias, TbImageReview_t *review);
bool tb_images_review_range(const TbImages_t *images, const TbImageReview_t *review, size_t index,
                            TbImageRange_t *range);
bool tb_images_confirm(TbImages_t *images, const TbImageReview_t *review, TbImageTrust_e trust);
/* Check file identities before model requests, never from rendering. Returns
 * true when evidence changed. Invalidation preserves retained metadata. */
bool tb_images_validate(TbImages_t *images);
bool tb_images_unverify(TbImages_t *images);
uint64_t tb_images_revision(const TbImages_t *images);
bool tb_images_lookup(const TbImages_t *images, uint64_t address, TbImageRange_t *range);
bool tb_images_trusted_lookup(const TbImages_t *images, uint64_t address, TbImageRange_t *range);
bool tb_images_neighbor(const TbImages_t *images, uint64_t address, int direction, TbImageRange_t *range);
bool tb_images_trusted_neighbor(const TbImages_t *images, uint64_t address, int direction,
                                TbImageRange_t *range);
/* Linux native adapter: resolve the mapped image containing PC, including PIE.
 * Called after stops, never from a protocol callback or while running. */
void tb_images_native(TbImages_t *images, pid_t pid, const char *executable, uint64_t pc);
/* Current procfs/ELF metadata proof, no target bytes or access permission. */
bool tb_images_native_mapping(pid_t pid, uint64_t address, TbSetupMapping_t *mapping);
bool tb_images_mapping_equal(const TbSetupMapping_t *saved, const TbSetupMapping_t *current,
                             uint64_t address);
bool tb_images_limited(const TbImages_t *images);

#endif // THE_BUGGER_IMAGES_H
