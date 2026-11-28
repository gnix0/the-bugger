#ifndef THE_BUGGER_SYMBOL_ABBREV_H
#define THE_BUGGER_SYMBOL_ABBREV_H

#include "symbol/forms.h"
#include "symbol/sections.h"

typedef struct tb_symbol_abbrev TbSymbolAbbrev_t;

typedef struct {
    uint64_t section_index;
    uint64_t table_offset;
    unsigned version;
    int      descriptor;
} TbSymbolAbbrevSelection_t;

typedef struct {
    uint64_t name;
    uint64_t form;
    uint64_t offset;
    uint64_t implicit_offset;
    uint64_t implicit_bytes;
    int64_t  implicit_value;
    bool     implicit_fits;
} TbSymbolAbbrevAttribute_t;

typedef struct {
    uint64_t code;
    uint64_t tag;
    uint64_t offset;
    uint64_t end_offset;
    uint64_t first_attribute;
    uint64_t attribute_count;
    bool     children;
} TbSymbolAbbrevEntry_t;

typedef struct {
    const TbSymbolAbbrevEntry_t     *entries;
    const TbSymbolAbbrevAttribute_t *attributes;
    uint64_t                        count;
    uint64_t                        attribute_count;
    uint64_t                        section_index;
    uint64_t                        table_offset;
    uint64_t                        end_offset;
    unsigned                        version;
    TbSymbolStorageInfo_t            backing;
} TbSymbolAbbrevView_t;

typedef enum {
    TB_SYMBOL_ABBREV_NONE,
    TB_SYMBOL_ABBREV_REQUEST,
    TB_SYMBOL_ABBREV_SECTION,
    TB_SYMBOL_ABBREV_RANGE,
    TB_SYMBOL_ABBREV_READ,
    TB_SYMBOL_ABBREV_TRUNCATED,
    TB_SYMBOL_ABBREV_IDENTIFIER,
    TB_SYMBOL_ABBREV_FORM,
    TB_SYMBOL_ABBREV_CHILDREN,
    TB_SYMBOL_ABBREV_TERMINATOR,
    TB_SYMBOL_ABBREV_RESOURCE,
    TB_SYMBOL_ABBREV_RECONSTRUCTION,
    TB_SYMBOL_ABBREV_CODE_CONFLICT
} TbSymbolAbbrevFailure_e;

typedef struct {
    TbSymbolAbbrevFailure_e reason;
    uint64_t                offset;
    uint64_t                related_offset;
    TbSymbolFileFailure_t    read;
    int                     system_error;
} TbSymbolAbbrevFailure_t;

typedef enum {
    TB_SYMBOL_ABBREV_FOUND,
    TB_SYMBOL_ABBREV_MISSING,
    TB_SYMBOL_ABBREV_UNAVAILABLE,
    TB_SYMBOL_ABBREV_INVALID
} TbSymbolAbbrevLookup_e;

/* One explicitly selected table in a retained abbreviation section. Borrows
 * directory, descriptor and content lease until destruction. Creation performs
 * no I/O. All owned state, reader scratch, entries, attributes and lookup order
 * require the caller's checked construction account; no default is selected.
 * version is 4 or 5. The caller establishes the selected section relationship. */
TbSymbolAbbrev_t *tb_symbol_abbrev_create(TbSymbolSectionsView_t directory,
                                         TbSymbolAbbrevSelection_t selection,
                                         TbSymbolBudget_t *construction);
TbSymbolOutcome_e tb_symbol_abbrev_step(TbSymbolAbbrev_t *abbrev, size_t max_bytes);
/* Complete selected table only, borrowed until destruction. Physical entry and
 * attribute order remains intact. Numeric code lookup is validated independently.
 * Unknown forms or identifiers exceeding 64-bit interpretation are unsupported,
 * never guessed/truncated. Implicit constants retain complete encoded spans even
 * when no int64 value is available. Those spans are section-relative and their
 * fields are meaningful only for FORM_IMPLICIT_CONST. No DIE/type/reference
 * semantics, frontend guarantee, artifact-wide completeness or runtime authority
 * follows. Cancellation and malformed/resource failures expose no prefix. */
bool tb_symbol_abbrev_view(const TbSymbolAbbrev_t *abbrev, TbSymbolAbbrevView_t *view);
TbSymbolAbbrevLookup_e tb_symbol_abbrev_find(const TbSymbolAbbrev_t *abbrev, uint64_t code,
                                           const TbSymbolAbbrevEntry_t **entry);
TbSymbolAbbrevFailure_t tb_symbol_abbrev_failure(const TbSymbolAbbrev_t *abbrev);
uint64_t tb_symbol_abbrev_progress(const TbSymbolAbbrev_t *abbrev);
void tb_symbol_abbrev_destroy(TbSymbolAbbrev_t *abbrev);

#endif // THE_BUGGER_SYMBOL_ABBREV_H
