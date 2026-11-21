#include "symbol/elf.h"
#include "../support/check.h"

#include <elf.h>
#include <string.h>

static unsigned address_bytes;
static TbSymbolByteOrder_e byte_order;

static void put(unsigned char *bytes, size_t offset, unsigned width, uint64_t value)
{
    for (unsigned i = 0; i < width; ++i) {
        unsigned index = byte_order == TB_SYMBOL_LITTLE_ENDIAN ? i : width - 1 - i;
        bytes[offset + index] = (unsigned char)(value >> (8 * i));
    }
}


static size_t header_fixture(unsigned char *bytes)
{
    memset(bytes, 0, 64);
    memcpy(bytes, ELFMAG, SELFMAG);
    bytes[EI_CLASS] = address_bytes == 4 ? ELFCLASS32 : ELFCLASS64;
    bytes[EI_DATA] = byte_order == TB_SYMBOL_LITTLE_ENDIAN ? ELFDATA2LSB : ELFDATA2MSB;
    bytes[EI_VERSION] = EV_CURRENT;
    bytes[EI_OSABI] = 255;
    bytes[EI_ABIVERSION] = 19;
    bytes[EI_PAD] = 0xa5;
    put(bytes, 16, 2, ET_EXEC);
    put(bytes, 18, 2, EM_RISCV);
    put(bytes, 20, 4, EV_CURRENT);
    put(bytes, 24, address_bytes, address_bytes == 4 ? UINT32_C(0x89abcdef) : UINT64_C(0x123456789abcdef0));
    if (address_bytes == 4) {
        put(bytes, 28, 4, 1024);
        put(bytes, 32, 4, 4096);
        put(bytes, 36, 4, 0x81234567);
        put(bytes, 40, 2, 52);
        put(bytes, 42, 2, 32);
        put(bytes, 44, 2, 2);
        put(bytes, 46, 2, 40);
        put(bytes, 48, 2, 3);
        put(bytes, 50, 2, 2);
        return 52;
    }
    put(bytes, 32, 8, 1024);
    put(bytes, 40, 8, 4096);
    put(bytes, 48, 4, 0x81234567);
    put(bytes, 52, 2, 64);
    put(bytes, 54, 2, 56);
    put(bytes, 56, 2, 2);
    put(bytes, 58, 2, 64);
    put(bytes, 60, 2, 3);
    put(bytes, 62, 2, 2);
    return 64;
}


