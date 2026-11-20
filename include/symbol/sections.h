#ifndef THE_BUGGER_SYMBOL_SECTIONS_H
#define THE_BUGGER_SYMBOL_SECTIONS_H

#include "symbol/elf.h"
#include "symbol/file.h"

typedef struct tb_symbol_sections TbSymbolSections_t;

typedef enum {
    TB_SYMBOL_SECTION_NAMES_ABSENT,
    TB_SYMBOL_SECTION_NAMES_AVAILABLE,
    TB_SYMBOL_SECTION_NAMES_COMPRESSED
} TbSymbolSectionNames_e;

typedef struct {
    TbSymbolElfSection_t header;
    uint64_t             name_offset;
    uint64_t             name_bytes;
    bool                 has_name;
} TbSymbolSectionEntry_t;

typedef struct {
    const TbSymbolElfHeader_t    *header;
    const TbSymbolElfLayout_t    *layout;
    const TbSymbolSectionEntry_t *entries;
    uint64_t                     count;
    TbSymbolSectionNames_e       names;
    TbSymbolStorageInfo_t         backing;
} TbSymbolSectionsView_t;

typedef enum {
    TB_SYMBOL_SECTIONS_NONE,
    TB_SYMBOL_SECTIONS_REQUEST,
    TB_SYMBOL_SECTIONS_READ,
    TB_SYMBOL_SECTIONS_HEADER,
    TB_SYMBOL_SECTIONS_LAYOUT,
    TB_SYMBOL_SECTIONS_RECORD,
    TB_SYMBOL_SECTIONS_OVERLAP,
    TB_SYMBOL_SECTIONS_NAMES,
    TB_SYMBOL_SECTIONS_RESOURCE
} TbSymbolSectionsFailure_e;

typedef struct {
    TbSymbolSectionsFailure_e reason;
    uint64_t                 section_index;
    TbSymbolFileFailure_t    read;
    int                      system_error;
} TbSymbolSectionsFailure_t;

/* Helper-local directory construction over exact frozen backing. Borrows the
 * descriptor/content lease until destruction. All owned arrays, bookkeeping and
 * scratch are charged to the required caller budget; construction performs no
 * I/O. No production cache default or full-file buffer is introduced. */
TbSymbolSections_t *tb_symbol_sections_create(TbSymbolStorageInfo_t expected, int descriptor,
                                             TbSymbolBudget_t *construction);
/* One bounded construction action or reader step. max_bytes is 64..65536,
 * accommodating a fixed ELF header. Sorting yields after each constant-size
 * heap action. The outer operation owns cancellation, deadlines and generations. */
TbSymbolOutcome_e tb_symbol_sections_step(TbSymbolSections_t *sections, size_t max_bytes);
/* Complete directory only, borrowed until destroy. Checks header/table framing,
 * section records, file-backed overlap and supported section-name dependencies.
 * Compressed name-table bytes remain unexamined with explicit coverage status.
 * Missing names do not prove absence of DWARF. Program entries, symbol tables,
 * other cross-section links and processor/OS semantics are separate scopes.
 * This helper cache does not own independent frontend publication guarantees. */
bool tb_symbol_sections_view(const TbSymbolSections_t *sections, TbSymbolSectionsView_t *view);
TbSymbolSectionsFailure_t tb_symbol_sections_failure(const TbSymbolSections_t *sections);
uint64_t tb_symbol_sections_progress(const TbSymbolSections_t *sections);
void tb_symbol_sections_destroy(TbSymbolSections_t *sections);

#endif // THE_BUGGER_SYMBOL_SECTIONS_H
