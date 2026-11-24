#include "symbol/companion_metadata.h"

#include <string.h>

static bool scope_valid(TbSymbolCompanionScope_t scope)
{
    return scope.section_index != UINT64_MAX && scope.bytes <= scope.backing.bytes &&
           (scope.byte_order == TB_SYMBOL_LITTLE_ENDIAN || scope.byte_order == TB_SYMBOL_BIG_ENDIAN);
}


TbSymbolCompanionMetadataStatus_e tb_symbol_debuglink_init(TbSymbolDebuglink_t *link,
    TbSymbolCompanionScope_t scope, unsigned char *name, size_t capacity)
{
    if (!link || !scope_valid(scope) || (!name && capacity))
        return TB_SYMBOL_COMPANION_METADATA_INVALID;

    *link = (TbSymbolDebuglink_t) {
        .view = {
            .scope = scope,
            .name  = name
        },
        .name        = name,
        .capacity    = capacity,
        .status      = TB_SYMBOL_COMPANION_METADATA_PENDING,
        .initialized = true
    };

    return link->status;
}


static void debuglink_byte(TbSymbolDebuglink_t *link, unsigned char byte)
{
    if (!link->phase) {
        if (!byte && !link->view.name_bytes) {
            link->status = TB_SYMBOL_COMPANION_METADATA_MALFORMED;
            return;
        }
        if (byte == '/' || byte == '\r' || byte == '\n')
            link->unsupported_name = true;
        if (link->view.name_bytes == link->capacity) {
            link->status = TB_SYMBOL_COMPANION_METADATA_RESOURCE;
            return;
        }

        link->name[link->view.name_bytes] = byte;
        if (byte) {
            ++link->view.name_bytes;
        } else {
            if (link->name[0] == '.' && (link->view.name_bytes == 1 ||
                (link->view.name_bytes == 2 && link->name[1] == '.'))) {
                link->unsupported_name = true;
            }

            uint64_t rounded;
            uint64_t end;
            if (!tb_symbol_add(link->cursor, 3, &rounded) ||
                !tb_symbol_add(rounded & ~UINT64_C(3), 4, &end) || end != link->view.scope.bytes) {
                link->status = TB_SYMBOL_COMPANION_METADATA_MALFORMED;
                return;
            }
            link->view.crc_offset = rounded & ~UINT64_C(3);
            link->phase = 1;
        }
    } else if (link->cursor - 1 >= link->view.crc_offset) {
        unsigned shift = link->view.scope.byte_order == TB_SYMBOL_LITTLE_ENDIAN ?
                         link->received * 8U : (3U - link->received) * 8U;
        link->accumulator |= (uint64_t)byte << shift;
        if (++link->received == 4) {
            link->view.crc = (uint32_t)link->accumulator;
            link->status = link->unsupported_name ? TB_SYMBOL_COMPANION_METADATA_UNSUPPORTED :
                                                   TB_SYMBOL_COMPANION_METADATA_FOUND;
        }
    }
}


TbSymbolCompanionMetadataStatus_e tb_symbol_debuglink_feed(TbSymbolDebuglink_t *link,
    const void *bytes, size_t length, size_t *consumed)
{
    if (!link || !link->initialized || !consumed || (!bytes && length))
        return TB_SYMBOL_COMPANION_METADATA_INVALID;

    *consumed = 0;
    if (link->status != TB_SYMBOL_COMPANION_METADATA_PENDING)
        return link->status;

    const unsigned char *input = bytes;
    size_t limit = length < 256 ? length : 256;
    while (*consumed < limit && link->cursor < link->view.scope.bytes &&
           link->status == TB_SYMBOL_COMPANION_METADATA_PENDING) {
        unsigned char byte = input[(*consumed)++];
        ++link->cursor;
        debuglink_byte(link, byte);
    }
    if (link->status == TB_SYMBOL_COMPANION_METADATA_PENDING && link->cursor == link->view.scope.bytes)
        link->status = TB_SYMBOL_COMPANION_METADATA_MALFORMED;

    return link->status;
}