static TbSymbolElfHeader_t header_checks(void)
{
    unsigned char bytes[64];
    size_t length = header_fixture(bytes);
    TbSymbolElfHeader_t header;
    memset(&header, 0xa5, sizeof(header));
    unsigned char original[sizeof(header)];
    memcpy(original, &header, sizeof(header));
    for (size_t i = 0; i < length; ++i) {
        CHECK(tb_symbol_elf_header(32 * 1024 * 1024, bytes, i, &header) == TB_SYMBOL_ELF_MALFORMED);
        CHECK(!memcmp(original, &header, sizeof(header)));
    }
    CHECK(tb_symbol_elf_header(length - 1, bytes, length, &header) == TB_SYMBOL_ELF_MALFORMED);
    CHECK(!memcmp(original, &header, sizeof(header)));
    CHECK(tb_symbol_elf_header(32 * 1024 * 1024, bytes, length, &header) == TB_SYMBOL_ELF_OK);
    CHECK(header.address_bytes == address_bytes && header.byte_order == byte_order);
    CHECK(header.os_abi == 255 && header.abi_version == 19 && header.machine == EM_RISCV);
    CHECK(header.object_type == ET_EXEC && header.flags == UINT32_C(0x81234567));
    CHECK(header.entry == (address_bytes == 4 ? UINT32_C(0x89abcdef) : UINT64_C(0x123456789abcdef0)));
    CHECK(header.program_offset == 1024 && header.section_offset == 4096);
    CHECK(header.program_count == 2 && header.section_count == 3 && header.section_names == 2);
    CHECK(header.header_bytes == length && !tb_symbol_elf_needs_zero(&header));
    TbSymbolElfHeader_t saved = header;
    put(bytes, 16, 2, ET_REL);
    CHECK(tb_symbol_elf_header(header.file_bytes, bytes, length, &header) == TB_SYMBOL_ELF_UNSUPPORTED);
    CHECK(!memcmp(&saved, &header, sizeof(header)));
    put(bytes, 16, 2, ET_DYN);
    CHECK(tb_symbol_elf_header(header.file_bytes, bytes, length, &header) == TB_SYMBOL_ELF_OK);
    CHECK(header.object_type == ET_DYN);
    bytes[0] = 0;
    CHECK(tb_symbol_elf_header(header.file_bytes, bytes, length, &header) == TB_SYMBOL_ELF_MALFORMED);
    CHECK(header_fixture(bytes) == length);
    unsigned positions[] = {EI_CLASS, EI_DATA, EI_VERSION};
    for (size_t i = 0; i < sizeof(positions) / sizeof(positions[0]); ++i) {
        unsigned char value = bytes[positions[i]];
        bytes[positions[i]] = 0;
        CHECK(tb_symbol_elf_header(header.file_bytes, bytes, length, &header) == TB_SYMBOL_ELF_MALFORMED);
        bytes[positions[i]] = 250;
        CHECK(tb_symbol_elf_header(header.file_bytes, bytes, length, &header) == TB_SYMBOL_ELF_UNSUPPORTED);
        bytes[positions[i]] = value;
    }
    put(bytes, 20, 4, 0);
    CHECK(tb_symbol_elf_header(header.file_bytes, bytes, length, &header) == TB_SYMBOL_ELF_MALFORMED);
    put(bytes, 20, 4, 2);
    CHECK(tb_symbol_elf_header(header.file_bytes, bytes, length, &header) == TB_SYMBOL_ELF_UNSUPPORTED);
    CHECK(header_fixture(bytes) == length);
    size_t size_field = address_bytes == 4 ? 40 : 52;
    put(bytes, size_field, 2, length - 1);
    CHECK(tb_symbol_elf_header(header.file_bytes, bytes, length, &header) == TB_SYMBOL_ELF_MALFORMED);
    put(bytes, size_field, 2, length + 1);
    CHECK(tb_symbol_elf_header(header.file_bytes, bytes, length, &header) == TB_SYMBOL_ELF_UNSUPPORTED);
    CHECK(header_fixture(bytes) == length);
    put(bytes, address_bytes == 4 ? 48 : 60, 2, SHN_LORESERVE);
    CHECK(tb_symbol_elf_header(header.file_bytes, bytes, length, &header) == TB_SYMBOL_ELF_MALFORMED);
    CHECK(header_fixture(bytes) == length);
    put(bytes, address_bytes == 4 ? 50 : 62, 2, SHN_ABS);
    CHECK(tb_symbol_elf_header(header.file_bytes, bytes, length, &header) == TB_SYMBOL_ELF_MALFORMED);
    return saved;
}


