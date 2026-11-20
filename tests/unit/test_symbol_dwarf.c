#include "symbol/dwarf.h"
#include "../support/check.h"

#include <string.h>

static unsigned char image[128];
static TbSymbolDwarfSpan_t span;
static unsigned offset_bytes;
static unsigned header_bytes;
static size_t unit_bytes;

static void put(size_t at, unsigned width, uint64_t value)
{
    CHECK(at <= sizeof(image) && width <= sizeof(image) - at);
    for (unsigned i = 0; i < width; ++i) {
        unsigned index = span.byte_order == TB_SYMBOL_LITTLE_ENDIAN ? i : width - 1 - i;
        image[at + index] = (unsigned char)(value >> (8 * i));
    }
}


static unsigned length_bytes(void)
{
    return offset_bytes == 4 ? 4 : 12;
}


static void set_length(uint64_t payload)
{
    if (offset_bytes == 4) {
        put(0, 4, payload);
    } else {
        put(0, 4, UINT32_MAX);
        put(4, 8, payload);
    }
}


static void fixture(unsigned version, unsigned type, unsigned address_bytes)
{
    memset(image, 0, sizeof(image));
    unsigned prefix = length_bytes();
    unsigned base = version == 4 ? 3 + offset_bytes : 4 + offset_bytes;
    header_bytes = prefix + base + (type == 2 ? 8 + offset_bytes : 0);
    unit_bytes = header_bytes + 3;
    span.section_bytes = span.unit_offset + unit_bytes;
    set_length(unit_bytes - prefix);
    put(prefix, 2, version);
    uint64_t abbreviation = offset_bytes == 4 ? UINT64_C(0x12345678) : UINT64_C(0x123456789abcdef0);
    if (version == 4) {
        put(prefix + 2, offset_bytes, abbreviation);
        put(prefix + 2 + offset_bytes, 1, address_bytes);
    } else {
        put(prefix + 2, 1, type);
        put(prefix + 3, 1, address_bytes);
        put(prefix + 4, offset_bytes, abbreviation);
    }
    if (type == 2) {
        put(prefix + base, 8, UINT64_C(0x8877665544332211));
        put(prefix + base + 8, offset_bytes, header_bytes + 1);
    }
    image[header_bytes] = 1;
    image[header_bytes + 1] = 2;
}


static void rejected(TbSymbolDwarfSection_e section, size_t available, TbSymbolDwarfStatus_e expected)
{
    TbSymbolDwarfUnit_t unit;
    memset(&unit, 0xa5, sizeof(unit));
    unsigned char before[sizeof(unit)];
    memcpy(before, &unit, sizeof(unit));
    CHECK(tb_symbol_dwarf_unit(section, span, image, available, &unit) == expected);
    CHECK(!memcmp(before, &unit, sizeof(unit)));
}


static void rejected_length(size_t available)
{
    TbSymbolDwarfLength_t extent;
    memset(&extent, 0xa5, sizeof(extent));
    unsigned char before[sizeof(extent)];
    memcpy(before, &extent, sizeof(extent));
    CHECK(tb_symbol_dwarf_length(span, image, available, &extent) == TB_SYMBOL_DWARF_MALFORMED);
    CHECK(!memcmp(before, &extent, sizeof(extent)));
}


