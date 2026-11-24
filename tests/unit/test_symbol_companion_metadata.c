#include "symbol/companion_metadata.h"
#include "../support/check.h"

#include <stdio.h>
#include <string.h>

static TbSymbolCompanionScope_t scope(size_t bytes, TbSymbolByteOrder_e order)
{
    return (TbSymbolCompanionScope_t) {
        .backing = {
            .key = {
                .device = 1,
                .inode  = 2
            },
            .bytes = 1024 * 1024
        },
        .section_index = 7,
        .bytes         = bytes,
        .byte_order    = order
    };
}


static void number(unsigned char *bytes, uint32_t value, TbSymbolByteOrder_e order)
{
    for (unsigned i = 0; i < 4; ++i)
        bytes[order == TB_SYMBOL_LITTLE_ENDIAN ? i : 3 - i] = (unsigned char)(value >> (8U * i));
}


static TbSymbolCompanionMetadataStatus_e link_parse(TbSymbolDebuglink_t *link,
    const unsigned char *bytes, size_t length, size_t chunk)
{
    TbSymbolCompanionMetadataStatus_e status = link->status;
    size_t cursor = 0;
    while (cursor < length && status == TB_SYMBOL_COMPANION_METADATA_PENDING) {
        TbSymbolDebuglinkView_t view;
        CHECK(!tb_symbol_debuglink_view(link, &view));
        size_t count = length - cursor < chunk ? length - cursor : chunk;
        size_t consumed;
        status = tb_symbol_debuglink_feed(link, bytes + cursor, count, &consumed);
        CHECK(consumed <= count && consumed <= 256);
        cursor += consumed;
    }
    if (status == TB_SYMBOL_COMPANION_METADATA_PENDING)
        status = tb_symbol_debuglink_finish(link);
    return status;
}


static void links(TbSymbolByteOrder_e order)
{
    unsigned char bytes[32];
    unsigned char name[16];
    TbSymbolDebuglink_t link;
    TbSymbolDebuglinkView_t view;
    for (size_t length = 1; length < 9; ++length) {
        memset(bytes, 0xa5, sizeof(bytes));
        memset(bytes, 'a', length);
        bytes[length] = 0;
        size_t crc_offset = (length + 4) & ~(size_t)3;
        number(bytes + crc_offset, UINT32_C(0x12345678), order);
        for (size_t chunk = 1; chunk < 20; ++chunk) {
            CHECK(tb_symbol_debuglink_init(&link, scope(crc_offset + 4, order), name, sizeof(name)) ==
                  TB_SYMBOL_COMPANION_METADATA_PENDING);
            CHECK(link_parse(&link, bytes, crc_offset + 4, chunk) == TB_SYMBOL_COMPANION_METADATA_FOUND);
            CHECK(tb_symbol_debuglink_view(&link, &view));
            CHECK(view.name_bytes == length && !memcmp(view.name, bytes, length + 1));
            CHECK(view.crc == UINT32_C(0x12345678) && view.crc_offset == crc_offset);
            CHECK(view.scope.section_index == 7 && view.scope.backing.key.inode == 2);
            size_t consumed = 99;
            CHECK(tb_symbol_debuglink_feed(&link, bytes, 1, &consumed) ==
                  TB_SYMBOL_COMPANION_METADATA_FOUND && consumed == 0);
        }
        for (size_t end = 0; end < crc_offset + 4; ++end) {
            CHECK(tb_symbol_debuglink_init(&link, scope(crc_offset + 4, order), name, sizeof(name)) ==
                  TB_SYMBOL_COMPANION_METADATA_PENDING);
            CHECK(link_parse(&link, bytes, end, 1) == TB_SYMBOL_COMPANION_METADATA_MALFORMED);
            CHECK(!tb_symbol_debuglink_view(&link, &view));
        }
    }
    const char *invalid[] = {"", ".", "..", "../x", "a/b", "a\nb", "a\rb"};
    for (size_t i = 0; i < sizeof(invalid) / sizeof(*invalid); ++i) {
        memset(bytes, 0, sizeof(bytes));
        memcpy(bytes, invalid[i], strlen(invalid[i]));
        size_t size = ((strlen(invalid[i]) + 4) & ~(size_t)3) + 4;
        CHECK(tb_symbol_debuglink_init(&link, scope(size, order), name, sizeof(name)) ==
              TB_SYMBOL_COMPANION_METADATA_PENDING);
        CHECK(link_parse(&link, bytes, size, 2) == (i ? TB_SYMBOL_COMPANION_METADATA_UNSUPPORTED :
                                                      TB_SYMBOL_COMPANION_METADATA_MALFORMED));
        CHECK(!tb_symbol_debuglink_view(&link, &view));
    }
    const unsigned char valid[] = {'a', 0, 0, 0, 0, 0, 0, 0};
    CHECK(tb_symbol_debuglink_init(&link, scope(8, order), name, 1) ==
          TB_SYMBOL_COMPANION_METADATA_PENDING);
    CHECK(link_parse(&link, valid, sizeof(valid), 3) == TB_SYMBOL_COMPANION_METADATA_RESOURCE);
    CHECK(!tb_symbol_debuglink_view(&link, &view));
    CHECK(tb_symbol_debuglink_init(&link, scope(9, order), name, 16) ==
          TB_SYMBOL_COMPANION_METADATA_PENDING);
    CHECK(link_parse(&link, valid, sizeof(valid), 3) == TB_SYMBOL_COMPANION_METADATA_MALFORMED);
    tb_symbol_debuglink_discard(&link);
    CHECK(!tb_symbol_debuglink_view(&link, &view));
}