static void layout_checks(TbSymbolElfHeader_t header)
{
    TbSymbolElfLayout_t layout;
    CHECK(tb_symbol_elf_layout(&header, NULL, &layout) == TB_SYMBOL_ELF_OK);
    CHECK(layout.sections.count == 3 && layout.programs.count == 2 && layout.section_names == 2);
    uint64_t offset = UINT64_MAX;
    CHECK(tb_symbol_elf_entry(&layout.sections, 2, &offset));
    CHECK(offset == UINT64_C(4096) + 2 * header.section_stride);
    CHECK(!tb_symbol_elf_entry(&layout.sections, 3, &offset));
    CHECK(offset == UINT64_C(4096) + 2 * header.section_stride);
    TbSymbolElfHeader_t original = header;
    header.section_stride--;
    CHECK(tb_symbol_elf_layout(&header, NULL, &layout) == TB_SYMBOL_ELF_MALFORMED);
    header.section_stride += 2;
    CHECK(tb_symbol_elf_layout(&header, NULL, &layout) == TB_SYMBOL_ELF_UNSUPPORTED);
    header = original;
    header.program_stride++;
    CHECK(tb_symbol_elf_layout(&header, NULL, &layout) == TB_SYMBOL_ELF_UNSUPPORTED);
    header = original;
    header.section_names = 3;
    CHECK(tb_symbol_elf_layout(&header, NULL, &layout) == TB_SYMBOL_ELF_MALFORMED);
    header = original;
    header.section_offset = header.file_bytes - header.section_stride;
    CHECK(tb_symbol_elf_layout(&header, NULL, &layout) == TB_SYMBOL_ELF_MALFORMED);
    header = original;
    header.program_offset = UINT64_MAX;
    CHECK(tb_symbol_elf_layout(&header, NULL, &layout) == TB_SYMBOL_ELF_MALFORMED);
    header = original;
    header.section_offset = 4 * 1024 * 1024;
    header.section_count = 0;
    header.section_names = SHN_XINDEX;
    header.program_count = PN_XNUM;
    CHECK(tb_symbol_elf_needs_zero(&header));
    CHECK(tb_symbol_elf_layout(&header, NULL, &layout) == TB_SYMBOL_ELF_MALFORMED);
    TbSymbolElfSection_t zero = {
        .bytes = SHN_LORESERVE + 2,
        .link  = SHN_LORESERVE + 1,
        .info  = PN_XNUM + 1
    };
    CHECK(tb_symbol_elf_layout(&header, &zero, &layout) == TB_SYMBOL_ELF_OK);
    CHECK(layout.sections.count == zero.bytes && layout.programs.count == zero.info);
    CHECK(layout.section_names == zero.link);
    CHECK(tb_symbol_elf_entry(&layout.sections, zero.bytes - 1, &offset));
    CHECK(offset == header.section_offset + (zero.bytes - 1) * header.section_stride);
    TbSymbolElfLayout_t preserved = layout;
    zero.bytes = UINT64_MAX;
    CHECK(tb_symbol_elf_layout(&header, &zero, &layout) == TB_SYMBOL_ELF_MALFORMED);
    CHECK(!memcmp(&preserved, &layout, sizeof(layout)));
    zero.bytes = SHN_LORESERVE - 1;
    CHECK(tb_symbol_elf_layout(&header, &zero, &layout) == TB_SYMBOL_ELF_MALFORMED);
    zero.bytes = SHN_LORESERVE + 2;
    zero.link = SHN_LORESERVE - 1;
    CHECK(tb_symbol_elf_layout(&header, &zero, &layout) == TB_SYMBOL_ELF_MALFORMED);
    zero.link = SHN_LORESERVE + 1;
    zero.info = PN_XNUM - 1;
    CHECK(tb_symbol_elf_layout(&header, &zero, &layout) == TB_SYMBOL_ELF_MALFORMED);
    zero.info = PN_XNUM + 1;
    zero.type = SHT_PROGBITS;
    CHECK(tb_symbol_elf_layout(&header, &zero, &layout) == TB_SYMBOL_ELF_MALFORMED);
    zero.type = SHT_NULL;
    zero.address = 1;
    CHECK(tb_symbol_elf_layout(&header, &zero, &layout) == TB_SYMBOL_ELF_MALFORMED);
    header = original;
    header.section_count = 0;
    header.section_names = 0;
    header.section_offset = 0;
    header.program_count = 0;
    header.program_offset = 0;
    header.section_stride = 0;
    header.program_stride = 0;
    CHECK(!tb_symbol_elf_needs_zero(&header));
    CHECK(tb_symbol_elf_layout(&header, NULL, &layout) == TB_SYMBOL_ELF_OK);
    CHECK(!layout.sections.count && !layout.programs.count && !layout.section_names);
    CHECK(!tb_symbol_elf_entry(&layout.sections, 0, &offset));
    TbSymbolElfTable_t overflowing = {
        .offset = UINT64_MAX,
        .count  = UINT64_MAX,
        .stride = 64
    };
    CHECK(!tb_symbol_elf_entry(&overflowing, 1, &offset));
    overflowing.offset = 0;
    CHECK(!tb_symbol_elf_entry(&overflowing, UINT64_MAX - 1, &offset));
}