static void supported(unsigned version, unsigned type)
{
    const unsigned address_sizes[] = {1, 2, 3, 4, 8, 16, 255};
    TbSymbolDwarfSection_e section = version == 4 && type == 2 ? TB_SYMBOL_DWARF_TYPES
                                                             : TB_SYMBOL_DWARF_INFO;
    for (size_t i = 0; i < sizeof(address_sizes) / sizeof(address_sizes[0]); ++i) {
        fixture(version, type, address_sizes[i]);
        CHECK(header_bytes == (version == 4 ? (offset_bytes == 4 ? 11U : 23U)
                                                           : (offset_bytes == 4 ? 12U : 24U)) +
                              (type == 2 ? 8 + offset_bytes : 0));
        TbSymbolDwarfUnit_t unit;
        CHECK(tb_symbol_dwarf_unit(section, span, image, header_bytes, &unit) == TB_SYMBOL_DWARF_OK);
        CHECK(unit.version == version && unit.address_bytes == address_sizes[i]);
        CHECK(unit.extent.payload_bytes == unit_bytes - length_bytes());
        CHECK(unit.extent.total_bytes == unit_bytes && unit.extent.end_offset == span.section_bytes);
        CHECK(unit.extent.offset_bytes == offset_bytes && unit.extent.length_bytes == length_bytes());
        CHECK(unit.header_bytes == header_bytes && unit.unit_type == (version == 4 ? 0U : type));
        CHECK(unit.kind == (type == 2 ? TB_SYMBOL_DWARF_TYPE : version == 4
                               ? TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL : type == 3
                               ? TB_SYMBOL_DWARF_PARTIAL : TB_SYMBOL_DWARF_COMPILE));
        CHECK(unit.abbrev_offset == (offset_bytes == 4 ? UINT64_C(0x12345678)
                                                       : UINT64_C(0x123456789abcdef0)));
        CHECK(unit.has_signature == (type == 2));
        CHECK(unit.signature == (type == 2 ? UINT64_C(0x8877665544332211) : 0));
        CHECK(unit.type_offset == (type == 2 ? header_bytes + 1 : 0));
        for (size_t available = 0; available < header_bytes; ++available)
            rejected(section, available, TB_SYMBOL_DWARF_MALFORMED);
        unsigned char prefix[40];
        CHECK(header_bytes <= sizeof(prefix));
        memcpy(prefix, image, header_bytes);
        CHECK(tb_symbol_dwarf_unit(section, span, prefix, SIZE_MAX, &unit) == TB_SYMBOL_DWARF_OK);
    }
}


static void length_cases(void)
{
    fixture(5, 1, 8);
    TbSymbolDwarfLength_t extent;
    CHECK(tb_symbol_dwarf_length(span, image, length_bytes(), &extent) == TB_SYMBOL_DWARF_OK);
    for (unsigned available = 0; available < length_bytes(); ++available)
        rejected_length(available);
    --span.section_bytes;
    rejected_length(sizeof(image));
    span.unit_offset = UINT64_MAX;
    span.section_bytes = UINT64_MAX;
    rejected_length(sizeof(image));
    span.unit_offset = 0;
    if (offset_bytes == 8) {
        set_length(UINT64_MAX);
        rejected_length(sizeof(image));
        set_length(UINT64_MAX - 12);
        CHECK(tb_symbol_dwarf_length(span, image, 12, &extent) == TB_SYMBOL_DWARF_OK);
        CHECK(extent.total_bytes == UINT64_MAX && extent.end_offset == UINT64_MAX);
        span.unit_offset = 1;
        rejected_length(sizeof(image));
    } else {
        for (uint32_t value = UINT32_C(0xfffffff0); value < UINT32_MAX; ++value) {
            put(0, 4, value);
            rejected_length(sizeof(image));
        }
        put(0, 4, UINT32_C(0xffffffef));
        CHECK(tb_symbol_dwarf_length(span, image, 4, &extent) == TB_SYMBOL_DWARF_OK);
        CHECK(extent.offset_bytes == 4);
    }
    span.unit_offset = 7;
    fixture(5, 1, 8);
    set_length(0);
    CHECK(tb_symbol_dwarf_length(span, image, length_bytes(), &extent) == TB_SYMBOL_DWARF_OK);
    rejected(TB_SYMBOL_DWARF_INFO, sizeof(image), TB_SYMBOL_DWARF_MALFORMED);
}