static size_t note(unsigned char *bytes, size_t offset, unsigned alignment,
    TbSymbolByteOrder_e order, const unsigned char *identifier, size_t count, uint32_t type)
{
    number(bytes + offset, 4, order);
    number(bytes + offset + 4, (uint32_t)count, order);
    number(bytes + offset + 8, type, order);
    memcpy(bytes + offset + 12, "GNU", 4);
    size_t descriptor = (offset + 16 + alignment - 1) & ~((size_t)alignment - 1);
    memcpy(bytes + descriptor, identifier, count);
    return (descriptor + count + alignment - 1) & ~((size_t)alignment - 1);
}


static TbSymbolCompanionMetadataStatus_e id_parse(TbSymbolBuildId_t *id,
    const unsigned char *bytes, size_t length, size_t chunk)
{
    TbSymbolCompanionMetadataStatus_e status = id->status;
    size_t cursor = 0;
    while (cursor < length && status == TB_SYMBOL_COMPANION_METADATA_PENDING) {
        TbSymbolBuildIdView_t view;
        CHECK(!tb_symbol_build_id_view(id, &view));
        size_t count = length - cursor < chunk ? length - cursor : chunk;
        size_t consumed;
        status = tb_symbol_build_id_feed(id, bytes + cursor, count, &consumed);
        CHECK(consumed <= count && consumed <= 256);
        cursor += consumed;
    }
    if (status == TB_SYMBOL_COMPANION_METADATA_PENDING)
        status = tb_symbol_build_id_finish(id);
    return status;
}


