#include "symbol/code_attributes.h"

#include <string.h>

static const uint64_t names[TB_SYMBOL_CODE_FIELDS] = {0x11, 0x12, 0x55, 0x73, 0x74};

static uint64_t maximum(const TbSymbolCode_t *code)
{
    unsigned width = code->view.unit.address_bytes;
    return width == 8 ? UINT64_MAX : (UINT64_C(1) << (8U * width)) - 1;
}


TbSymbolCodeStatus_e tb_symbol_code_init(TbSymbolCode_t *code, TbSymbolUnitsView_t units,
    TbSymbolDiesView_t tree, TbSymbolCodeSelection_t selection)
{
    uint64_t unit_index = selection.unit_index;
    uint64_t die_index  = selection.die_index;

    if (!code || !units.entries || unit_index >= units.count || !tree.entries ||
        die_index >= tree.count || !units.entries[unit_index].header_available ||
        units.section_index != tree.section_index || units.entries[unit_index].offset != tree.unit_offset ||
        units.backing.key.device != tree.backing.key.device ||
        units.backing.key.inode != tree.backing.key.inode || units.backing.bytes != tree.backing.bytes) {
        return TB_SYMBOL_CODE_INVALID;
    }

    const TbSymbolDwarfUnit_t *unit  = &units.entries[unit_index].header;
    const TbSymbolDieEntry_t  *root  = &tree.entries[0];
    const TbSymbolDieEntry_t  *entry = &tree.entries[die_index];
    uint64_t                  first;
    bool root_kind = (tree.kind == TB_SYMBOL_DWARF_COMPILE && root->tag == 0x11) ||
                     (tree.kind == TB_SYMBOL_DWARF_PARTIAL && root->tag == 0x3c) ||
                     (tree.kind == TB_SYMBOL_DWARF_TYPE && root->tag == 0x41);
    bool unit_kind = unit->kind == tree.kind ||
                     (unit->kind == TB_SYMBOL_DWARF_COMPILE_OR_PARTIAL &&
                      (tree.kind == TB_SYMBOL_DWARF_COMPILE || tree.kind == TB_SYMBOL_DWARF_PARTIAL));

    if (!root_kind || !unit_kind || root->parent != UINT64_MAX ||
        (unit->version != 4 && unit->version != 5) ||
        (unit->extent.offset_bytes != 4 && unit->extent.offset_bytes != 8) ||
        tree.abbrev_offset != unit->abbrev_offset ||
        !tb_symbol_add(tree.unit_offset, unit->header_bytes, &first) || root->offset != first ||
        entry->offset < first || entry->end_offset <= entry->offset ||
        entry->end_offset > unit->extent.end_offset) {
        return TB_SYMBOL_CODE_INVALID;
    }
    if (unit->address_bytes != 1 && unit->address_bytes != 2 &&
        unit->address_bytes != 4 && unit->address_bytes != 8) {
        return TB_SYMBOL_CODE_UNSUPPORTED;
    }

    TbSymbolCode_t candidate = {
        .view = {
            .unit           = *unit,
            .abbrev_section = tree.abbrev_section,
            .abbrev_offset  = tree.abbrev_offset,
            .die_index      = die_index
        },
        .entry       = *entry,
        .attributes  = tree.attributes,
        .status      = TB_SYMBOL_CODE_PENDING,
        .initialized = true
    };

    for (unsigned field = 0; field < TB_SYMBOL_CODE_FIELDS; ++field) {
        if (tb_symbol_attributes_init(&candidate.lookup[field], tree, (TbSymbolAttributeSelection_t) {
            .die_index = die_index,
            .name      = names[field]
        }) != TB_SYMBOL_ATTRIBUTE_PENDING) {
            return TB_SYMBOL_CODE_INVALID;
        }
    }

    *code = candidate;

    return code->status;
}


