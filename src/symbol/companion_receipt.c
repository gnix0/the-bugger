#include "symbol/companion_receipt.h"
#include "symbol/protocol.h"

#include <string.h>

static bool valid(const TbSymbolCompanionInventory_t *view)
{
    if (!view || view->backing.bytes > INT64_MAX || view->name_bytes >= TB_SYMBOL_COMPANION_PATH_BYTES ||
        view->id_bytes > TB_SYMBOL_COMPANION_ID_BYTES ||
        (view->name_bytes && (!view->name || !view->debuglink_known)) ||
        (view->id_bytes && !view->build_id) ||
        (view->header.address_bytes != 4 && view->header.address_bytes != 8) ||
        (view->header.byte_order != TB_SYMBOL_LITTLE_ENDIAN &&
         view->header.byte_order != TB_SYMBOL_BIG_ENDIAN) ||
        ((view->name_bytes != 0) != (view->debuglink_section != UINT64_MAX)) ||
        ((view->id_bytes != 0) != (view->build_id_section != UINT64_MAX)) ||
        ((view->id_bytes != 0) != (view->build_id_note != UINT64_MAX))) {
        return false;
    }

    for (size_t i = 0; i < view->name_bytes; ++i) {
        unsigned char byte = view->name[i];
        if (!byte || byte == '/' || byte == '\r' || byte == '\n')
            return false;
    }
    if ((view->name_bytes == 1 && view->name[0] == '.') ||
        (view->name_bytes == 2 && view->name[0] == '.' && view->name[1] == '.')) {
        return false;
    }

    return true;
}


size_t tb_symbol_companion_receipt_size(const TbSymbolCompanionInventory_t *view)
{
    return valid(view) ? TB_SYMBOL_COMPANION_RECEIPT_BYTES + view->name_bytes + view->id_bytes : 0;
}


bool tb_symbol_companion_receipt_encode(const TbSymbolCompanionInventory_t *view, void *bytes, size_t length)
{
    size_t required = tb_symbol_companion_receipt_size(view);
    if (!bytes || !required || length != required)
        return false;

    uint64_t wide[] = {
        view->backing.bytes, view->backing.key.device, view->backing.key.inode,
        view->debuglink_section, view->build_id_section, view->build_id_note
    };
    uint32_t narrow[] = {
        view->header.object_type, view->header.machine, view->header.address_bytes,
        view->header.byte_order == TB_SYMBOL_LITTLE_ENDIAN ? 1U : 2U, view->header.flags,
        view->header.os_abi, view->header.abi_version, view->debuglink_known ? 1U : 0U,
        view->debuglink_crc, view->file_crc, (uint32_t)view->name_bytes, (uint32_t)view->id_bytes
    };
    unsigned char *output = bytes;
    for (size_t i = 0; i < sizeof(wide) / sizeof(wide[0]); ++i)
        tb_symbol_protocol_put(output + i * 8, wide[i], 8);
    for (size_t i = 0; i < sizeof(narrow) / sizeof(narrow[0]); ++i)
        tb_symbol_protocol_put(output + 48 + i * 4, narrow[i], 4);
    if (view->name_bytes)
        memcpy(output + TB_SYMBOL_COMPANION_RECEIPT_BYTES, view->name, view->name_bytes);
    if (view->id_bytes)
        memcpy(output + TB_SYMBOL_COMPANION_RECEIPT_BYTES + view->name_bytes, view->build_id, view->id_bytes);

    return true;
}


bool tb_symbol_companion_receipt_decode(const void *bytes, size_t length, TbSymbolCompanionInventory_t *view)
{
    if (!bytes || !view || length < TB_SYMBOL_COMPANION_RECEIPT_BYTES)
        return false;

    const unsigned char *input = bytes;
    uint32_t fields[12];
    for (size_t i = 0; i < sizeof(fields) / sizeof(fields[0]); ++i)
        fields[i] = (uint32_t)tb_symbol_protocol_get(input + 48 + i * 4, 4);
    if (fields[0] > UINT16_MAX || fields[1] > UINT16_MAX || fields[5] > UINT8_MAX ||
        fields[6] > UINT8_MAX || fields[7] > 1 || (fields[3] != 1 && fields[3] != 2) ||
        fields[10] >= TB_SYMBOL_COMPANION_PATH_BYTES || fields[11] > TB_SYMBOL_COMPANION_ID_BYTES ||
        length != TB_SYMBOL_COMPANION_RECEIPT_BYTES + (size_t)fields[10] + fields[11]) {
        return false;
    }

    TbSymbolCompanionInventory_t candidate = {
        .backing = {
            .key = {
                .device = tb_symbol_protocol_get(input + 8, 8),
                .inode  = tb_symbol_protocol_get(input + 16, 8)
            },
            .bytes = tb_symbol_protocol_get(input, 8)
        },
        .header = {
            .byte_order    = fields[3] == 1 ? TB_SYMBOL_LITTLE_ENDIAN : TB_SYMBOL_BIG_ENDIAN,
            .address_bytes = fields[2],
            .os_abi        = (uint8_t)fields[5],
            .abi_version   = (uint8_t)fields[6],
            .object_type   = (uint16_t)fields[0],
            .machine       = (uint16_t)fields[1],
            .flags         = fields[4],
            .file_bytes    = tb_symbol_protocol_get(input, 8)
        },
        .name              = input + TB_SYMBOL_COMPANION_RECEIPT_BYTES,
        .build_id          = input + TB_SYMBOL_COMPANION_RECEIPT_BYTES + fields[10],
        .name_bytes        = fields[10],
        .id_bytes          = fields[11],
        .debuglink_section = tb_symbol_protocol_get(input + 24, 8),
        .build_id_section  = tb_symbol_protocol_get(input + 32, 8),
        .build_id_note     = tb_symbol_protocol_get(input + 40, 8),
        .debuglink_crc     = fields[8],
        .file_crc          = fields[9],
        .debuglink_known   = fields[7] != 0
    };
    if (!valid(&candidate))
        return false;

    *view = candidate;

    return true;
}
