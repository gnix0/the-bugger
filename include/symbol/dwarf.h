#ifndef THE_BUGGER_SYMBOL_DWARF_H
#define THE_BUGGER_SYMBOL_DWARF_H

#include "symbol/reader.h"

typedef enum {
    TB_SYMBOL_DWARF_OK,
    TB_SYMBOL_DWARF_INVALID,
    TB_SYMBOL_DWARF_MALFORMED,
    TB_SYMBOL_DWARF_UNSUPPORTED
} TbSymbolDwarfStatus_e;

typedef enum {
    TB_SYMBOL_DWARF_INFO,
    TB_SYMBOL_DWARF_TYPES
} TbSymbolDwarfSection_e;

typedef enum {
    TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL,
    TB_SYMBOL_DWARF_COMPILE,
    TB_SYMBOL_DWARF_PARTIAL,
    TB_SYMBOL_DWARF_TYPE
} TbSymbolDwarfUnitKind_e;

typedef struct {
    uint64_t            section_bytes;
    uint64_t            unit_offset;
    TbSymbolByteOrder_e byte_order;
} TbSymbolDwarfSpan_t;

typedef struct {
    uint64_t payload_bytes;
    uint64_t total_bytes;
    uint64_t end_offset;
    unsigned length_bytes;
    unsigned offset_bytes;
} TbSymbolDwarfLength_t;

typedef struct {
    TbSymbolDwarfLength_t   extent;
    TbSymbolDwarfUnitKind_e kind;
    uint16_t               version;
    uint8_t                unit_type;
    uint8_t                address_bytes;
    unsigned               header_bytes;
    uint64_t               abbrev_offset;
    uint64_t               signature;
    uint64_t               type_offset;
    bool                   has_signature;
} TbSymbolDwarfUnit_t;

/* Allocation-free prefix decoding over one retained section. bytes starts at
 * unit_offset; section_bytes describes the full section, not the prefix buffer.
 * No I/O, pointer casts to input structures or body/relationship validation.
 * Failure leaves outputs unchanged. Offset format and address size are separate.
 * Initial-length success establishes bounded framing only, even for an empty
 * contribution; at most 12 bytes are inspected. Reserved encodings are rejected. */
TbSymbolDwarfStatus_e tb_symbol_dwarf_length(TbSymbolDwarfSpan_t span, const void *bytes, size_t length,
                                           TbSymbolDwarfLength_t *extent);
/* At most 40 header bytes. Supports non-split DWARF4 info/types and DWARF5 info
 * compile/partial/type headers. A DWARF4 info header cannot distinguish compile
 * from partial until its root DIE is validated. unit_type is raw DWARF5 only,
 * zero for DWARF4. Any nonzero raw address size is retained; this grants no
 * numeric address decoding, segmented addressing or location support.
 * Type offsets must lie after the header within the same unit, but do not yet
 * prove a DIE boundary or type. Signatures never replace physical identity.
 * Abbreviation offsets remain unresolved dependencies. Unsupported unit headers
 * expose no guessed fields; separately valid framing is not a decoded unit. */
TbSymbolDwarfStatus_e tb_symbol_dwarf_unit(TbSymbolDwarfSection_e section, TbSymbolDwarfSpan_t span,
                                         const void *bytes, size_t length, TbSymbolDwarfUnit_t *unit);

#endif // THE_BUGGER_SYMBOL_DWARF_H