static TbSymbolCodeStatus_e interpret(TbSymbolCode_t *code)
{
    TbSymbolCodeFact_t           *fact      = &code->view.fields[code->field];
    const TbSymbolDieAttribute_t *attribute = &fact->attribute;
    const TbSymbolFormView_t     *value     = &attribute->value;
    uint64_t                     end;
    uint64_t                     payload_end;

    if (attribute->offset <= code->entry.offset ||
        !tb_symbol_add(attribute->offset, value->bytes, &end) || end > code->entry.end_offset ||
        value->payload_offset < attribute->offset ||
        !tb_symbol_add(value->payload_offset, value->payload_bytes, &payload_end) || payload_end != end ||
        (value->indirect ? attribute->declared_form != TB_SYMBOL_FORM_INDIRECT :
                           attribute->declared_form != value->form)) {
        return TB_SYMBOL_CODE_MALFORMED;
    }

    bool address = value->form == TB_SYMBOL_FORM_ADDR;
    bool indexed = value->form == TB_SYMBOL_FORM_ADDRX ||
                   (value->form >= TB_SYMBOL_FORM_ADDRX1 && value->form <= TB_SYMBOL_FORM_ADDRX4);
    if (code->field == TB_SYMBOL_CODE_LOW || code->field == TB_SYMBOL_CODE_HIGH) {
        if (address || indexed) {
            if (indexed && code->view.unit.version != 5)
                return TB_SYMBOL_CODE_MALFORMED;
            if (!value->numeric)
                return TB_SYMBOL_CODE_UNSUPPORTED;
            if (value->is_signed || (address && (value->payload_bytes != code->view.unit.address_bytes ||
                                               value->bits > maximum(code)))) {
                return TB_SYMBOL_CODE_MALFORMED;
            }

            fact->kind = address ? TB_SYMBOL_CODE_ADDRESS : TB_SYMBOL_CODE_ADDRESS_INDEX;
        } else if (code->field == TB_SYMBOL_CODE_HIGH) {
            switch (value->form) {
            case TB_SYMBOL_FORM_DATA1:
            case TB_SYMBOL_FORM_DATA2:
            case TB_SYMBOL_FORM_DATA4:
            case TB_SYMBOL_FORM_DATA8:
            case TB_SYMBOL_FORM_UDATA:
            case TB_SYMBOL_FORM_SDATA:
            case TB_SYMBOL_FORM_IMPLICIT_CONST:
                break;
            case TB_SYMBOL_FORM_DATA16:
                return TB_SYMBOL_CODE_UNSUPPORTED;
            default:
                return TB_SYMBOL_CODE_MALFORMED;
            }
            if (value->form == TB_SYMBOL_FORM_IMPLICIT_CONST && !value->indirect) {
                if (code->view.unit.version != 5 || value->bytes || value->payload_bytes ||
                    value->numeric || value->is_signed) {
                    return TB_SYMBOL_CODE_MALFORMED;
                }
                return TB_SYMBOL_CODE_SCHEMA;
            }

            bool signed_form = value->form == TB_SYMBOL_FORM_SDATA ||
                               value->form == TB_SYMBOL_FORM_IMPLICIT_CONST;
            if (!value->numeric)
                return TB_SYMBOL_CODE_UNSUPPORTED;
            if (value->is_signed != signed_form || (signed_form && value->bits > INT64_MAX))
                return TB_SYMBOL_CODE_MALFORMED;

            fact->kind = TB_SYMBOL_CODE_LENGTH;
        } else {
            return TB_SYMBOL_CODE_MALFORMED;
        }
    } else {
        bool list_index = code->field == TB_SYMBOL_CODE_RANGES && value->form == TB_SYMBOL_FORM_RNGLISTX;
        if ((list_index || code->field != TB_SYMBOL_CODE_RANGES) && code->view.unit.version != 5)
            return TB_SYMBOL_CODE_MALFORMED;
        if (code->field != TB_SYMBOL_CODE_RANGES && code->view.die_index != 0)
            return TB_SYMBOL_CODE_MALFORMED;
        if ((!list_index && value->form != TB_SYMBOL_FORM_SEC_OFFSET) || !value->numeric ||
            value->is_signed ||
            (!list_index && value->payload_bytes != code->view.unit.extent.offset_bytes)) {
            return TB_SYMBOL_CODE_MALFORMED;
        }

        fact->kind = list_index ? TB_SYMBOL_CODE_LIST_INDEX :
                     code->field == TB_SYMBOL_CODE_RANGES ? TB_SYMBOL_CODE_LIST_OFFSET :
                                                           TB_SYMBOL_CODE_TABLE_BASE;
    }

    fact->value = value->bits;

    return TB_SYMBOL_CODE_PENDING;
}


