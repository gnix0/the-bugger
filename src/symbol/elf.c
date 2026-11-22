#include "symbol/elf.h"

#include <elf.h>
#include <string.h>

static bool header_valid(const TbSymbolElfHeader_t *header)
{
    return header && (header->address_bytes == 4 || header->address_bytes == 8) &&
           (header->byte_order == TB_SYMBOL_LITTLE_ENDIAN || header->byte_order == TB_SYMBOL_BIG_ENDIAN) &&
           (header->object_type == ET_EXEC || header->object_type == ET_DYN);
}


static bool fields(const void *bytes, size_t length, TbSymbolByteOrder_e order,
                   const unsigned *widths, size_t count, uint64_t *values)
{
    TbSymbolReader_t reader;
    if (tb_symbol_reader_init(&reader, bytes, length, order) != TB_SYMBOL_READ_OK)
        return false;

    for (size_t i = 0; i < count; ++i) {
        if (tb_symbol_reader_uint(&reader, widths[i], &values[i]) != TB_SYMBOL_READ_OK)
            return false;
    }

    return true;
}


static bool alignment_valid(uint64_t alignment)
{
    return !alignment || !(alignment & (alignment - 1));
}


static bool address_range(const TbSymbolElfHeader_t *header, uint64_t address, uint64_t bytes)
{
    uint64_t maximum = header->address_bytes == 4 ? UINT32_MAX : UINT64_MAX;

    return address <= maximum && (!bytes || bytes - 1 <= maximum - address);
}


TbSymbolElfStatus_e tb_symbol_elf_header(uint64_t file_bytes, const void *bytes, size_t length,
                                        TbSymbolElfHeader_t *header)
{
    if (!bytes || !header)
        return TB_SYMBOL_ELF_INVALID;

    const unsigned char *ident = bytes;
    if (length < EI_NIDENT || memcmp(ident, ELFMAG, SELFMAG) != 0)
        return TB_SYMBOL_ELF_MALFORMED;

    if (ident[EI_CLASS] != ELFCLASS32 && ident[EI_CLASS] != ELFCLASS64)
        return ident[EI_CLASS] ? TB_SYMBOL_ELF_UNSUPPORTED : TB_SYMBOL_ELF_MALFORMED;

    if (ident[EI_DATA] != ELFDATA2LSB && ident[EI_DATA] != ELFDATA2MSB)
        return ident[EI_DATA] ? TB_SYMBOL_ELF_UNSUPPORTED : TB_SYMBOL_ELF_MALFORMED;

    if (ident[EI_VERSION] != EV_CURRENT)
        return ident[EI_VERSION] ? TB_SYMBOL_ELF_UNSUPPORTED : TB_SYMBOL_ELF_MALFORMED;

    unsigned width = ident[EI_CLASS] == ELFCLASS32 ? 4 : 8;
    TbSymbolByteOrder_e order = ident[EI_DATA] == ELFDATA2LSB
                                  ? TB_SYMBOL_LITTLE_ENDIAN : TB_SYMBOL_BIG_ENDIAN;
    unsigned widths[] = {2, 2, 4, width, width, width, 4, 2, 2, 2, 2, 2, 2};
    uint64_t values[13];
    if (!fields(ident + EI_NIDENT, length - EI_NIDENT, order, widths, 13, values))
        return TB_SYMBOL_ELF_MALFORMED;

    if (values[2] != EV_CURRENT)
        return values[2] ? TB_SYMBOL_ELF_UNSUPPORTED : TB_SYMBOL_ELF_MALFORMED;

    if (values[0] != ET_EXEC && values[0] != ET_DYN)
        return TB_SYMBOL_ELF_UNSUPPORTED;

    uint64_t minimum = width == 4 ? 52 : 64;
    if (values[7] < minimum || !tb_symbol_range(0, values[7], file_bytes))
        return TB_SYMBOL_ELF_MALFORMED;

    if (values[7] != minimum)
        return TB_SYMBOL_ELF_UNSUPPORTED;

    if ((values[11] >= SHN_LORESERVE) ||
        (values[12] >= SHN_LORESERVE && values[12] != SHN_XINDEX)) {
        return TB_SYMBOL_ELF_MALFORMED;
    }

    *header = (TbSymbolElfHeader_t) {
        .byte_order     = order,
        .address_bytes  = width,
        .os_abi         = ident[EI_OSABI],
        .abi_version    = ident[EI_ABIVERSION],
        .object_type    = (uint16_t)values[0],
        .machine        = (uint16_t)values[1],
        .flags          = (uint32_t)values[6],
        .file_bytes     = file_bytes,
        .entry          = values[3],
        .program_offset = values[4],
        .section_offset = values[5],
        .header_bytes   = (uint16_t)values[7],
        .program_stride = (uint16_t)values[8],
        .program_count  = (uint16_t)values[9],
        .section_stride = (uint16_t)values[10],
        .section_count  = (uint16_t)values[11],
        .section_names  = (uint16_t)values[12]
    };

    return TB_SYMBOL_ELF_OK;
}