static void section_checks(const TbSymbolElfHeader_t *header)
{
    unsigned char bytes[64] = {0};
    size_t length = address_bytes == 4 ? 40 : 64;
    size_t address_at = address_bytes == 4 ? 12 : 16;
    size_t offset_at = address_bytes == 4 ? 16 : 24;
    size_t size_at = address_bytes == 4 ? 20 : 32;
    size_t alignment_at = address_bytes == 4 ? 32 : 48;
    put(bytes, 0, 4, 9);
    put(bytes, 4, 4, SHT_PROGBITS);
    put(bytes, 8, address_bytes, SHF_ALLOC | SHF_WRITE);
    put(bytes, address_at, address_bytes, 0x8000);
    put(bytes, offset_at, address_bytes, 0x1000);
    put(bytes, size_at, address_bytes, 0x20);
    put(bytes, address_bytes == 4 ? 24 : 40, 4, 2);
    put(bytes, address_bytes == 4 ? 28 : 44, 4, 1);
    put(bytes, alignment_at, address_bytes, 0x10);
    put(bytes, address_bytes == 4 ? 36 : 56, address_bytes, 8);
    TbSymbolElfSection_t section;
    memset(&section, 0xa5, sizeof(section));
    unsigned char original[sizeof(section)];
    memcpy(original, &section, sizeof(section));
    for (size_t i = 0; i < length; ++i) {
        CHECK(tb_symbol_elf_section(header, bytes, i, &section) == TB_SYMBOL_ELF_MALFORMED);
        CHECK(!memcmp(original, &section, sizeof(section)));
    }
    CHECK(tb_symbol_elf_section(header, bytes, length, &section) == TB_SYMBOL_ELF_OK);
    CHECK(section.name_offset == 9 && section.type == SHT_PROGBITS);
    CHECK(section.flags == (SHF_ALLOC | SHF_WRITE) && section.address == 0x8000);
    CHECK(section.offset == 0x1000 && section.bytes == 0x20 && section.link == 2 && section.info == 1);
    CHECK(section.alignment == 0x10 && section.entry_bytes == 8);
    put(bytes, offset_at, address_bytes, header->file_bytes - 1);
    CHECK(tb_symbol_elf_section(header, bytes, length, &section) == TB_SYMBOL_ELF_MALFORMED);
    put(bytes, 4, 4, SHT_NOBITS);
    put(bytes, offset_at, address_bytes, address_bytes == 4 ? UINT32_MAX : UINT64_MAX);
    CHECK(tb_symbol_elf_section(header, bytes, length, &section) == TB_SYMBOL_ELF_OK);
    put(bytes, alignment_at, address_bytes, 3);
    CHECK(tb_symbol_elf_section(header, bytes, length, &section) == TB_SYMBOL_ELF_MALFORMED);
    put(bytes, alignment_at, address_bytes, 1);
    put(bytes, address_at, address_bytes, address_bytes == 4 ? UINT32_MAX - 16 : UINT64_MAX - 16);
    put(bytes, size_at, address_bytes, 17);
    CHECK(tb_symbol_elf_section(header, bytes, length, &section) == TB_SYMBOL_ELF_OK);
    put(bytes, size_at, address_bytes, 18);
    CHECK(tb_symbol_elf_section(header, bytes, length, &section) == TB_SYMBOL_ELF_MALFORMED);
    put(bytes, 4, 4, SHT_NULL);
    CHECK(tb_symbol_elf_section(header, bytes, length, &section) == TB_SYMBOL_ELF_OK);
    put(bytes, 4, 4, SHT_PROGBITS);
    put(bytes, 8, address_bytes, SHF_COMPRESSED);
    put(bytes, address_at, address_bytes, 0);
    put(bytes, offset_at, address_bytes, 1024);
    CHECK(tb_symbol_elf_section(header, bytes, length, &section) == TB_SYMBOL_ELF_OK);
    CHECK(section.flags == SHF_COMPRESSED);
}