TbSymbolCompanionMetadataStatus_e tb_symbol_debuglink_finish(TbSymbolDebuglink_t *link)
{
    if (!link || !link->initialized)
        return TB_SYMBOL_COMPANION_METADATA_INVALID;
    if (link->status == TB_SYMBOL_COMPANION_METADATA_PENDING)
        link->status = TB_SYMBOL_COMPANION_METADATA_MALFORMED;

    return link->status;
}


bool tb_symbol_debuglink_view(const TbSymbolDebuglink_t *link, TbSymbolDebuglinkView_t *view)
{
    if (!link || !link->initialized || !view || link->status != TB_SYMBOL_COMPANION_METADATA_FOUND)
        return false;

    *view = link->view;

    return true;
}


void tb_symbol_debuglink_discard(TbSymbolDebuglink_t *link)
{
    if (link)
        *link = (TbSymbolDebuglink_t) {0};
}


TbSymbolCompanionMetadataStatus_e tb_symbol_build_id_init(TbSymbolBuildId_t *id,
    TbSymbolCompanionScope_t scope, unsigned alignment, unsigned char *identifier, size_t capacity)
{
    if (!id || !scope_valid(scope) || (!identifier && capacity))
        return TB_SYMBOL_COMPANION_METADATA_INVALID;
    if (alignment != 4 && alignment != 8)
        return TB_SYMBOL_COMPANION_METADATA_UNSUPPORTED;

    *id = (TbSymbolBuildId_t) {
        .view = {
            .scope     = scope,
            .bytes     = identifier,
            .alignment = alignment
        },
        .identifier  = identifier,
        .capacity    = capacity,
        .alignment   = alignment,
        .status      = TB_SYMBOL_COMPANION_METADATA_PENDING,
        .initialized = true
    };

    return id->status;
}


static bool align_offset(uint64_t offset, unsigned alignment, uint64_t *result)
{
    uint64_t rounded;
    if (!tb_symbol_add(offset, alignment - 1U, &rounded))
        return false;

    *result = rounded & ~((uint64_t)alignment - 1);

    return true;
}


static bool note_header(TbSymbolBuildId_t *id)
{
    TbSymbolReader_t reader;
    (void)tb_symbol_reader_init(&reader, id->header, sizeof(id->header), id->view.scope.byte_order);
    uint64_t name_bytes;
    uint64_t descriptor_bytes;
    uint64_t type;
    if (tb_symbol_reader_uint(&reader, 4, &name_bytes) != TB_SYMBOL_READ_OK ||
        tb_symbol_reader_uint(&reader, 4, &descriptor_bytes) != TB_SYMBOL_READ_OK ||
        tb_symbol_reader_uint(&reader, 4, &type) != TB_SYMBOL_READ_OK ||
        !tb_symbol_add(id->cursor, name_bytes, &id->name_end) ||
        !align_offset(id->name_end, id->alignment, &id->descriptor_offset) ||
        !tb_symbol_add(id->descriptor_offset, descriptor_bytes, &id->descriptor_end) ||
        !align_offset(id->descriptor_end, id->alignment, &id->note_end) ||
        id->note_end > id->view.scope.bytes) {
        return false;
    }

    id->name_bytes       = (uint32_t)name_bytes;
    id->descriptor_bytes = (uint32_t)descriptor_bytes;
    id->type             = (uint32_t)type;
    id->selected         = false;
    id->phase            = 1;

    return true;
}


