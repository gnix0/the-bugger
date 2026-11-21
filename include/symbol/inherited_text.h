#ifndef THE_BUGGER_SYMBOL_INHERITED_TEXT_H
#define THE_BUGGER_SYMBOL_INHERITED_TEXT_H

#include "symbol/inherit.h"
#include "symbol/string_order.h"
#include "symbol/text.h"

typedef struct tb_symbol_inherited_text TbSymbolInheritedText_t;

typedef enum {
    TB_SYMBOL_INHERITED_TEXT_PENDING,
    TB_SYMBOL_INHERITED_TEXT_SOURCE,
    TB_SYMBOL_INHERITED_TEXT_FOUND,
    TB_SYMBOL_INHERITED_TEXT_ABSENT,
    TB_SYMBOL_INHERITED_TEXT_CONFLICT,
    TB_SYMBOL_INHERITED_TEXT_DEPENDENCY,
    TB_SYMBOL_INHERITED_TEXT_UNSUPPORTED,
    TB_SYMBOL_INHERITED_TEXT_MALFORMED,
    TB_SYMBOL_INHERITED_TEXT_RESOURCE,
    TB_SYMBOL_INHERITED_TEXT_OPERATIONAL,
    TB_SYMBOL_INHERITED_TEXT_INVALID
} TbSymbolInheritedTextStatus_e;

typedef struct {
    uint64_t name;
    uint64_t string_section;
    uint64_t line_string_section;
    uint64_t offset_section;
    int      descriptor;
} TbSymbolInheritedTextSelection_t;

typedef enum {
    TB_SYMBOL_INHERITED_TEXT_OWNER_SOURCE,
    TB_SYMBOL_INHERITED_TEXT_VALUE_SOURCE
} TbSymbolInheritedTextNeedKind_e;

typedef struct {
    TbSymbolInheritedTextNeedKind_e kind;
    TbSymbolInheritNeed_t           source;
} TbSymbolInheritedTextNeed_t;

/* Local owned validation summary. The parent retains all borrowed source/context
 * dependencies; freeing a child decoder's scratch does not discard this claim.
 * No independent frontend evidence or authority beyond the parent's lifetime. */
typedef struct {
    size_t                    node_index;
    TbSymbolAttributeResult_t selection;
    TbSymbolTextView_t        text;
    TbSymbolUnitEntry_t       unit;
    TbSymbolReferenceTarget_t unit_root;
    uint64_t                  root_first_attribute;
    uint64_t                  root_attribute_count;
} TbSymbolInheritedTextFact_t;

typedef struct {
    TbSymbolReferenceTarget_t          selected;
    const TbSymbolInheritedTextFact_t *facts;
    size_t                             count;
} TbSymbolInheritedTextView_t;

typedef enum {
    TB_SYMBOL_INHERITED_TEXT_NONE,
    TB_SYMBOL_INHERITED_TEXT_REQUEST,
    TB_SYMBOL_INHERITED_TEXT_OWNERS,
    TB_SYMBOL_INHERITED_TEXT_ALLOCATION,
    TB_SYMBOL_INHERITED_TEXT_FORM,
    TB_SYMBOL_INHERITED_TEXT_VALUE,
    TB_SYMBOL_INHERITED_TEXT_ADOPTION,
    TB_SYMBOL_INHERITED_TEXT_ORDER,
    TB_SYMBOL_INHERITED_TEXT_DISAGREEMENT
} TbSymbolInheritedTextFailure_e;

typedef struct {
    TbSymbolInheritedTextFailure_e reason;
    size_t                         node_index;
    size_t                         other_node_index;
    TbSymbolInheritFailure_t       owner;
    TbSymbolTextFailure_t          text;
    TbSymbolStringOrderFailure_t   order;
} TbSymbolInheritedTextFailure_t;

/* Complete graph and directory only. name is standard name/linkage name.
 * UINT64_MAX means an unavailable string/line-string/offset section. Borrows the
 * graph, directory, descriptor/content lease and EVERY supplied complete source
 * view through destruction. The caller pins/accounts those scopes independently;
 * all parent-owned metadata and child scratch use the required construction account.
 * No constructor I/O, discovery or callback. Outer job owns request/consumer
 * identities, captured revisions, cancellation and monotonic deadlines. */
TbSymbolInheritedText_t *tb_symbol_inherited_text_create(TbSymbolSectionsView_t directory,
                                                         TbSymbolGraphView_t graph,
                                                         TbSymbolInheritedTextSelection_t selection,
                                                         TbSymbolBudget_t *construction);
/* One bounded owner, text, adoption or comparison action; 64..65536 bytes.
 * Invalid arguments preserve the existing operation. SOURCE does no work until
 * explicit supply; external completions must pass outer-job revision checks. */
TbSymbolInheritedTextStatus_e tb_symbol_inherited_text_step(TbSymbolInheritedText_t *text, size_t max_bytes);
bool tb_symbol_inherited_text_need(const TbSymbolInheritedText_t *text, TbSymbolInheritedTextNeed_t *need);
/* Wrong source context leaves the request intact; NULL reports a dependency.
 * Does no I/O. Ownership is never inferred from a matching name or pathname. */
TbSymbolInheritedTextStatus_e tb_symbol_inherited_text_source(TbSymbolInheritedText_t *text,
                                                             const TbSymbolUnitsView_t *units,
                                                             const TbSymbolDiesView_t *tree,
                                                             uint64_t unit_index);
/* FOUND only, borrowed until destruction. All owners have valid string-class
 * values and complete equal byte sequences. Distinct records remain distinct;
 * facts[0] is a value representative, not a canonical entity. Empty FOUND differs
 * from ABSENT. No qualified-context agreement, frontend ownership, source-file
 * correspondence, runtime binding or action authority follows. */
bool tb_symbol_inherited_text_view(const TbSymbolInheritedText_t *text, TbSymbolInheritedTextView_t *view);
TbSymbolInheritedTextFailure_t tb_symbol_inherited_text_failure(const TbSymbolInheritedText_t *text);
uint64_t tb_symbol_inherited_text_progress(const TbSymbolInheritedText_t *text);
void tb_symbol_inherited_text_destroy(TbSymbolInheritedText_t *text);

#endif // THE_BUGGER_SYMBOL_INHERITED_TEXT_H
