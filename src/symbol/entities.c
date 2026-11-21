#include "symbol/entities.h"

static const uint64_t field_names[TB_SYMBOL_ENTITY_FIELDS] = {
    [TB_SYMBOL_ENTITY_NAME]          = 0x03,
    [TB_SYMBOL_ENTITY_LINKAGE]       = 0x6e,
    [TB_SYMBOL_ENTITY_DECLARATION]   = 0x3c,
    [TB_SYMBOL_ENTITY_EXTERNAL]      = 0x3f,
    [TB_SYMBOL_ENTITY_ARTIFICIAL]    = 0x34,
    [TB_SYMBOL_ENTITY_INLINE]        = 0x20,
    [TB_SYMBOL_ENTITY_SPECIFICATION] = 0x47,
    [TB_SYMBOL_ENTITY_ORIGIN]        = 0x31,
    [TB_SYMBOL_ENTITY_TYPE]          = 0x49
};

static TbSymbolEntityKind_e entry_kind(uint64_t tag)
{
    switch (tag) {
    case 0x2e:
        return TB_SYMBOL_ENTITY_FUNCTION;
    case 0x34:
        return TB_SYMBOL_ENTITY_DATA;
    case 0x1d:
        return TB_SYMBOL_ENTITY_INLINE_INSTANCE;
    default:
        return TB_SYMBOL_ENTITY_OTHER;
    }
}


TbSymbolEntityStatus_e tb_symbol_entity_init(TbSymbolEntity_t *entity, TbSymbolDiesView_t tree,
                                             uint64_t die_index)
{
    TbSymbolAttributes_t lookup;
    if (!entity || tb_symbol_attributes_init(&lookup, tree, (TbSymbolAttributeSelection_t){
        .die_index = die_index,
        .name      = field_names[0]
    }) != TB_SYMBOL_ATTRIBUTE_PENDING) {
        return TB_SYMBOL_ENTITY_INVALID;
    }
    const TbSymbolDieEntry_t *entry = &tree.entries[die_index];
    *entity = (TbSymbolEntity_t){
        .attributes    = tree.attributes,
        .cursor        = lookup.cursor,
        .end           = lookup.end,
        .view          = {
            .backing        = tree.backing,
            .section_index  = tree.section_index,
            .unit_offset    = tree.unit_offset,
            .record_offset  = entry->offset,
            .parent_index   = entry->parent,
            .tag            = entry->tag,
            .abbrev_section = tree.abbrev_section,
            .abbrev_offset  = tree.abbrev_offset,
            .kind           = entry_kind(entry->tag)
        },
        .status        = TB_SYMBOL_ENTITY_PENDING,
        .failure_field = TB_SYMBOL_ENTITY_FIELDS,
        .initialized   = true
    };
    for (size_t i = 0; i < TB_SYMBOL_ENTITY_FIELDS; ++i) {
        TbSymbolAttributeResult_t *selection = &entity->view.fields[i].selection;
        *selection = lookup.result;
        selection->name = field_names[i];
    }
    return entity->status;
}


static void collect(TbSymbolEntity_t *entity, uint64_t index)
{
    const TbSymbolDieAttribute_t *attribute = &entity->attributes[index];
    for (size_t i = 0; i < TB_SYMBOL_ENTITY_FIELDS; ++i) {
        if (attribute->name != field_names[i])
            continue;
        TbSymbolEntityField_t *field = &entity->view.fields[i];
        if (!field->selection.matches) {
            field->attribute              = *attribute;
            field->selection.first_index  = index;
            field->selection.first_offset = attribute->offset;
        } else if (field->selection.matches == 1) {
            field->selection.second_index  = index;
            field->selection.second_offset = attribute->offset;
        }
        ++field->selection.matches;
        break;
    }
}