static TbSymbolCodeStatus_e complete(TbSymbolCode_t *code)
{
    const TbSymbolCodeFact_t *low    = &code->view.fields[TB_SYMBOL_CODE_LOW];
    const TbSymbolCodeFact_t *high   = &code->view.fields[TB_SYMBOL_CODE_HIGH];
    const TbSymbolCodeFact_t *ranges = &code->view.fields[TB_SYMBOL_CODE_RANGES];

    if (high->kind != TB_SYMBOL_CODE_MISSING &&
        (low->kind == TB_SYMBOL_CODE_MISSING || ranges->kind != TB_SYMBOL_CODE_MISSING)) {
        return TB_SYMBOL_CODE_MALFORMED;
    }
    if (low->kind == TB_SYMBOL_CODE_ADDRESS &&
        (high->kind == TB_SYMBOL_CODE_ADDRESS || high->kind == TB_SYMBOL_CODE_LENGTH)) {
        uint64_t end = high->value;
        if (high->kind == TB_SYMBOL_CODE_LENGTH && !tb_symbol_add(low->value, high->value, &end))
            return TB_SYMBOL_CODE_MALFORMED;
        if (end < low->value || end > maximum(code))
            return TB_SYMBOL_CODE_MALFORMED;

        code->view.begin              = low->value;
        code->view.end                = end;
        code->view.interval_available = true;
    }

    return TB_SYMBOL_CODE_COMPLETE;
}


TbSymbolCodeStatus_e tb_symbol_code_schema(TbSymbolCode_t *code, const TbSymbolAbbrevView_t *schema)
{
    if (!code || !code->initialized || code->status != TB_SYMBOL_CODE_SCHEMA)
        return TB_SYMBOL_CODE_INVALID;
    if (!schema) {
        code->status = TB_SYMBOL_CODE_DEPENDENCY;
        return code->status;
    }

    const TbSymbolCodeFact_t *fact = &code->view.fields[TB_SYMBOL_CODE_HIGH];
    TbSymbolStorageInfo_t backing = fact->selection.backing;
    if (!schema->entries || !schema->count || !schema->attributes || schema->version != 5 ||
        schema->section_index != code->view.abbrev_section ||
        schema->table_offset != code->view.abbrev_offset ||
        schema->backing.key.device != backing.key.device || schema->backing.key.inode != backing.key.inode ||
        schema->backing.bytes != backing.bytes ||
        fact->attribute.schema_attribute >= schema->attribute_count) {
        return TB_SYMBOL_CODE_INVALID;
    }

    const TbSymbolAbbrevAttribute_t *field = &schema->attributes[fact->attribute.schema_attribute];
    uint64_t end;
    if (field->name != names[TB_SYMBOL_CODE_HIGH] || field->form != TB_SYMBOL_FORM_IMPLICIT_CONST ||
        field->offset < schema->table_offset || field->offset >= schema->end_offset ||
        field->implicit_offset <= field->offset || !field->implicit_bytes ||
        !tb_symbol_add(field->implicit_offset, field->implicit_bytes, &end) || end > schema->end_offset) {
        return TB_SYMBOL_CODE_INVALID;
    }

    code->schema           = *schema;
    code->schema_available = true;
    code->status           = TB_SYMBOL_CODE_PENDING;

    return code->status;
}


