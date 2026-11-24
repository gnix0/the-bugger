#ifndef THE_BUGGER_SYMBOL_COMPANION_PATHS_H
#define THE_BUGGER_SYMBOL_COMPANION_PATHS_H

#include "symbol/source_path.h"

/* Private operation/schema bounds, not allocations or filesystem sandbox limits.
 * Smaller caller capacities fail explicitly. Production sizing remains SE10. */
enum {
    TB_SYMBOL_COMPANION_PATH_BYTES = 4096,
    TB_SYMBOL_COMPANION_ID_BYTES   = 4096,
    TB_SYMBOL_COMPANION_ROOTS      = 32
};

typedef enum {
    TB_SYMBOL_COMPANION_BESIDE,
    TB_SYMBOL_COMPANION_DOT_DEBUG,
    TB_SYMBOL_COMPANION_MIRROR,
    TB_SYMBOL_COMPANION_BUILD_ID
} TbSymbolCompanionPathKind_e;

typedef struct {
    TbSymbolSourcePathText_t        origin;
    TbSymbolSourcePathText_t        debuglink;
    TbSymbolSourcePathText_t        build_id;
    const TbSymbolSourcePathText_t *roots;
    size_t                          root_count;
} TbSymbolCompanionPathsRequest_t;

typedef enum {
    TB_SYMBOL_COMPANION_PATH_PENDING,
    TB_SYMBOL_COMPANION_PATH_READY,
    TB_SYMBOL_COMPANION_PATH_END,
    TB_SYMBOL_COMPANION_PATH_INVALID,
    TB_SYMBOL_COMPANION_PATH_UNSUPPORTED,
    TB_SYMBOL_COMPANION_PATH_RESOURCE
} TbSymbolCompanionPathStatus_e;

typedef struct {
    const unsigned char          *path;
    size_t                        bytes;
    size_t                        ordinal;
    size_t                        root;
    TbSymbolCompanionPathKind_e   kind;
} TbSymbolCompanionPathView_t;

/* Pure stationary iterator; caller owns/accounts all inputs, state and disjoint
 * output through discard. Origin is an explicitly captured absolute pathname,
 * never private backing/cwd. Roots are absolute POSIX paths. All path text is
 * validated before first candidate. No I/O, directory scan, admission or access
 * permission; drive existing supervised ACQUIRE explicitly for each candidate.
 * Repeated roots/paths retain their ordinals and do not imply shared identity. */
typedef struct {
    TbSymbolCompanionPathsRequest_t request;
    TbSymbolCompanionPathView_t     view;
    TbSymbolSourcePathText_t        parts[4];
    unsigned char                 *output;
    size_t                         capacity;
    size_t                         input;
    size_t                         cursor;
    size_t                         directory_bytes;
    size_t                         total;
    size_t                         ordinal;
    size_t                         part_count;
    size_t                         part;
    size_t                         output_bytes;
    unsigned                       phase;
    TbSymbolCompanionPathStatus_e  status;
    bool                           initialized;
} TbSymbolCompanionPaths_t;

TbSymbolCompanionPathStatus_e tb_symbol_companion_paths_init(TbSymbolCompanionPaths_t *paths,
    TbSymbolCompanionPathsRequest_t request, unsigned char *output, size_t capacity);
/* 1..256 byte/transition actions. Preserves repeated separators/dot components
 * and symlink spelling; roots constrain candidates, not OS path resolution.
 * No normalization, recursion, environment/tilde expansion or network fallback.
 * build_id is opaque binary; hex is lowercase, first byte selects directory.
 * One-byte IDs use an empty hex remainder followed by .debug. */
TbSymbolCompanionPathStatus_e tb_symbol_companion_paths_step(TbSymbolCompanionPaths_t *paths,
    size_t max_steps);
bool tb_symbol_companion_paths_view(const TbSymbolCompanionPaths_t *paths, TbSymbolCompanionPathView_t *view);
/* Explicitly consume READY. Old buffer/view becomes unavailable immediately. */
bool tb_symbol_companion_paths_next(TbSymbolCompanionPaths_t *paths);
void tb_symbol_companion_paths_discard(TbSymbolCompanionPaths_t *paths);

#endif // THE_BUGGER_SYMBOL_COMPANION_PATHS_H
