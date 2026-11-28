#include "symbol/attributes.h"

TbSymbolAttributeStatus_e tb_symbol_attributes_init(TbSymbolAttributes_t *lookup, TbSymbolDiesView_t tree,
                                                    TbSymbolAttributeSelection_t selection)
{
    if (!lookup || !tree.entries || selection.die_index >= tree.count || !selection.name ||
        (tree.attribute_count && !tree.attributes)) {
        return TB_SYMBOL_ATTRIBUTE_INVALID;
    }

    const TbSymbolDieEntry_t *die = &tree.entries[selection.die_index];
    uint64_t end;
    if (!tb_symbol_add(die->first_attribute, die->attribute_count, &end) || end > tree.attribute_count)
        return TB_SYMBOL_ATTRIBUTE_INVALID;

    *lookup = (TbSymbolAttributes_t) {
        .attributes  = tree.attributes,
        .cursor      = die->first_attribute,
        .end         = end,
        .result      = {
            .backing       = tree.backing,
            .section_index = tree.section_index,
            .unit_offset   = tree.unit_offset,
            .record_offset = die->offset,
            .name          = selection.name,
            .first_index   = UINT64_MAX,
            .second_index  = UINT64_MAX,
            .first_offset  = UINT64_MAX,
            .second_offset = UINT64_MAX
        },
        .status      = TB_SYMBOL_ATTRIBUTE_PENDING,
        .initialized = true
    };

    return lookup->status;
}


TbSymbolAttributeStatus_e tb_symbol_attributes_step(TbSymbolAttributes_t *lookup, size_t max_steps)
{
    if (!lookup || !lookup->initialized)
        return TB_SYMBOL_ATTRIBUTE_INVALID;

    if (lookup->status != TB_SYMBOL_ATTRIBUTE_PENDING)
        return lookup->status;

    if (!max_steps || max_steps > 256) {
        lookup->status = TB_SYMBOL_ATTRIBUTE_INVALID;
        return lookup->status;
    }

    for (size_t step = 0; step < max_steps && lookup->cursor < lookup->end; ++step) {
        uint64_t index = lookup->cursor++;
        const TbSymbolDieAttribute_t *attribute = &lookup->attributes[index];
        if (attribute->name != lookup->result.name)
            continue;

        if (!lookup->result.matches) {
            lookup->result.first_index = index;
            lookup->result.first_offset = attribute->offset;
        } else if (lookup->result.matches == 1) {
            lookup->result.second_index = index;
            lookup->result.second_offset = attribute->offset;
        }
        ++lookup->result.matches;
    }

    if (lookup->cursor == lookup->end) {
        if (!lookup->result.matches)
            lookup->status = TB_SYMBOL_ATTRIBUTE_MISSING;
        else if (lookup->result.matches == 1)
            lookup->status = TB_SYMBOL_ATTRIBUTE_UNIQUE;
        else
            lookup->status = TB_SYMBOL_ATTRIBUTE_AMBIGUOUS;
    }

    return lookup->status;
}


TbSymbolAttributeStatus_e tb_symbol_attributes_result(const TbSymbolAttributes_t *lookup,
                                                      TbSymbolAttributeResult_t *result)
{
    if (!lookup || !lookup->initialized || !result)
        return TB_SYMBOL_ATTRIBUTE_INVALID;

    if (lookup->status == TB_SYMBOL_ATTRIBUTE_UNIQUE || lookup->status == TB_SYMBOL_ATTRIBUTE_MISSING ||
        lookup->status == TB_SYMBOL_ATTRIBUTE_AMBIGUOUS) {
        *result = lookup->result;
    }

    return lookup->status;
}


bool tb_symbol_attributes_unique(const TbSymbolAttributes_t *lookup, uint64_t *index)
{
    if (!lookup || !lookup->initialized || !index || lookup->status != TB_SYMBOL_ATTRIBUTE_UNIQUE)
        return false;

    *index = lookup->result.first_index;

    return true;
}