static void notes(TbSymbolByteOrder_e order, unsigned alignment)
{
    unsigned char bytes[2048] = {0};
    unsigned char identifier[600];
    unsigned char storage[600];
    for (size_t i = 0; i < sizeof(identifier); ++i)
        identifier[i] = (unsigned char)i;
    TbSymbolBuildId_t id;
    TbSymbolBuildIdView_t view;
    size_t size = note(bytes, 0, alignment, order, identifier, 20, 3);
    for (size_t chunk = 1; chunk < 50; ++chunk) {
        CHECK(tb_symbol_build_id_init(&id, scope(size, order), alignment, storage, sizeof(storage)) ==
              TB_SYMBOL_COMPANION_METADATA_PENDING);
        CHECK(id_parse(&id, bytes, size, chunk) == TB_SYMBOL_COMPANION_METADATA_FOUND);
        CHECK(tb_symbol_build_id_view(&id, &view) && view.length == 20 && view.note_offset == 0);
        CHECK(!memcmp(view.bytes, identifier, 20) && view.descriptor_offset == 16);
    }
    for (size_t end = 0; end < size; ++end) {
        CHECK(tb_symbol_build_id_init(&id, scope(size, order), alignment, storage, sizeof(storage)) ==
              TB_SYMBOL_COMPANION_METADATA_PENDING);
        CHECK(id_parse(&id, bytes, end, 1) == TB_SYMBOL_COMPANION_METADATA_MALFORMED);
        CHECK(!tb_symbol_build_id_view(&id, &view));
    }
    /* A valid candidate cannot survive a malformed later note header. */
    CHECK(tb_symbol_build_id_init(&id, scope(size + 1, order), alignment, storage, sizeof(storage)) ==
          TB_SYMBOL_COMPANION_METADATA_PENDING);
    CHECK(id_parse(&id, bytes, size + 1, 3) == TB_SYMBOL_COMPANION_METADATA_MALFORMED);
    CHECK(!tb_symbol_build_id_view(&id, &view));
    bytes[15] = 'X';
    CHECK(tb_symbol_build_id_init(&id, scope(size, order), alignment, storage, sizeof(storage)) ==
          TB_SYMBOL_COMPANION_METADATA_PENDING);
    CHECK(id_parse(&id, bytes, size, 3) == TB_SYMBOL_COMPANION_METADATA_MALFORMED);
    bytes[15] = 0;
    size_t total = note(bytes, size, alignment, order, identifier, 20, 3);
    CHECK(tb_symbol_build_id_init(&id, scope(total, order), alignment, storage, sizeof(storage)) ==
          TB_SYMBOL_COMPANION_METADATA_PENDING);
    CHECK(id_parse(&id, bytes, total, 13) == TB_SYMBOL_COMPANION_METADATA_CONFLICT);
    CHECK(!tb_symbol_build_id_view(&id, &view));

    size = note(bytes, 0, alignment, order, identifier, 4, 7);
    total = note(bytes, size, alignment, order, identifier, 513, 3);
    CHECK(tb_symbol_build_id_init(&id, scope(total, order), alignment, storage, sizeof(storage)) ==
          TB_SYMBOL_COMPANION_METADATA_PENDING);
    CHECK(id_parse(&id, bytes, total, 65536) == TB_SYMBOL_COMPANION_METADATA_FOUND);
    CHECK(tb_symbol_build_id_view(&id, &view) && view.length == 513 && view.note_offset == size);
    CHECK(!memcmp(view.bytes, identifier, 513));
    CHECK(tb_symbol_build_id_init(&id, scope(total, order), alignment, storage, 512) ==
          TB_SYMBOL_COMPANION_METADATA_PENDING);
    CHECK(id_parse(&id, bytes, total, 7) == TB_SYMBOL_COMPANION_METADATA_RESOURCE);
    CHECK(!tb_symbol_build_id_view(&id, &view));
    CHECK(tb_symbol_build_id_init(&id, scope(size, order), alignment, storage, 0) ==
          TB_SYMBOL_COMPANION_METADATA_PENDING);
    CHECK(id_parse(&id, bytes, size, 7) == TB_SYMBOL_COMPANION_METADATA_ABSENT);
    size = note(bytes, 0, alignment, order, identifier, 0, 3);
    CHECK(tb_symbol_build_id_init(&id, scope(size, order), alignment, storage, sizeof(storage)) ==
          TB_SYMBOL_COMPANION_METADATA_PENDING);
    CHECK(id_parse(&id, bytes, size, 7) == TB_SYMBOL_COMPANION_METADATA_MALFORMED);
    CHECK(tb_symbol_build_id_init(&id, scope(0, order), alignment, NULL, 0) ==
          TB_SYMBOL_COMPANION_METADATA_PENDING);
    CHECK(tb_symbol_build_id_finish(&id) == TB_SYMBOL_COMPANION_METADATA_ABSENT);
    tb_symbol_build_id_discard(&id);
    CHECK(!tb_symbol_build_id_view(&id, &view));
}