static TbSymbolCodeStatus_e implicit(TbSymbolCode_t *code, size_t max_steps)
{
    TbSymbolCodeFact_t *fact = &code->view.fields[TB_SYMBOL_CODE_HIGH];
    for (size_t step = 0; step < max_steps && code->schema_cursor < code->schema.count; ++step) {
        uint64_t index = code->schema_cursor++;
        const TbSymbolAbbrevEntry_t *entry = &code->schema.entries[index];
        if (entry->code != code->entry.code)
            continue;

        uint64_t expected;
        uint64_t ordinal = fact->selection.first_index - code->entry.first_attribute;
        if (entry->tag != code->entry.tag || entry->children != code->entry.children ||
            entry->attribute_count != code->entry.attribute_count ||
            !tb_symbol_add(entry->first_attribute, ordinal, &expected) ||
            expected != fact->attribute.schema_attribute) {
            return TB_SYMBOL_CODE_MALFORMED;
        }

        const TbSymbolAbbrevAttribute_t *field = &code->schema.attributes[expected];
        if (!field->implicit_fits)
            return TB_SYMBOL_CODE_UNSUPPORTED;
        if (field->implicit_value < 0)
            return TB_SYMBOL_CODE_MALFORMED;

        fact->abbreviation      = *field;
        fact->schema_entry      = index;
        fact->from_abbreviation = true;
        fact->kind              = TB_SYMBOL_CODE_LENGTH;
        fact->value             = (uint64_t)field->implicit_value;
        code->schema_available  = false;
        ++code->field;

        return TB_SYMBOL_CODE_PENDING;
    }

    return code->schema_cursor == code->schema.count ? TB_SYMBOL_CODE_MALFORMED : TB_SYMBOL_CODE_PENDING;
}


TbSymbolCodeStatus_e tb_symbol_code_step(TbSymbolCode_t *code, size_t max_steps)
{
    if (!code || !code->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_CODE_INVALID;
    if (code->status != TB_SYMBOL_CODE_PENDING)
        return code->status;

    if (code->schema_available) {
        code->status = implicit(code, max_steps);
    } else if (code->field == TB_SYMBOL_CODE_FIELDS) {
        code->status = complete(code);
    } else if (code->field < TB_SYMBOL_CODE_FIELDS) {
        TbSymbolAttributes_t *lookup = &code->lookup[code->field];
        TbSymbolAttributeStatus_e status = tb_symbol_attributes_step(lookup, max_steps);
        if (status == TB_SYMBOL_ATTRIBUTE_PENDING)
            return code->status;

        TbSymbolCodeFact_t *fact = &code->view.fields[code->field];
        (void)tb_symbol_attributes_result(lookup, &fact->selection);
        if (status == TB_SYMBOL_ATTRIBUTE_AMBIGUOUS) {
            code->status = TB_SYMBOL_CODE_CONFLICT;
        } else if (status == TB_SYMBOL_ATTRIBUTE_MISSING) {
            ++code->field;
        } else if (status == TB_SYMBOL_ATTRIBUTE_UNIQUE) {
            fact->attribute = code->attributes[fact->selection.first_index];
            code->status = interpret(code);
            if (code->status == TB_SYMBOL_CODE_PENDING)
                ++code->field;
        } else {
            code->status = TB_SYMBOL_CODE_INVALID;
        }
    } else {
        code->status = TB_SYMBOL_CODE_INVALID;
    }

    return code->status;
}


bool tb_symbol_code_view(const TbSymbolCode_t *code, TbSymbolCodeView_t *view)
{
    if (!code || !code->initialized || !view || code->status != TB_SYMBOL_CODE_COMPLETE)
        return false;

    *view = code->view;

    return true;
}


bool tb_symbol_code_selection(const TbSymbolCode_t *code, TbSymbolCodeField_e field,
    TbSymbolAttributeResult_t *selection)
{
    if (!code || !code->initialized || (unsigned)field >= TB_SYMBOL_CODE_FIELDS || !selection)
        return false;

    TbSymbolAttributeStatus_e status = tb_symbol_attributes_result(&code->lookup[field], selection);
    return status == TB_SYMBOL_ATTRIBUTE_UNIQUE || status == TB_SYMBOL_ATTRIBUTE_MISSING ||
           status == TB_SYMBOL_ATTRIBUTE_AMBIGUOUS;
}


void tb_symbol_code_discard(TbSymbolCode_t *code)
{
    if (code)
        memset(code, 0, sizeof(*code));
}
