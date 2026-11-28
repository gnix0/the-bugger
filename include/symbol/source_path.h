#ifndef THE_BUGGER_SYMBOL_SOURCE_PATH_H
#define THE_BUGGER_SYMBOL_SOURCE_PATH_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    const unsigned char *data;
    size_t               bytes;
} TbSymbolSourcePathText_t;

typedef struct {
    TbSymbolSourcePathText_t from;
    TbSymbolSourcePathText_t to;
} TbSymbolSourcePathRule_t;

typedef struct {
    TbSymbolSourcePathText_t        file;
    TbSymbolSourcePathText_t        directory;
    TbSymbolSourcePathText_t        compilation;
    TbSymbolSourcePathText_t        image_directory;
    const TbSymbolSourcePathRule_t *rules;
    size_t                          rule_count;
    size_t                          selected_rule;
    bool                            directory_current;
    bool                            directory_unknown;
} TbSymbolSourcePathRequest_t;

typedef enum {
    TB_SYMBOL_SOURCE_PATH_PENDING,
    TB_SYMBOL_SOURCE_PATH_COMPLETE,
    TB_SYMBOL_SOURCE_PATH_DEPENDENCY,
    TB_SYMBOL_SOURCE_PATH_AMBIGUOUS,
    TB_SYMBOL_SOURCE_PATH_INVALID,
    TB_SYMBOL_SOURCE_PATH_RESOURCE
} TbSymbolSourcePathStatus_e;

typedef struct {
    const unsigned char *recorded;
    const unsigned char *local;
    size_t               recorded_bytes;
    size_t               local_bytes;
    size_t               matching_rules;
    size_t               first_rule;
    size_t               second_rule;
    size_t               selected_rule;
    bool                 mapped;
    bool                 local_available;
} TbSymbolSourcePathView_t;

/* POSIX byte-path profile, no filesystem queries/normalization/tilde expansion.
 * All text has a complete explicit length; caller owns/accounts inputs, rules,
 * stationary state and disjoint candidate buffers through discard. Compilation
 * context is chosen from exact native provenance (v5 directory zero, or the
 * explicit CU comp_dir). Relative compilation paths require an explicitly
 * captured absolute linked-image directory, never private backing or current cwd.
 * SIZE_MAX selected_rule means no explicit choice: multiple component-prefix
 * matches are ambiguous, even with equal destinations. No longest/first fallback.
 * capacities include the terminator and are caller/consumer limits, not defaults. */
typedef struct {
    TbSymbolSourcePathRequest_t  request;
    TbSymbolSourcePathView_t     view;
    TbSymbolSourcePathText_t     parts[4];
    unsigned char               *recorded;
    unsigned char               *local;
    size_t                       recorded_capacity;
    size_t                       local_capacity;
    size_t                       part_count;
    size_t                       part;
    size_t                       cursor;
    size_t                       rule;
    size_t                       rule_cursor;
    size_t                       output_cursor;
    size_t                       suffix;
    uint64_t                     progress;
    unsigned                     phase;
    TbSymbolSourcePathStatus_e   status;
    bool                         rule_matches;
    bool                         need_image;
    bool                         joining;
    bool                         initialized;
} TbSymbolSourcePath_t;

TbSymbolSourcePathStatus_e tb_symbol_source_path_init(TbSymbolSourcePath_t *path,
    TbSymbolSourcePathRequest_t request, unsigned char *recorded, size_t recorded_capacity,
    unsigned char *local, size_t local_capacity);
/* At most 256 byte/rule/transition actions. No full-string scan in a step.
 * Preserve repeated separators, dot/dot-dot, symlinks, spaces and non-ASCII bytes.
 * Reject embedded NUL/CR/LF; never truncate into an actionable argument. Outer
 * jobs retain revisions, cancellation, interests and absolute deadlines. */
TbSymbolSourcePathStatus_e tb_symbol_source_path_step(TbSymbolSourcePath_t *path, size_t max_steps);
/* COMPLETE/DEPENDENCY/AMBIGUOUS only: full recorded path remains inspectable.
 * Only COMPLETE has a local candidate. Neither a path nor a mapping establishes
 * file existence, source-byte correspondence, runtime mapping or action authority. */
bool tb_symbol_source_path_view(const TbSymbolSourcePath_t *path, TbSymbolSourcePathView_t *view);
void tb_symbol_source_path_discard(TbSymbolSourcePath_t *path);

#endif // THE_BUGGER_SYMBOL_SOURCE_PATH_H