static bool flag(TbSymbolEntity_t *entity, TbSymbolEntityField_e selected, TbSymbolEntityFlag_t *result)
{
    const TbSymbolEntityField_t *field = &entity->view.fields[selected];
    if (!field->selection.matches)
        return true;
    const TbSymbolFormView_t *value = &field->attribute.value;
    if ((value->form != TB_SYMBOL_FORM_FLAG && value->form != TB_SYMBOL_FORM_FLAG_PRESENT) ||
        !value->numeric || value->is_signed ||
        (value->form == TB_SYMBOL_FORM_FLAG && value->bits > UINT8_MAX) ||
        (value->form == TB_SYMBOL_FORM_FLAG_PRESENT && value->bits != 1)) {
        entity->status        = TB_SYMBOL_ENTITY_MALFORMED;
        entity->failure_field = selected;
        return false;
    }
    result->present = true;
    result->value   = value->bits != 0;
    return true;
}


TbSymbolEntityStatus_e tb_symbol_entity_step(TbSymbolEntity_t *entity, size_t max_steps)
{
    if (!entity || !entity->initialized)
        return TB_SYMBOL_ENTITY_INVALID;
    if (entity->status != TB_SYMBOL_ENTITY_PENDING)
        return entity->status;
    if (!max_steps || max_steps > 256) {
        entity->status = TB_SYMBOL_ENTITY_INVALID;
        return entity->status;
    }
    for (size_t step = 0; step < max_steps && entity->cursor < entity->end; ++step)
        collect(entity, entity->cursor++);
    if (entity->cursor < entity->end)
        return entity->status;
    for (size_t i = 0; i < TB_SYMBOL_ENTITY_FIELDS; ++i) {
        if (entity->view.fields[i].selection.matches > 1) {
            entity->status        = TB_SYMBOL_ENTITY_CONFLICT;
            entity->failure_field = (TbSymbolEntityField_e)i;
            return entity->status;
        }
    }
    if (flag(entity, TB_SYMBOL_ENTITY_DECLARATION, &entity->view.declaration) &&
        flag(entity, TB_SYMBOL_ENTITY_EXTERNAL, &entity->view.external) &&
        flag(entity, TB_SYMBOL_ENTITY_ARTIFICIAL, &entity->view.artificial)) {
        entity->status = TB_SYMBOL_ENTITY_READY;
    }
    return entity->status;
}


bool tb_symbol_entity_view(const TbSymbolEntity_t *entity, TbSymbolEntityView_t *view)
{
    if (!entity || !entity->initialized || !view || entity->status != TB_SYMBOL_ENTITY_READY)
        return false;
    *view = entity->view;
    return true;
}


bool tb_symbol_entity_failure(const TbSymbolEntity_t *entity, TbSymbolEntityField_e *field,
                              TbSymbolAttributeResult_t *selection)
{
    if (!entity || !entity->initialized || !field || !selection ||
        (entity->status != TB_SYMBOL_ENTITY_CONFLICT && entity->status != TB_SYMBOL_ENTITY_MALFORMED)) {
        return false;
    }
    *field = entity->failure_field;
    *selection = entity->view.fields[*field].selection;
    return true;
}


bool tb_symbol_entity_role(const TbSymbolEntity_t *entity, TbSymbolEntityRole_e *role)
{
    if (!entity || !entity->initialized || !role || entity->status != TB_SYMBOL_ENTITY_READY)
        return false;
    switch (entity->view.kind) {
    case TB_SYMBOL_ENTITY_FUNCTION:
    case TB_SYMBOL_ENTITY_DATA:
        *role = entity->view.declaration.present && entity->view.declaration.value
                    ? TB_SYMBOL_ROLE_DECLARATION : TB_SYMBOL_ROLE_DEFINITION;
        break;
    case TB_SYMBOL_ENTITY_INLINE_INSTANCE:
        *role = TB_SYMBOL_ROLE_INLINE_INSTANCE;
        break;
    default:
        *role = TB_SYMBOL_ROLE_OTHER;
        break;
    }
    return true;
}
