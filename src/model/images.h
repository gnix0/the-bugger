#ifndef THE_BUGGER_IMAGES_H
#define THE_BUGGER_IMAGES_H

#include <stdbool.h>
#include <stdint.h>
#include <sys/types.h>

typedef enum { TB_IMAGE_UNKNOWN, TB_IMAGE_CODE, TB_IMAGE_DATA } TbImageKind;
typedef struct {
    uint64_t start, end, anchor;
    TbImageKind kind;
    char name[96];
} TbImageRange;
typedef struct TbImages TbImages;
TbImages *tb_images_create(void);
void tb_images_destroy(TbImages *images);
void tb_images_reset(TbImages *images);
/* Metadata only. GDB remains responsible for reading/decoding target bytes. */
bool tb_images_load(TbImages *images, const char *path, uint64_t bias);
bool tb_images_lookup(const TbImages *images, uint64_t address, TbImageRange *range);
bool tb_images_neighbor(const TbImages *images, uint64_t address, int direction, TbImageRange *range);
/* Linux native adapter: resolve the mapped image containing PC, including PIE.
 * Called after stops, never from a protocol callback or while running. */
void tb_images_native(TbImages *images, pid_t pid, const char *executable, uint64_t pc);
bool tb_images_limited(const TbImages *images);
#endif
