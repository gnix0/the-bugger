#ifndef THE_BUGGER_SYMBOL_INDEXED_TABLES_H
#define THE_BUGGER_SYMBOL_INDEXED_TABLES_H

#include "symbol/dwarf.h"

typedef enum {
    TB_SYMBOL_TABLE_ADDRESSES,
    TB_SYMBOL_TABLE_RANGES
} TbSymbolIndexedTableKind_e;

typedef struct {
    TbSymbolIndexedTableKind_e kind;
    TbSymbolDwarfLength_t      extent;
    uint64_t                   table_offset;
    uint64_t                   entries_offset;
    uint64_t                   lists_offset;
    uint64_t                   count;
    TbSymbolByteOrder_e        byte_order;
    unsigned                   address_bytes;
    unsigned                   segment_bytes;
} TbSymbolIndexedTable_t;

typedef struct {
    TbSymbolIndexedTable_t table;
    uint64_t               index;
    uint64_t               offset;
    uint64_t               value;
    uint64_t               target_offset;
} TbSymbolIndexedEntry_t;

/* Fixed DWARF5 .debug_addr/.debug_rnglists prefix, at most 20 bytes.
 * Caller supplies exact same-content contribution-boundary evidence and retains
 * the frozen scope. No allocation, I/O, section selection or CU association.
 * count means address entries or range-offset entries according to kind.
 * entries_offset is the corresponding CU base; lists_offset is the beginning
 * of list bytes for RANGES, the contribution end for ADDRESSES. Unsupported
 * nonzero addressing widths/selectors retain framing, not numeric support.
 * Failed outputs remain unchanged. */
TbSymbolDwarfStatus_e tb_symbol_indexed_table_header(TbSymbolIndexedTableKind_e kind,
    TbSymbolDwarfSpan_t span, const void *bytes, size_t length, TbSymbolIndexedTable_t *table);
/* Checked section-relative offset for one index, never a read request itself.
 * Header must be an unchanged successful result from the prefix decoder. */
bool tb_symbol_indexed_table_offset(const TbSymbolIndexedTable_t *table, uint64_t index,
                                     uint64_t *offset);
/* bytes starts at the exact returned entry offset in the same frozen section.
 * At most 8 bytes inspected. Numeric addresses support widths 1/2/4/8 without
 * selectors; range offsets retain their independent DWARF32/64 representation.
 * value is the encoded address/offset. target_offset is UINT64_MAX for addresses;
 * for range offsets it is relative to the section and lies in the list area.
 * A list target still needs separate physical-boundary and full-list proof.
 * No relocation, runtime correspondence, access authority or frontend guarantee. */
TbSymbolDwarfStatus_e tb_symbol_indexed_table_entry(TbSymbolIndexedTable_t table, uint64_t index,
    const void *bytes, size_t length, TbSymbolIndexedEntry_t *entry);

#endif // THE_BUGGER_SYMBOL_INDEXED_TABLES_H