bool tb_symbol_elf_needs_zero(const TbSymbolElfHeader_t *header)
{
    return header_valid(header) && ((!header->section_count && header->section_offset) ||
           header->section_names == SHN_XINDEX || header->program_count == PN_XNUM);
}


static TbSymbolElfStatus_e table_valid(uint64_t extent, TbSymbolElfTable_t table, uint64_t expected)
{
    if (!table.count)
        return table.offset ? TB_SYMBOL_ELF_MALFORMED : TB_SYMBOL_ELF_OK;

    if (!table.offset || table.stride < expected)
        return TB_SYMBOL_ELF_MALFORMED;

    uint64_t bytes;
    if (!tb_symbol_multiply(table.stride, table.count, &bytes) ||
        !tb_symbol_range(table.offset, bytes, extent)) {
        return TB_SYMBOL_ELF_MALFORMED;
    }

    return table.stride == expected ? TB_SYMBOL_ELF_OK : TB_SYMBOL_ELF_UNSUPPORTED;
}


TbSymbolElfStatus_e tb_symbol_elf_layout(const TbSymbolElfHeader_t *header,
                                        const TbSymbolElfSection_t *zero, TbSymbolElfLayout_t *layout)
{
    if (!header_valid(header) || !layout)
        return TB_SYMBOL_ELF_INVALID;

    if (tb_symbol_elf_needs_zero(header) && (!zero || !header->section_offset))
        return TB_SYMBOL_ELF_MALFORMED;

    if (zero && (zero->type != SHT_NULL || zero->name_offset || zero->flags || zero->address ||
                 zero->offset || zero->alignment || zero->entry_bytes)) {
        return TB_SYMBOL_ELF_MALFORMED;
    }

    TbSymbolElfLayout_t candidate = {
        .programs = {
            .offset = header->program_offset,
            .count  = header->program_count,
            .stride = header->program_stride
        },
        .sections = {
            .offset = header->section_offset,
            .count  = header->section_count,
            .stride = header->section_stride
        },
        .section_names = header->section_names
    };

    if (header->section_offset && !header->section_count) {
        if (zero->bytes < SHN_LORESERVE)
            return TB_SYMBOL_ELF_MALFORMED;

        candidate.sections.count = zero->bytes;
    } else if (zero && zero->bytes) {
        return TB_SYMBOL_ELF_MALFORMED;
    }

    if (header->program_count == PN_XNUM) {
        if (zero->info < PN_XNUM)
            return TB_SYMBOL_ELF_MALFORMED;

        candidate.programs.count = zero->info;
    } else if (zero && zero->info) {
        return TB_SYMBOL_ELF_MALFORMED;
    }

    if (header->section_names == SHN_XINDEX) {
        if (zero->link < SHN_LORESERVE)
            return TB_SYMBOL_ELF_MALFORMED;

        candidate.section_names = zero->link;
    } else if (zero && zero->link) {
        return TB_SYMBOL_ELF_MALFORMED;
    }

    if (candidate.section_names && candidate.section_names >= candidate.sections.count)
        return TB_SYMBOL_ELF_MALFORMED;

    TbSymbolElfStatus_e status = table_valid(header->file_bytes, candidate.sections,
                                            header->address_bytes == 4 ? 40 : 64);
    if (status == TB_SYMBOL_ELF_OK)
        status = table_valid(header->file_bytes, candidate.programs, header->address_bytes == 4 ? 32 : 56);

    if (status != TB_SYMBOL_ELF_OK)
        return status;

    *layout = candidate;

    return TB_SYMBOL_ELF_OK;
}


bool tb_symbol_elf_entry(const TbSymbolElfTable_t *table, uint64_t index, uint64_t *offset)
{
    uint64_t relative;
    uint64_t result;
    if (!table || !offset || !table->stride || index >= table->count ||
        !tb_symbol_multiply(index, table->stride, &relative) ||
        !tb_symbol_add(table->offset, relative, &result)) {
        return false;
    }

    *offset = result;

    return true;
}


