#include "symbol/constants.h"

TbSymbolConstantStatus_e tb_symbol_constant_init(TbSymbolConstant_t *constant, TbSymbolDiesView_t tree,
                                                 uint64_t die_index, TbSymbolConstantField_e field)
{
    if (!constant || !tree.entries || die_index >= tree.count)
        return TB_SYMBOL_CONSTANT_INVALID;

    const TbSymbolDieEntry_t *entry = &tree.entries[die_index];

    if (field == TB_SYMBOL_CONSTANT_LANGUAGE) {
        if (die_index != 0 || entry->parent != UINT64_MAX ||
            (entry->tag != 0x11 && entry->tag != 0x3c && entry->tag != 0x41)) {
            return TB_SYMBOL_CONSTANT_INVALID;
        }
    } else if (field != TB_SYMBOL_CONSTANT_INLINE || entry->tag != 0x2e) {
        return TB_SYMBOL_CONSTANT_INVALID;
    }

    TbSymbolAttributes_t lookup;
    if (tb_symbol_attributes_init(&lookup, tree, (TbSymbolAttributeSelection_t) {
        .die_index = die_index,
        .name      = (uint64_t)field
    }) != TB_SYMBOL_ATTRIBUTE_PENDING) {
        return TB_SYMBOL_CONSTANT_INVALID;
    }

    *constant = (TbSymbolConstant_t) {
        .lookup      = lookup,
        .attributes  = tree.attributes,
        .entry       = *entry,
        .view        = {
            .abbrev_section = tree.abbrev_section,
            .abbrev_offset  = tree.abbrev_offset
        },
        .status      = TB_SYMBOL_CONSTANT_PENDING,
        .initialized = true
    };

    return constant->status;
}


static TbSymbolConstantStatus_e interpret(TbSymbolFormView_t value, uint64_t *result)
{
    unsigned width = 64;
    bool signed_form = false;

    switch (value.form) {
    case TB_SYMBOL_FORM_DATA1:
        width = 8;
        break;
    case TB_SYMBOL_FORM_DATA2:
        width = 16;
        break;
    case TB_SYMBOL_FORM_DATA4:
        width = 32;
        break;
    case TB_SYMBOL_FORM_DATA8:
    case TB_SYMBOL_FORM_UDATA:
        break;
    case TB_SYMBOL_FORM_SDATA:
    case TB_SYMBOL_FORM_IMPLICIT_CONST:
        signed_form = true;
        break;
    case TB_SYMBOL_FORM_DATA16:
        return TB_SYMBOL_CONSTANT_UNSUPPORTED;
    default:
        return TB_SYMBOL_CONSTANT_MALFORMED;
    }

    if (!value.numeric || value.is_signed != signed_form ||
        (signed_form && value.bits > INT64_MAX) || (width < 64 && value.bits >> width)) {
        return TB_SYMBOL_CONSTANT_MALFORMED;
    }

    *result = value.bits;

    return TB_SYMBOL_CONSTANT_FOUND;
}


TbSymbolConstantStatus_e tb_symbol_constant_schema(TbSymbolConstant_t *constant,
                                                   const TbSymbolAbbrevView_t *schema)
{
    if (!constant || !constant->initialized || constant->status != TB_SYMBOL_CONSTANT_SCHEMA)
        return TB_SYMBOL_CONSTANT_INVALID;

    if (!schema) {
        constant->status = TB_SYMBOL_CONSTANT_DEPENDENCY;
        return constant->status;
    }

    TbSymbolStorageInfo_t backing = constant->view.selection.backing;
    const TbSymbolDieAttribute_t *attribute = &constant->view.attribute;
    if (!schema->entries || !schema->count || !schema->attributes ||
        schema->section_index != constant->view.abbrev_section ||
        schema->table_offset != constant->view.abbrev_offset || schema->version != 5 ||
        schema->backing.key.device != backing.key.device || schema->backing.key.inode != backing.key.inode ||
        schema->backing.bytes != backing.bytes || attribute->schema_attribute >= schema->attribute_count) {
        return TB_SYMBOL_CONSTANT_INVALID;
    }

    const TbSymbolAbbrevAttribute_t *field = &schema->attributes[attribute->schema_attribute];
    uint64_t end;
    if (field->name != attribute->name || field->form != TB_SYMBOL_FORM_IMPLICIT_CONST ||
        field->offset < schema->table_offset || field->offset >= schema->end_offset ||
        field->implicit_offset <= field->offset || !field->implicit_bytes ||
        !tb_symbol_add(field->implicit_offset, field->implicit_bytes, &end) || end > schema->end_offset) {
        return TB_SYMBOL_CONSTANT_INVALID;
    }

    constant->schema           = *schema;
    constant->schema_available = true;
    constant->status           = TB_SYMBOL_CONSTANT_PENDING;

    return constant->status;
}


