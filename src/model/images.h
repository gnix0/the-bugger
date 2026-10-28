#ifndef THE_BUGGER_IMAGES_H
#define THE_BUGGER_IMAGES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <sys/types.h>

typedef enum { TB_IMAGE_UNKNOWN, TB_IMAGE_CODE, TB_IMAGE_DATA } TbImageKind;
typedef enum { TB_IMAGE_UNVERIFIED, TB_IMAGE_USER_CONFIRMED, TB_IMAGE_BACKEND_VERIFIED } TbImageTrust;
typedef struct {
    uint64_t start, end, anchor;
    TbImageKind kind;
    TbImageTrust trust;
    char name[96];
} TbImageRange;
typedef struct {
    char path[4096];
    uint64_t bias, revision;
    size_t ranges;
} TbImageReview;
typedef struct TbImages TbImages;
TbImages *tb_images_create(void);
void tb_images_destroy(TbImages *images);
void tb_images_reset(TbImages *images);
/* Metadata only. GDB remains responsible for reading/decoding target bytes. */
bool tb_images_load(TbImages *images, const char *path, uint64_t bias);
/* Local metadata only. A review binds the file identity and additive bias to
 * linked virtual addresses. Confirmed mappings do not prove live byte equality
 * or authorize any memory access. Stale reviews cannot confirm changed files. */
bool tb_images_review(TbImages *images, const char *path, uint64_t bias, TbImageReview *review);
bool tb_images_review_range(const TbImages *images, const TbImageReview *review, size_t index, TbImageRange *range);
bool tb_images_confirm(TbImages *images, const TbImageReview *review, TbImageTrust trust);
/* Check file identities before model requests, never from rendering. Returns
 * true when evidence changed. Invalidation preserves retained metadata. */
bool tb_images_validate(TbImages *images);
bool tb_images_unverify(TbImages *images);
uint64_t tb_images_revision(const TbImages *images);
bool tb_images_lookup(const TbImages *images, uint64_t address, TbImageRange *range);
bool tb_images_trusted_lookup(const TbImages *images, uint64_t address, TbImageRange *range);
bool tb_images_neighbor(const TbImages *images, uint64_t address, int direction, TbImageRange *range);
bool tb_images_trusted_neighbor(const TbImages *images, uint64_t address, int direction, TbImageRange *range);
/* Linux native adapter: resolve the mapped image containing PC, including PIE.
 * Called after stops, never from a protocol callback or while running. */
void tb_images_native(TbImages *images, pid_t pid, const char *executable, uint64_t pc);
bool tb_images_limited(const TbImages *images);
#endif
