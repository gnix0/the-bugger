#include "symbol/identity.h"

bool tb_symbol_record_valid(const TbSymbolRecordId_t *record)
{
    if (!record || !record->content_id || !record->section_index)
        return false;

    if (record->kind == TB_SYMBOL_RECORD_DIE)
        return record->unit_offset <= record->record_offset;

    if (record->kind == TB_SYMBOL_RECORD_SYMTAB || record->kind == TB_SYMBOL_RECORD_DYNSYM)
        return !record->unit_offset;

    return false;
}


bool tb_symbol_record_equal(const TbSymbolRecordId_t *left, const TbSymbolRecordId_t *right)
{
    return tb_symbol_record_valid(left) && tb_symbol_record_valid(right) &&
           left->content_id == right->content_id && left->kind == right->kind &&
           left->section_index == right->section_index && left->unit_offset == right->unit_offset &&
           left->record_offset == right->record_offset;
}