static void program_checks(const TbSymbolElfHeader_t *header)
{
    unsigned char bytes[56] = {0};
    size_t length = address_bytes == 4 ? 32 : 56;
    size_t offset_at = address_bytes == 4 ? 4 : 8;
    size_t address_at = address_bytes == 4 ? 8 : 16;
    size_t file_at = address_bytes == 4 ? 16 : 32;
    size_t memory_at = address_bytes == 4 ? 20 : 40;
    size_t alignment_at = address_bytes == 4 ? 28 : 48;
    put(bytes, 0, 4, PT_LOAD);
    put(bytes, address_bytes == 4 ? 24 : 4, 4, PF_R | PF_X);
    put(bytes, offset_at, address_bytes, 0x2000);
    put(bytes, address_at, address_bytes, 0x8000);
    put(bytes, address_bytes == 4 ? 12 : 24, address_bytes, 0xa000);
    put(bytes, file_at, address_bytes, 32);
    put(bytes, memory_at, address_bytes, 64);
    put(bytes, alignment_at, address_bytes, 16);
    TbSymbolElfProgram_t program;
    memset(&program, 0xa5, sizeof(program));
    unsigned char original[sizeof(program)];
    memcpy(original, &program, sizeof(program));
    for (size_t i = 0; i < length; ++i) {
        CHECK(tb_symbol_elf_program(header, bytes, i, &program) == TB_SYMBOL_ELF_MALFORMED);
        CHECK(!memcmp(original, &program, sizeof(program)));
    }
    CHECK(tb_symbol_elf_program(header, bytes, length, &program) == TB_SYMBOL_ELF_OK);
    CHECK(program.type == PT_LOAD && program.flags == (PF_R | PF_X));
    CHECK(program.offset == 0x2000 && program.virtual_address == 0x8000 &&
          program.physical_address == 0xa000);
    CHECK(program.file_bytes == 32 && program.memory_bytes == 64 && program.alignment == 16);
    put(bytes, file_at, address_bytes, 65);
    CHECK(tb_symbol_elf_program(header, bytes, length, &program) == TB_SYMBOL_ELF_MALFORMED);
    put(bytes, file_at, address_bytes, 32);
    put(bytes, alignment_at, address_bytes, 3);
    CHECK(tb_symbol_elf_program(header, bytes, length, &program) == TB_SYMBOL_ELF_MALFORMED);
    put(bytes, alignment_at, address_bytes, 16);
    put(bytes, offset_at, address_bytes, 0x2001);
    CHECK(tb_symbol_elf_program(header, bytes, length, &program) == TB_SYMBOL_ELF_MALFORMED);
    put(bytes, 0, 4, PT_TLS);
    CHECK(tb_symbol_elf_program(header, bytes, length, &program) == TB_SYMBOL_ELF_OK);
    put(bytes, 0, 4, PT_LOAD);
    put(bytes, offset_at, address_bytes, header->file_bytes - 1);
    CHECK(tb_symbol_elf_program(header, bytes, length, &program) == TB_SYMBOL_ELF_MALFORMED);
    put(bytes, offset_at, address_bytes, 0);
    put(bytes, alignment_at, address_bytes, 1);
    put(bytes, address_at, address_bytes, address_bytes == 4 ? UINT32_MAX : UINT64_MAX);
    put(bytes, file_at, address_bytes, 0);
    put(bytes, memory_at, address_bytes, 1);
    CHECK(tb_symbol_elf_program(header, bytes, length, &program) == TB_SYMBOL_ELF_OK);
    put(bytes, memory_at, address_bytes, 2);
    CHECK(tb_symbol_elf_program(header, bytes, length, &program) == TB_SYMBOL_ELF_MALFORMED);
    put(bytes, 0, 4, PT_NULL);
    CHECK(tb_symbol_elf_program(header, bytes, length, &program) == TB_SYMBOL_ELF_OK);
}


