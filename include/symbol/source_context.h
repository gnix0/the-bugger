#ifndef THE_BUGGER_SYMBOL_SOURCE_CONTEXT_H
#define THE_BUGGER_SYMBOL_SOURCE_CONTEXT_H

#include "symbol/source_file.h"
#include "symbol/source_path.h"
#include "symbol/text.h"

typedef struct {
    TbSymbolSourcePathView_t   path;
    TbSymbolSourcePathStatus_e status;
} TbSymbolSourceCandidate_t;

typedef enum {
    TB_SYMBOL_SOURCE_CONTEXT_PENDING,
    TB_SYMBOL_SOURCE_CONTEXT_READY,
    TB_SYMBOL_SOURCE_CONTEXT_AMBIGUOUS,
    TB_SYMBOL_SOURCE_CONTEXT_DEPENDENCY,
    TB_SYMBOL_SOURCE_CONTEXT_INVALID
} TbSymbolSourceContextStatus_e;

typedef struct {
    TbSymbolSourceFileView_t  file;
    TbSymbolSourceCandidate_t candidates[2];
    TbSymbolTextView_t        unit_directory;
    size_t                    count;
    size_t                    selected;
    bool                      unit_directory_available;
} TbSymbolSourceContextView_t;

/* Compare complete paths derived from the SAME selected file/directory using
 * (0) its default compilation context (v5 directory zero, otherwise comp_dir)
 * and (1) the exact root comp_dir, when separately supplied for v5. Each path
 * must already be a completed source_path result, with its string dependencies
 * retained. The caller owns/accounts all state, strings and path buffers.
 * READY covers only this captured candidate inventory, not an unexamined root
 * attribute. Full-reconciliation consumers must retain its lookup outcome and
 * supply all relevant found alternatives. No I/O, allocation or normalization. */
typedef struct {
    TbSymbolSourceContextView_t   view;
    TbSymbolSourceContextStatus_e status;
    size_t                        cursor;
    unsigned                      phase;
    bool                          initialized;
} TbSymbolSourceContext_t;

/* SIZE_MAX choice requests agreement of full recorded AND local path bytes.
 * Distinct contexts producing equal paths retain both provenances. An explicit
 * choice selects exactly that candidate, never a nearest/first fallback.
 * unit_directory, when supplied, is the selected physical unit root's complete
 * DW_AT_comp_dir TextView. alternate requires it and a v5 default context.
 * Missing/unsupported default context stays qualified in its candidate; explicit
 * unit choice can use a supported alternate. Wrong roots/choices preserve state. */
TbSymbolSourceContextStatus_e tb_symbol_source_context_init(TbSymbolSourceContext_t *context,
    TbSymbolSourceFileView_t file, TbSymbolSourceCandidate_t primary,
    const TbSymbolSourceCandidate_t *alternate, const TbSymbolTextView_t *unit_directory, size_t choice);
/* 1..256 byte-pair/transition actions. Context mismatch is AMBIGUOUS even if a
 * substitution happens to map the differing recorded paths to one local path. */
TbSymbolSourceContextStatus_e tb_symbol_source_context_step(TbSymbolSourceContext_t *context,
    size_t max_steps);
/* READY/AMBIGUOUS/DEPENDENCY only. selected is SIZE_MAX without an accepted
 * complete candidate. Ambiguous/missing context permits no prepared Source path. */
bool tb_symbol_source_context_view(const TbSymbolSourceContext_t *context, TbSymbolSourceContextView_t *view);
void tb_symbol_source_context_discard(TbSymbolSourceContext_t *context);

#endif // THE_BUGGER_SYMBOL_SOURCE_CONTEXT_H
