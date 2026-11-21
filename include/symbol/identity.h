#ifndef THE_BUGGER_SYMBOL_IDENTITY_H
#define THE_BUGGER_SYMBOL_IDENTITY_H

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    TB_SYMBOL_RECORD_INVALID,
    TB_SYMBOL_RECORD_DIE,
    TB_SYMBOL_RECORD_SYMTAB,
    TB_SYMBOL_RECORD_DYNSYM
} TbSymbolRecordKind_e;

/* Offsets are section-relative, never runtime addresses. A DIE retains its
 * unit's start as well as its own offset. ELF entries have unit_offset zero.
 * The artifact manager must assign a nonzero, nonreused content_id only after
 * admission; shared content requires complete byte equality, not just a digest.
 * Open-context identity is separate and belongs to consumer/prepared facts. */
typedef struct {
    uint64_t             content_id;
    TbSymbolRecordKind_e kind;
    uint64_t             section_index;
    uint64_t             unit_offset;
    uint64_t             record_offset;
} TbSymbolRecordId_t;

/* Shape checks only: neither function validates metadata or grants access. */
bool tb_symbol_record_valid(const TbSymbolRecordId_t *record);
bool tb_symbol_record_equal(const TbSymbolRecordId_t *left, const TbSymbolRecordId_t *right);

#endif // THE_BUGGER_SYMBOL_IDENTITY_H
