#ifndef THE_BUGGER_SYMBOL_ELF_H
#define THE_BUGGER_SYMBOL_ELF_H

#include "symbol/reader.h"

typedef enum {
    TB_SYMBOL_ELF_OK,
    TB_SYMBOL_ELF_INVALID,
    TB_SYMBOL_ELF_MALFORMED,
    TB_SYMBOL_ELF_UNSUPPORTED
} TbSymbolElfStatus_e;

typedef struct {
    TbSymbolByteOrder_e byte_order;
    unsigned            address_bytes;
    uint8_t             os_abi;
    uint8_t             abi_version;
    uint16_t            object_type;
    uint16_t            machine;
    uint32_t            flags;
    uint64_t            file_bytes;
    uint64_t            entry;
    uint64_t            program_offset;
    uint64_t            section_offset;
    uint16_t            header_bytes;
    uint16_t            program_stride;
    uint16_t            program_count;
    uint16_t            section_stride;
    uint16_t            section_count;
    uint16_t            section_names;
} TbSymbolElfHeader_t;

typedef struct {
    uint32_t name_offset;
    uint32_t type;
    uint64_t flags;
    uint64_t address;
    uint64_t offset;
    uint64_t bytes;
    uint32_t link;
    uint32_t info;
    uint64_t alignment;
    uint64_t entry_bytes;
} TbSymbolElfSection_t;

typedef struct {
    uint64_t offset;
    uint64_t count;
    uint64_t stride;
} TbSymbolElfTable_t;

typedef struct {
    TbSymbolElfTable_t programs;
    TbSymbolElfTable_t sections;
    uint64_t           section_names;
} TbSymbolElfLayout_t;

typedef struct {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t virtual_address;
    uint64_t physical_address;
    uint64_t file_bytes;
    uint64_t memory_bytes;
    uint64_t alignment;
} TbSymbolElfProgram_t;

typedef struct {
    uint32_t name_offset;
    uint8_t  info;
    uint8_t  other;
    uint16_t section_index;
    uint64_t value;
    uint64_t bytes;
} TbSymbolElfSymbol_t;

/* Allocation-free fixed-record decoding, with explicit target class/byte order.
 * Input spans borrow complete record prefixes, never host ELF structs. Each call
 * examines at most 64 bytes; extra input is ignored. Failure leaves output intact.
 * INVALID denotes bad API arguments, MALFORMED supported but invalid structure,
 * and UNSUPPORTED an unavailable format/version. No I/O, inventory publication,
 * physical identity, semantic storage/code claim or runtime authority is created.
 * Retain header/layout outputs unchanged; all subsequent bytes must come from
 * the same immutable content. Their ownership belongs to the caller. Header
 * decoding does not validate table extents. Check section-zero prefix bounds
 * before fetching it; iterate tables only after successful layout validation. */
TbSymbolElfStatus_e tb_symbol_elf_header(uint64_t file_bytes, const void *bytes, size_t length,
                                        TbSymbolElfHeader_t *header);
/* Resolves escaped counts/indexes using section zero when required. Normal
 * counts need no section-zero input. Checks complete table extents, not entries,
 * string contents, cross-section links, overlaps or processor/OS semantics.
 * Larger entry schemas are unsupported rather than guessed. */
bool tb_symbol_elf_needs_zero(const TbSymbolElfHeader_t *header);
TbSymbolElfStatus_e tb_symbol_elf_layout(const TbSymbolElfHeader_t *header,
                                        const TbSymbolElfSection_t *zero, TbSymbolElfLayout_t *layout);
/* Checked offsets for a previously validated table; index is never an address. */
bool tb_symbol_elf_entry(const TbSymbolElfTable_t *table, uint64_t index, uint64_t *offset);
/* NULL sections carry no payload; section zero's size/link/info may encode
 * escaped counts. NOBITS carries memory extent, not file bytes. Compression,
 * names and other section-dependent formats require separate validation. */
TbSymbolElfStatus_e tb_symbol_elf_section(const TbSymbolElfHeader_t *header, const void *bytes,
                                         size_t length, TbSymbolElfSection_t *section);
TbSymbolElfStatus_e tb_symbol_elf_program(const TbSymbolElfHeader_t *header, const void *bytes,
                                         size_t length, TbSymbolElfProgram_t *program);
/* Raw fields only: SHN_XINDEX remains unresolved until its associated SHNDX
 * entry is validated. TLS, ABS, COMMON, undefined and processor-specific values
 * must never be treated as ordinary locations by virtue of decoding success. */
TbSymbolElfStatus_e tb_symbol_elf_symbol(const TbSymbolElfHeader_t *header, const void *bytes,
                                        size_t length, TbSymbolElfSymbol_t *symbol);

#endif // THE_BUGGER_SYMBOL_ELF_H