static void classify_note(TbSymbolBuildId_t *id)
{
    if (id->name_bytes != 4 || id->type != 3 || memcmp(id->owner, "GNU", 4) != 0)
        return;

    if (!id->descriptor_bytes) {
        id->status = TB_SYMBOL_COMPANION_METADATA_MALFORMED;
    } else if (id->found) {
        id->status = TB_SYMBOL_COMPANION_METADATA_CONFLICT;
    } else if (id->descriptor_bytes > id->capacity) {
        id->status = TB_SYMBOL_COMPANION_METADATA_RESOURCE;
    } else {
        id->selected               = true;
        id->view.length            = id->descriptor_bytes;
        id->view.note_offset       = id->note_offset;
        id->view.descriptor_offset = id->descriptor_offset;
    }
}


static void note_byte(TbSymbolBuildId_t *id, unsigned char byte)
{
    uint64_t position = id->cursor++;
    if (!id->phase) {
        id->header[id->received++] = byte;
        if (id->received == sizeof(id->header) && !note_header(id)) {
            id->status = TB_SYMBOL_COMPANION_METADATA_MALFORMED;
            return;
        }
    } else {
        uint64_t name_start = id->note_offset + sizeof(id->header);
        if (position < id->name_end && position - name_start < sizeof(id->owner))
            id->owner[(size_t)(position - name_start)] = byte;
        if (id->cursor == id->name_end) {
            if (byte) {
                id->status = TB_SYMBOL_COMPANION_METADATA_MALFORMED;
                return;
            }
            classify_note(id);
        }
        if (id->selected && position >= id->descriptor_offset && position < id->descriptor_end)
            id->identifier[(size_t)(position - id->descriptor_offset)] = byte;
    }

    if (id->phase && id->cursor == id->note_end) {
        id->found       = id->found || id->selected;
        id->phase       = 0;
        id->received    = 0;
        id->note_offset = id->cursor;
    }
}


TbSymbolCompanionMetadataStatus_e tb_symbol_build_id_feed(TbSymbolBuildId_t *id,
    const void *bytes, size_t length, size_t *consumed)
{
    if (!id || !id->initialized || !consumed || (!bytes && length))
        return TB_SYMBOL_COMPANION_METADATA_INVALID;

    *consumed = 0;
    if (id->status != TB_SYMBOL_COMPANION_METADATA_PENDING)
        return id->status;

    const unsigned char *input = bytes;
    size_t limit = length < 256 ? length : 256;
    while (*consumed < limit && id->cursor < id->view.scope.bytes &&
           id->status == TB_SYMBOL_COMPANION_METADATA_PENDING) {
        note_byte(id, input[(*consumed)++]);
    }
    if (id->status == TB_SYMBOL_COMPANION_METADATA_PENDING && id->cursor == id->view.scope.bytes) {
        id->status = id->phase || id->received ? TB_SYMBOL_COMPANION_METADATA_MALFORMED :
                     (id->found ? TB_SYMBOL_COMPANION_METADATA_FOUND : TB_SYMBOL_COMPANION_METADATA_ABSENT);
    }

    return id->status;
}


TbSymbolCompanionMetadataStatus_e tb_symbol_build_id_finish(TbSymbolBuildId_t *id)
{
    if (!id || !id->initialized)
        return TB_SYMBOL_COMPANION_METADATA_INVALID;
    if (id->status == TB_SYMBOL_COMPANION_METADATA_PENDING) {
        if (id->cursor != id->view.scope.bytes || id->phase || id->received)
            id->status = TB_SYMBOL_COMPANION_METADATA_MALFORMED;
        else
            id->status = id->found ? TB_SYMBOL_COMPANION_METADATA_FOUND : TB_SYMBOL_COMPANION_METADATA_ABSENT;
    }

    return id->status;
}


bool tb_symbol_build_id_view(const TbSymbolBuildId_t *id, TbSymbolBuildIdView_t *view)
{
    if (!id || !id->initialized || !view || id->status != TB_SYMBOL_COMPANION_METADATA_FOUND)
        return false;

    *view = id->view;

    return true;
}


void tb_symbol_build_id_discard(TbSymbolBuildId_t *id)
{
    if (id)
        *id = (TbSymbolBuildId_t) {0};
}