static void symbol_checks(const TbSymbolElfHeader_t *header)
{
    unsigned char bytes[24] = {0};
    size_t length = address_bytes == 4 ? 16 : 24;
    size_t index_at = address_bytes == 4 ? 14 : 6;
    put(bytes, 0, 4, 0x12345678);
    put(bytes, address_bytes == 4 ? 12 : 4, 1, ELF64_ST_INFO(STB_WEAK, STT_TLS));
    put(bytes, address_bytes == 4 ? 13 : 5, 1, 0xab);
    put(bytes, index_at, 2, SHN_XINDEX);
    put(bytes, address_bytes == 4 ? 4 : 8, address_bytes,
        address_bytes == 4 ? UINT32_C(0xaabbccdd) : UINT64_C(0x8899aabbccddeeff));
    put(bytes, address_bytes == 4 ? 8 : 16, address_bytes, 0x87654321);
    TbSymbolElfSymbol_t symbol;
    memset(&symbol, 0xa5, sizeof(symbol));
    unsigned char original[sizeof(symbol)];
    memcpy(original, &symbol, sizeof(symbol));
    for (size_t i = 0; i < length; ++i) {
        CHECK(tb_symbol_elf_symbol(header, bytes, i, &symbol) == TB_SYMBOL_ELF_MALFORMED);
        CHECK(!memcmp(original, &symbol, sizeof(symbol)));
    }
    uint16_t indexes[] = {SHN_UNDEF, SHN_ABS, SHN_COMMON, SHN_XINDEX, 1};
    for (size_t i = 0; i < sizeof(indexes) / sizeof(indexes[0]); ++i) {
        put(bytes, index_at, 2, indexes[i]);
        CHECK(tb_symbol_elf_symbol(header, bytes, length, &symbol) == TB_SYMBOL_ELF_OK);
        CHECK(symbol.name_offset == 0x12345678 && symbol.other == 0xab);
        CHECK(symbol.info == ELF64_ST_INFO(STB_WEAK, STT_TLS) && symbol.section_index == indexes[i]);
        CHECK(symbol.value == (address_bytes == 4 ? UINT32_C(0xaabbccdd) : UINT64_C(0x8899aabbccddeeff)));
        CHECK(symbol.bytes == 0x87654321);
    }
}


int main(void)
{
    for (unsigned width = 4; width <= 8; width += 4) {
        address_bytes = width;
        for (unsigned order = 0; order < 2; ++order) {
            byte_order = order ? TB_SYMBOL_BIG_ENDIAN : TB_SYMBOL_LITTLE_ENDIAN;
            TbSymbolElfHeader_t header = header_checks();
            layout_checks(header);
            section_checks(&header);
            program_checks(&header);
            symbol_checks(&header);
        }
    }
    CHECK(tb_symbol_elf_header(0, NULL, 0, NULL) == TB_SYMBOL_ELF_INVALID);
    CHECK(tb_symbol_elf_layout(NULL, NULL, NULL) == TB_SYMBOL_ELF_INVALID);
    CHECK(!tb_symbol_elf_needs_zero(NULL) && !tb_symbol_elf_entry(NULL, 0, NULL));
    puts("PASS bounded ELF32/64 records, both byte orders, extended table layout and malformed boundaries");
    return 0;
}