TbSymbolElfStatus_e tb_symbol_elf_section(const TbSymbolElfHeader_t *header, const void *bytes,
                                         size_t length, TbSymbolElfSection_t *section)
{
    if (!header_valid(header) || !bytes || !section)
        return TB_SYMBOL_ELF_INVALID;

    unsigned width = header->address_bytes;
    unsigned widths[] = {4, 4, width, width, width, width, 4, 4, width, width};
    uint64_t values[10];
    if (!fields(bytes, length, header->byte_order, widths, 10, values))
        return TB_SYMBOL_ELF_MALFORMED;

    TbSymbolElfSection_t candidate = {
        .name_offset = (uint32_t)values[0],
        .type        = (uint32_t)values[1],
        .flags       = values[2],
        .address     = values[3],
        .offset      = values[4],
        .bytes       = values[5],
        .link        = (uint32_t)values[6],
        .info        = (uint32_t)values[7],
        .alignment   = values[8],
        .entry_bytes = values[9]
    };

    if (candidate.type != SHT_NULL) {
        if (!alignment_valid(candidate.alignment) ||
            (candidate.alignment && candidate.address % candidate.alignment) ||
            (candidate.type != SHT_NOBITS &&
             !tb_symbol_range(candidate.offset, candidate.bytes, header->file_bytes)) ||
            ((candidate.flags & SHF_ALLOC) && !address_range(header, candidate.address, candidate.bytes))) {
            return TB_SYMBOL_ELF_MALFORMED;
        }
    }
    *section = candidate;

    return TB_SYMBOL_ELF_OK;
}


TbSymbolElfStatus_e tb_symbol_elf_program(const TbSymbolElfHeader_t *header, const void *bytes,
                                         size_t length, TbSymbolElfProgram_t *program)
{
    if (!header_valid(header) || !bytes || !program)
        return TB_SYMBOL_ELF_INVALID;

    unsigned width = header->address_bytes;
    unsigned widths32[] = {4, 4, 4, 4, 4, 4, 4, 4};
    unsigned widths64[] = {4, 4, 8, 8, 8, 8, 8, 8};
    uint64_t values[8];
    if (!fields(bytes, length, header->byte_order, width == 4 ? widths32 : widths64, 8, values))
        return TB_SYMBOL_ELF_MALFORMED;

    TbSymbolElfProgram_t candidate = {
        .type             = (uint32_t)values[0],
        .flags            = (uint32_t)values[width == 4 ? 6 : 1],
        .offset           = values[width == 4 ? 1 : 2],
        .virtual_address  = values[width == 4 ? 2 : 3],
        .physical_address = values[width == 4 ? 3 : 4],
        .file_bytes       = values[width == 4 ? 4 : 5],
        .memory_bytes     = values[width == 4 ? 5 : 6],
        .alignment        = values[7]
    };

    if (candidate.type != PT_NULL &&
        !tb_symbol_range(candidate.offset, candidate.file_bytes, header->file_bytes)) {
        return TB_SYMBOL_ELF_MALFORMED;
    }

    if (candidate.type == PT_LOAD || candidate.type == PT_TLS) {
        if (candidate.file_bytes > candidate.memory_bytes || !alignment_valid(candidate.alignment) ||
            !address_range(header, candidate.virtual_address, candidate.memory_bytes) ||
            (candidate.type == PT_LOAD && candidate.alignment &&
             candidate.offset % candidate.alignment != candidate.virtual_address % candidate.alignment)) {
            return TB_SYMBOL_ELF_MALFORMED;
        }
    }
    *program = candidate;

    return TB_SYMBOL_ELF_OK;
}


TbSymbolElfStatus_e tb_symbol_elf_symbol(const TbSymbolElfHeader_t *header, const void *bytes,
                                        size_t length, TbSymbolElfSymbol_t *symbol)
{
    if (!header_valid(header) || !bytes || !symbol)
        return TB_SYMBOL_ELF_INVALID;

    unsigned width = header->address_bytes;
    unsigned widths32[] = {4, 4, 4, 1, 1, 2};
    unsigned widths64[] = {4, 1, 1, 2, 8, 8};
    uint64_t values[6];
    if (!fields(bytes, length, header->byte_order, width == 4 ? widths32 : widths64, 6, values))
        return TB_SYMBOL_ELF_MALFORMED;

    *symbol = (TbSymbolElfSymbol_t) {
        .name_offset   = (uint32_t)values[0],
        .info          = (uint8_t)values[width == 4 ? 3 : 1],
        .other         = (uint8_t)values[width == 4 ? 4 : 2],
        .section_index = (uint16_t)values[width == 4 ? 5 : 3],
        .value         = values[width == 4 ? 1 : 4],
        .bytes         = values[width == 4 ? 2 : 5]
    };

    return TB_SYMBOL_ELF_OK;
}