static void invalid_requests(void)
{
    unsigned char bytes[32] = {0};
    TbSymbolDebuglink_t link;
    CHECK(tb_symbol_debuglink_init(&link, scope(8, TB_SYMBOL_LITTLE_ENDIAN), bytes, sizeof(bytes)) ==
          TB_SYMBOL_COMPANION_METADATA_PENDING);
    TbSymbolDebuglink_t original = link;
    TbSymbolCompanionScope_t bad = scope(8, TB_SYMBOL_LITTLE_ENDIAN);
    bad.bytes = bad.backing.bytes + 1;
    CHECK(tb_symbol_debuglink_init(&link, bad, bytes, sizeof(bytes)) == TB_SYMBOL_COMPANION_METADATA_INVALID);
    CHECK(!memcmp(&link, &original, sizeof(link)));
    size_t consumed = 77;
    CHECK(tb_symbol_debuglink_feed(&link, NULL, 1, &consumed) == TB_SYMBOL_COMPANION_METADATA_INVALID);
    CHECK(consumed == 77 && !memcmp(&link, &original, sizeof(link)));
    const unsigned char prefix[] = {'a', 'b', 'c'};
    CHECK(tb_symbol_debuglink_feed(&link, prefix, sizeof(prefix), &consumed) ==
          TB_SYMBOL_COMPANION_METADATA_PENDING && consumed == 3);
    tb_symbol_debuglink_discard(&link);
    TbSymbolDebuglinkView_t unavailable;
    CHECK(!tb_symbol_debuglink_view(&link, &unavailable));
    CHECK(tb_symbol_debuglink_feed(&link, prefix, 1, &consumed) == TB_SYMBOL_COMPANION_METADATA_INVALID);
    consumed = 77;


    TbSymbolBuildId_t id;
    CHECK(tb_symbol_build_id_init(&id, scope(32, TB_SYMBOL_BIG_ENDIAN), 4, bytes, sizeof(bytes)) ==
          TB_SYMBOL_COMPANION_METADATA_PENDING);
    TbSymbolBuildId_t previous = id;
    CHECK(tb_symbol_build_id_init(&id, scope(32, TB_SYMBOL_BIG_ENDIAN), 16, bytes, sizeof(bytes)) ==
          TB_SYMBOL_COMPANION_METADATA_UNSUPPORTED);
    CHECK(!memcmp(&id, &previous, sizeof(id)));
    CHECK(tb_symbol_build_id_feed(&id, NULL, 1, &consumed) == TB_SYMBOL_COMPANION_METADATA_INVALID);
    CHECK(consumed == 77 && !memcmp(&id, &previous, sizeof(id)));
    CHECK(tb_symbol_build_id_feed(&id, bytes, 3, &consumed) ==
          TB_SYMBOL_COMPANION_METADATA_PENDING && consumed == 3);

    tb_symbol_build_id_discard(&id);
    CHECK(tb_symbol_build_id_finish(&id) == TB_SYMBOL_COMPANION_METADATA_INVALID);
}


int main(void)
{
    invalid_requests();
    links(TB_SYMBOL_LITTLE_ENDIAN);
    links(TB_SYMBOL_BIG_ENDIAN);
    for (unsigned alignment = 4; alignment <= 8; alignment += 4) {
        notes(TB_SYMBOL_LITTLE_ENDIAN, alignment);
        notes(TB_SYMBOL_BIG_ENDIAN, alignment);
    }
    puts("PASS bounded debuglinks and GNU notes, endian/alignment, truncation, conflicts and capacity");
    return 0;
}