static void invalid_headers(void)
{
    for (unsigned version = 4; version <= 5; ++version) {
        fixture(version, 1, 0);
        rejected(TB_SYMBOL_DWARF_INFO, sizeof(image), TB_SYMBOL_DWARF_MALFORMED);
        fixture(version, 1, 8);
        set_length(header_bytes - length_bytes());
        rejected(TB_SYMBOL_DWARF_INFO, sizeof(image), TB_SYMBOL_DWARF_MALFORMED);
        set_length(header_bytes - length_bytes() - 1);
        rejected(TB_SYMBOL_DWARF_INFO, sizeof(image), TB_SYMBOL_DWARF_MALFORMED);
        fixture(version, 2, 8);
        unsigned at = header_bytes - offset_bytes;
        const uint64_t invalid_offsets[] = {0, header_bytes - 1, unit_bytes, UINT64_MAX};
        for (size_t i = 0; i < sizeof(invalid_offsets) / sizeof(invalid_offsets[0]); ++i) {
            put(at, offset_bytes, invalid_offsets[i]);
            rejected(version == 4 ? TB_SYMBOL_DWARF_TYPES : TB_SYMBOL_DWARF_INFO,
                     sizeof(image), TB_SYMBOL_DWARF_MALFORMED);
        }
    }
    fixture(5, 1, 8);
    put(length_bytes(), 2, 0);
    rejected(TB_SYMBOL_DWARF_INFO, sizeof(image), TB_SYMBOL_DWARF_MALFORMED);
    put(length_bytes(), 2, 3);
    rejected(TB_SYMBOL_DWARF_INFO, sizeof(image), TB_SYMBOL_DWARF_UNSUPPORTED);
    put(length_bytes(), 2, 6);
    rejected(TB_SYMBOL_DWARF_INFO, sizeof(image), TB_SYMBOL_DWARF_UNSUPPORTED);
    const unsigned unsupported_types[] = {0, 4, 5, 6, 0x80, 0xff};
    for (size_t i = 0; i < sizeof(unsupported_types) / sizeof(unsupported_types[0]); ++i) {
        fixture(5, unsupported_types[i], 8);
        rejected(TB_SYMBOL_DWARF_INFO, sizeof(image), TB_SYMBOL_DWARF_UNSUPPORTED);
        TbSymbolDwarfLength_t extent;
        CHECK(tb_symbol_dwarf_length(span, image, sizeof(image), &extent) == TB_SYMBOL_DWARF_OK);
    }
    fixture(5, 2, 8);
    rejected(TB_SYMBOL_DWARF_TYPES, sizeof(image), TB_SYMBOL_DWARF_UNSUPPORTED);
}


static void mixed_units(void)
{
    unsigned char section[128];
    span.unit_offset = 0;
    offset_bytes = 4;
    fixture(4, 1, 8);
    size_t first = unit_bytes;
    memcpy(section, image, first);
    offset_bytes = 8;
    fixture(5, 3, 4);
    size_t second = unit_bytes;
    memcpy(section + first, image, second);
    span.section_bytes = first + second;
    TbSymbolDwarfUnit_t unit;
    CHECK(tb_symbol_dwarf_unit(TB_SYMBOL_DWARF_INFO, span, section, first + second, &unit) ==
          TB_SYMBOL_DWARF_OK);
    CHECK(unit.extent.offset_bytes == 4 && unit.address_bytes == 8 && unit.extent.end_offset == first);
    span.unit_offset = unit.extent.end_offset;
    CHECK(tb_symbol_dwarf_unit(TB_SYMBOL_DWARF_INFO, span, section + first, second, &unit) ==
          TB_SYMBOL_DWARF_OK);
    CHECK(unit.extent.offset_bytes == 8 && unit.address_bytes == 4 &&
          unit.extent.end_offset == span.section_bytes && unit.kind == TB_SYMBOL_DWARF_PARTIAL);
}


int main(void)
{
    for (unsigned order = 0; order < 2; ++order) {
        span.byte_order = order ? TB_SYMBOL_BIG_ENDIAN : TB_SYMBOL_LITTLE_ENDIAN;
        for (offset_bytes = 4; offset_bytes <= 8; offset_bytes += 4) {
            span.unit_offset = 7;
            supported(4, 1);
            supported(4, 2);
            supported(5, 1);
            supported(5, 2);
            supported(5, 3);
            length_cases();
            invalid_headers();
        }
        mixed_units();
    }
    TbSymbolDwarfLength_t extent;
    TbSymbolDwarfUnit_t unit;
    CHECK(tb_symbol_dwarf_length(span, NULL, 0, &extent) == TB_SYMBOL_DWARF_INVALID);
    CHECK(tb_symbol_dwarf_length(span, image, sizeof(image), NULL) == TB_SYMBOL_DWARF_INVALID);
    CHECK(tb_symbol_dwarf_unit((TbSymbolDwarfSection_e)9, span, image, sizeof(image), &unit) ==
          TB_SYMBOL_DWARF_INVALID);
    CHECK(tb_symbol_dwarf_unit(TB_SYMBOL_DWARF_INFO, span, image, sizeof(image), NULL) ==
          TB_SYMBOL_DWARF_INVALID);
    span.byte_order = (TbSymbolByteOrder_e)9;
    CHECK(tb_symbol_dwarf_length(span, image, sizeof(image), &extent) == TB_SYMBOL_DWARF_INVALID);
    puts("PASS bounded DWARF4/5 unit headers, mixed offset/address widths and exact contribution bounds");
    return 0;
}
