#include "symbol/line_reference.h"

#include <string.h>

TbSymbolLineReferenceStatus_e tb_symbol_line_reference_init(TbSymbolLineReference_t *reference,
    TbSymbolUnitsView_t units, TbSymbolDiesView_t tree, uint64_t unit_index)
{
    if (!reference || !units.entries || unit_index >= units.count || !tree.entries || !tree.count ||
        !units.entries[unit_index].header_available || units.section_index != tree.section_index ||
        units.entries[unit_index].offset != tree.unit_offset ||
        units.backing.key.device != tree.backing.key.device ||
        units.backing.key.inode != tree.backing.key.inode || units.backing.bytes != tree.backing.bytes) {
        return TB_SYMBOL_LINE_REFERENCE_INVALID;
    }

    const TbSymbolUnitEntry_t *entry = &units.entries[unit_index];
    const TbSymbolDieEntry_t  *root  = &tree.entries[0];
    uint64_t                  root_offset;

    bool root_kind = (tree.kind == TB_SYMBOL_DWARF_COMPILE && root->tag == 0x11) ||
                     (tree.kind == TB_SYMBOL_DWARF_PARTIAL && root->tag == 0x3c) ||
                     (tree.kind == TB_SYMBOL_DWARF_TYPE && root->tag == 0x41);
    bool unit_kind = entry->header.kind == tree.kind ||
                     (entry->header.kind == TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL &&
                      (tree.kind == TB_SYMBOL_DWARF_COMPILE || tree.kind == TB_SYMBOL_DWARF_PARTIAL));
    TbSymbolAttributes_t lookup;

    if (!root_kind || !unit_kind || root->parent != UINT64_MAX ||
        (entry->header.version != 4 && entry->header.version != 5) ||
        (entry->header.extent.offset_bytes != 4 && entry->header.extent.offset_bytes != 8) ||
        !entry->header.address_bytes || tree.abbrev_offset != entry->header.abbrev_offset ||
        !tb_symbol_add(entry->offset, entry->header.header_bytes, &root_offset) ||
        root->offset != root_offset || root->end_offset <= root->offset ||
        root->end_offset > entry->header.extent.end_offset ||
        tb_symbol_attributes_init(&lookup, tree, (TbSymbolAttributeSelection_t) {
            .die_index = 0,
            .name      = 0x10
        }) != TB_SYMBOL_ATTRIBUTE_PENDING) {
        return TB_SYMBOL_LINE_REFERENCE_INVALID;
    }

    *reference = (TbSymbolLineReference_t) {
        .lookup = lookup,
        .view = {
            .unit           = entry->header,
            .abbrev_section = tree.abbrev_section,
            .abbrev_offset  = tree.abbrev_offset
        },
        .status      = TB_SYMBOL_LINE_REFERENCE_PENDING,
        .root_end    = root->end_offset,
        .initialized = true
    };

    return reference->status;
}


TbSymbolLineReferenceStatus_e tb_symbol_line_reference_step(TbSymbolLineReference_t *reference,
                                                            size_t max_steps)
{
    if (!reference || !reference->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_LINE_REFERENCE_INVALID;

    if (reference->status != TB_SYMBOL_LINE_REFERENCE_PENDING)
        return reference->status;

    TbSymbolAttributeStatus_e status = tb_symbol_attributes_step(&reference->lookup, max_steps);
    if (status == TB_SYMBOL_ATTRIBUTE_PENDING)
        return reference->status;

    (void)tb_symbol_attributes_result(&reference->lookup, &reference->view.source);
    if (status == TB_SYMBOL_ATTRIBUTE_MISSING) {
        reference->status = TB_SYMBOL_LINE_REFERENCE_ABSENT;
    } else if (status == TB_SYMBOL_ATTRIBUTE_AMBIGUOUS) {
        reference->status = TB_SYMBOL_LINE_REFERENCE_CONFLICT;
    } else if (status != TB_SYMBOL_ATTRIBUTE_UNIQUE) {
        reference->status = TB_SYMBOL_LINE_REFERENCE_INVALID;
    } else {
        const TbSymbolDieAttribute_t *attribute =
            &reference->lookup.attributes[reference->view.source.first_index];
        uint64_t                  end;
        uint64_t                  payload_end;
        const TbSymbolFormView_t *value = &attribute->value;

        if (value->form != TB_SYMBOL_FORM_SEC_OFFSET || !value->numeric || value->is_signed ||
            value->payload_bytes != reference->view.unit.extent.offset_bytes ||
            attribute->offset <= reference->view.source.record_offset ||
            !tb_symbol_add(attribute->offset, value->bytes, &end) || end > reference->root_end ||
            value->payload_offset < attribute->offset ||
            !tb_symbol_add(value->payload_offset, value->payload_bytes, &payload_end) || payload_end != end) {
            reference->status = TB_SYMBOL_LINE_REFERENCE_MALFORMED;
        } else {
            reference->view.attribute   = *attribute;
            reference->view.line_offset = value->bits;
            reference->status           = TB_SYMBOL_LINE_REFERENCE_FOUND;
        }
    }

    return reference->status;
}


bool tb_symbol_line_reference_view(const TbSymbolLineReference_t *reference,
                                    TbSymbolLineReferenceView_t *view)
{
    if (!reference || !reference->initialized || !view ||
        reference->status != TB_SYMBOL_LINE_REFERENCE_FOUND) {
        return false;
    }

    *view = reference->view;
    return true;
}


bool tb_symbol_line_reference_scope(const TbSymbolLineReference_t *reference,
                                     TbSymbolAttributeResult_t *scope)
{
    if (!reference || !reference->initialized || !scope ||
        (reference->status != TB_SYMBOL_LINE_REFERENCE_FOUND &&
         reference->status != TB_SYMBOL_LINE_REFERENCE_ABSENT &&
         reference->status != TB_SYMBOL_LINE_REFERENCE_CONFLICT)) {
        return false;
    }

    *scope = reference->view.source;
    return true;
}


void tb_symbol_line_reference_discard(TbSymbolLineReference_t *reference)
{
    if (reference)
        memset(reference, 0, sizeof(*reference));
}