static TbSymbolConstantStatus_e implicit(TbSymbolConstant_t *constant, size_t max_steps)
{
    const TbSymbolDieAttribute_t *attribute = &constant->view.attribute;
    for (size_t step = 0; step < max_steps && constant->schema_cursor < constant->schema.count; ++step) {
        uint64_t index = constant->schema_cursor++;
        const TbSymbolAbbrevEntry_t *entry = &constant->schema.entries[index];
        if (entry->code != constant->entry.code)
            continue;

        uint64_t expected;
        uint64_t ordinal = constant->view.selection.first_index - constant->entry.first_attribute;
        if (entry->tag != constant->entry.tag || entry->children != constant->entry.children ||
            entry->attribute_count != constant->entry.attribute_count ||
            !tb_symbol_add(entry->first_attribute, ordinal, &expected) ||
            expected != attribute->schema_attribute) {
            constant->status = TB_SYMBOL_CONSTANT_MALFORMED;
            return constant->status;
        }

        const TbSymbolAbbrevAttribute_t *field = &constant->schema.attributes[expected];
        if (!field->implicit_fits) {
            constant->status = TB_SYMBOL_CONSTANT_UNSUPPORTED;
        } else if (field->implicit_value < 0) {
            constant->status = TB_SYMBOL_CONSTANT_MALFORMED;
        } else {
            constant->view.value             = (uint64_t)field->implicit_value;
            constant->view.abbreviation      = *field;
            constant->view.schema_entry      = index;
            constant->view.from_abbreviation = true;
            constant->status                 = TB_SYMBOL_CONSTANT_FOUND;
        }

        return constant->status;
    }

    if (constant->schema_cursor == constant->schema.count)
        constant->status = TB_SYMBOL_CONSTANT_MALFORMED;

    return constant->status;
}


TbSymbolConstantStatus_e tb_symbol_constant_step(TbSymbolConstant_t *constant, size_t max_steps)
{
    if (!constant || !constant->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_CONSTANT_INVALID;

    if (constant->status != TB_SYMBOL_CONSTANT_PENDING)
        return constant->status;

    if (constant->schema_available)
        return implicit(constant, max_steps);

    TbSymbolAttributeStatus_e status = tb_symbol_attributes_step(&constant->lookup, max_steps);
    if (status == TB_SYMBOL_ATTRIBUTE_PENDING)
        return constant->status;

    if (tb_symbol_attributes_result(&constant->lookup, &constant->view.selection) != status) {
        constant->status = TB_SYMBOL_CONSTANT_INVALID;
        return constant->status;
    }

    if (status == TB_SYMBOL_ATTRIBUTE_MISSING) {
        constant->status = TB_SYMBOL_CONSTANT_ABSENT;
    } else if (status == TB_SYMBOL_ATTRIBUTE_AMBIGUOUS) {
        constant->status = TB_SYMBOL_CONSTANT_CONFLICT;
    } else if (status == TB_SYMBOL_ATTRIBUTE_UNIQUE) {
        constant->view.attribute = constant->attributes[constant->view.selection.first_index];

        const TbSymbolFormView_t *value = &constant->view.attribute.value;
        if (value->form == TB_SYMBOL_FORM_IMPLICIT_CONST && !value->indirect) {
            bool framed = constant->view.attribute.declared_form == TB_SYMBOL_FORM_IMPLICIT_CONST &&
                          !value->bytes && !value->payload_bytes && !value->numeric;

            constant->status = framed ? TB_SYMBOL_CONSTANT_SCHEMA : TB_SYMBOL_CONSTANT_MALFORMED;
        } else {
            constant->status = interpret(*value, &constant->view.value);
        }
    } else {
        constant->status = TB_SYMBOL_CONSTANT_INVALID;
    }

    return constant->status;
}


bool tb_symbol_constant_view(const TbSymbolConstant_t *constant, TbSymbolConstantView_t *view)
{
    if (!constant || !constant->initialized || !view || constant->status != TB_SYMBOL_CONSTANT_FOUND)
        return false;

    *view = constant->view;

    return true;
}


bool tb_symbol_constant_selection(const TbSymbolConstant_t *constant, TbSymbolAttributeResult_t *selection)
{
    if (!constant || !constant->initialized || !selection || constant->status == TB_SYMBOL_CONSTANT_PENDING ||
        constant->status == TB_SYMBOL_CONSTANT_INVALID) {
        return false;
    }

    *selection = constant->view.selection;

    return true;
}


TbSymbolLanguageFamily_e tb_symbol_language_family(uint64_t language)
{
    switch (language) {
    case 0x0001: /* C89 */
    case 0x0002: /* C */
    case 0x000c: /* C99 */
    case 0x001d: /* C11 */
    case 0x002c: /* C17 */
    case 0x003e: /* C23 */
        return TB_SYMBOL_LANGUAGE_C;
    case 0x0004: /* C++ */
    case 0x0019: /* C++03 */
    case 0x001a: /* C++11 */
    case 0x0021: /* C++14 */
    case 0x002a: /* C++17 */
    case 0x002b: /* C++20 */
    case 0x003a: /* C++23 */
        return TB_SYMBOL_LANGUAGE_CPP;
    default:
        return TB_SYMBOL_LANGUAGE_OTHER;
    }
}


bool tb_symbol_inline_kind(uint64_t value, TbSymbolInlineKind_e *kind)
{
    if (!kind || value > TB_SYMBOL_INLINE_DECLARED_INLINED)
        return false;

    *kind = (TbSymbolInlineKind_e)value;

    return true;
}
