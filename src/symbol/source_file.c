#include "symbol/source_file.h"

#include <elf.h>
#include <string.h>

static bool same_header(TbSymbolLineHeader_t first, TbSymbolLineHeader_t second)
{
    return first.table_offset == second.table_offset && first.entries_offset == second.entries_offset &&
           first.program_offset == second.program_offset &&
           first.extent.end_offset == second.extent.end_offset &&
           first.extent.total_bytes == second.extent.total_bytes &&
           first.extent.payload_bytes == second.extent.payload_bytes &&
           first.extent.offset_bytes == second.extent.offset_bytes &&
           first.extent.length_bytes == second.extent.length_bytes && first.version == second.version &&
           first.byte_order == second.byte_order && first.address_bytes == second.address_bytes &&
           first.segment_bytes == second.segment_bytes &&
           first.minimum_instruction == second.minimum_instruction &&
           first.maximum_operations == second.maximum_operations && first.line_range == second.line_range &&
           first.opcode_base == second.opcode_base && first.line_base == second.line_base &&
           first.default_is_stmt == second.default_is_stmt &&
           !memcmp(first.operand_counts, second.operand_counts, sizeof(first.operand_counts));
}


static TbSymbolSourceTextRef_t absent_text(void)
{
    return (TbSymbolSourceTextRef_t) {
        .selection = {
            .descriptor = -1
        },
        .status = TB_SYMBOL_SOURCE_TEXT_ABSENT
    };
}


static bool select_fields(TbSymbolSourceFile_t *file, size_t ordinal, bool directory)
{
    if (ordinal >= file->files.directory_count + file->files.file_count)
        return false;

    TbSymbolLineEntry_t entry = file->files.entries[ordinal];
    size_t expected = directory ? ordinal : ordinal - file->files.directory_count;
    if (entry.directory != directory ||
        entry.index != expected + (file->files.header.version < 5 ? 1U : 0U) ||
        entry.first_field > file->files.field_count ||
        entry.field_count > file->files.field_count - entry.first_field) {
        return false;
    }

    file->cursor = entry.first_field;
    file->end    = entry.first_field + entry.field_count;
    if (!directory)
        file->view.file_offset = entry.offset;

    return true;
}


static TbSymbolSourceTextRef_t text_reference(TbSymbolSourceFile_t *file, TbSymbolLineField_t field)
{
    TbSymbolSourceTextRef_t result = {
        .selection = {
            .descriptor = -1
        },
        .field  = field,
        .status = TB_SYMBOL_SOURCE_TEXT_UNSUPPORTED
    };
    uint64_t section = file->sections.line;
    uint64_t offset;
    uint64_t end;
    if (field.value.form == TB_SYMBOL_FORM_STRING) {
        if (!tb_symbol_add(field.offset, field.value.payload_offset, &offset) ||
            !tb_symbol_add(offset, field.value.payload_bytes, &end) ||
            !tb_symbol_add(end, 1, &end) || end > file->files.header.program_offset) {
            file->status = TB_SYMBOL_SOURCE_FILE_MALFORMED;
            return result;
        }
    } else if (field.value.form == TB_SYMBOL_FORM_STRP || field.value.form == TB_SYMBOL_FORM_LINE_STRP) {
        if (!field.value.numeric || field.value.is_signed) {
            file->status = TB_SYMBOL_SOURCE_FILE_MALFORMED;
            return result;
        }
        section = field.value.form == TB_SYMBOL_FORM_STRP ? file->sections.strings :
                                                          file->sections.line_strings;
        if (section == UINT64_MAX) {
            result.status = TB_SYMBOL_SOURCE_TEXT_DEPENDENCY;
            return result;
        }
        if (section >= file->directory.count) {
            file->status = TB_SYMBOL_SOURCE_FILE_INVALID;
            return result;
        }
        offset = field.value.bits;
        end    = file->directory.entries[section].header.bytes;
    } else {
        return result;
    }

    TbSymbolElfSection_t header = file->directory.entries[section].header;
    if (header.type != SHT_PROGBITS || (header.flags & SHF_COMPRESSED))
        return result;
    if (offset >= end || end > header.bytes) {
        file->status = TB_SYMBOL_SOURCE_FILE_MALFORMED;
        return result;
    }

    result.selection.section_index = section;
    result.selection.offset        = offset;
    result.selection.end_offset    = end;
    result.status                  = TB_SYMBOL_SOURCE_TEXT_READY;

    return result;
}


TbSymbolSourceFileStatus_e tb_symbol_source_file_init(TbSymbolSourceFile_t *file,
    TbSymbolSectionsView_t directory, TbSymbolLineReferenceView_t reference,
    TbSymbolLineFilesView_t files, TbSymbolLineView_t program, TbSymbolSourceSections_t sections,
    TbSymbolSourceFileSelection_t selection)
{
    if (!file || !directory.header || !directory.entries || sections.line >= directory.count ||
        files.header.version < 2 || files.header.version > 5 ||
        (files.directory_count + files.file_count < files.directory_count) ||
        ((files.directory_count || files.file_count) && !files.entries) ||
        (files.field_count && !files.fields) || (program.row_count && !program.rows) ||
        (program.file_count && !program.files) || !same_header(files.header, program.header) ||
        files.header.table_offset != reference.line_offset ||
        directory.backing.key.device != reference.source.backing.key.device ||
        directory.backing.key.inode != reference.source.backing.key.inode ||
        directory.backing.bytes != reference.source.backing.bytes ||
        reference.unit.address_bytes != program.address_bytes ||
        reference.source.unit_offset >= reference.unit.extent.end_offset ||
        files.header.byte_order != directory.header->byte_order ||
        files.header.extent.end_offset > directory.entries[sections.line].header.bytes ||
        selection.kind < TB_SYMBOL_SOURCE_FILE_ROW || selection.kind > TB_SYMBOL_SOURCE_FILE_INDEX) {
        return TB_SYMBOL_SOURCE_FILE_INVALID;
    }

    TbSymbolElfSection_t line_section = directory.entries[sections.line].header;
    if (line_section.type != SHT_PROGBITS || (line_section.flags & SHF_COMPRESSED))
        return TB_SYMBOL_SOURCE_FILE_UNSUPPORTED;

    TbSymbolSourceFile_t candidate = {
        .view = {
            .reference    = reference,
            .selection    = selection,
            .line_section = sections.line,
            .line_version = files.header.version,
            .path         = absent_text(),
            .directory    = absent_text(),
            .compilation  = absent_text(),
            .partial      = files.partial
        },
        .directory   = directory,
        .files       = files,
        .sections    = sections,
        .status      = TB_SYMBOL_SOURCE_FILE_PENDING,
        .initialized = true
    };
    uint64_t selected = selection.index;
    uint64_t instruction = UINT64_MAX;
    if (selection.kind == TB_SYMBOL_SOURCE_FILE_ROW) {
        if (selected >= program.row_count)
            return TB_SYMBOL_SOURCE_FILE_INVALID;
        TbSymbolLineRow_t row = program.rows[selected];
        if (row.end_sequence) {
            candidate.status = TB_SYMBOL_SOURCE_FILE_ABSENT;
            *file = candidate;
            return file->status;
        }
        selected    = row.file;
        instruction = row.instruction_offset;
    } else if (!selected && selection.kind != TB_SYMBOL_SOURCE_FILE_INDEX) {
        candidate.status = TB_SYMBOL_SOURCE_FILE_ABSENT;
        *file = candidate;
        return file->status;
    }
    candidate.view.file_index = selected;

    unsigned first = files.header.version < 5 ? 1U : 0U;
    if (selected < first) {
        candidate.status = TB_SYMBOL_SOURCE_FILE_MALFORMED;
    } else if (selected - first < files.file_count) {
        if (!select_fields(&candidate, files.directory_count + (size_t)(selected - first), false))
            return TB_SYMBOL_SOURCE_FILE_INVALID;
    } else {
        uint64_t ordinal = selected - first - files.file_count;
        if (files.header.version == 5 || ordinal >= program.file_count) {
            candidate.status = TB_SYMBOL_SOURCE_FILE_MALFORMED;
        } else {
            TbSymbolLineFile_t event = program.files[ordinal];
            uint64_t end;
            if (event.instruction_offset >= instruction || event.name_offset < files.header.program_offset ||
                !tb_symbol_add(event.name_offset, event.name_bytes, &end) ||
                !tb_symbol_add(end, 1, &end) || end > files.header.extent.end_offset) {
                candidate.status = TB_SYMBOL_SOURCE_FILE_MALFORMED;
            } else {
                candidate.view.dynamic             = true;
                candidate.view.event               = event;
                candidate.view.file_offset         = event.instruction_offset;
                candidate.view.directory_index     = event.directory;
                candidate.view.directory_available = true;
                candidate.view.timestamp           = event.timestamp;
                candidate.view.bytes               = event.bytes;
                candidate.view.timestamp_available = true;
                candidate.view.bytes_available     = true;
                candidate.view.path = (TbSymbolSourceTextRef_t) {
                    .selection = {
                        .section_index = sections.line,
                        .offset        = event.name_offset,
                        .end_offset    = end,
                        .descriptor    = -1
                    },
                    .status = TB_SYMBOL_SOURCE_TEXT_READY
                };
            }
        }
    }

    *file = candidate;

    return file->status;
}


static void field(TbSymbolSourceFile_t *file, TbSymbolLineField_t value)
{
    if (value.content == 1) {
        TbSymbolSourceTextRef_t text = text_reference(file, value);
        if (file->phase == 0)
            file->view.path = text;
        else if (file->phase == 1)
            file->view.directory = text;
        else
            file->view.compilation = text;
    } else if (file->phase == 0 && value.content >= 2 && value.content <= 4) {
        if (!value.value.numeric || value.value.is_signed) {
            file->view.partial = true;
            return;
        }
        if (value.content == 2) {
            file->view.directory_index     = value.value.bits;
            file->view.directory_available = true;
        } else if (value.content == 3) {
            file->view.timestamp           = value.value.bits;
            file->view.timestamp_available = true;
        } else {
            file->view.bytes           = value.value.bits;
            file->view.bytes_available = true;
        }
    } else if (file->phase == 0 && value.content == 5) {
        uint64_t offset;
        uint64_t end;
        if (value.value.form != TB_SYMBOL_FORM_DATA16 || value.value.payload_bytes != 16 ||
            !tb_symbol_add(value.offset, value.value.payload_offset, &offset) ||
            !tb_symbol_add(offset, 16, &end) || end > file->files.header.program_offset) {
            file->status = TB_SYMBOL_SOURCE_FILE_MALFORMED;
        } else {
            file->view.md5_offset    = offset;
            file->view.md5_available = true;
        }
    }
}


static void next_scope(TbSymbolSourceFile_t *file)
{
    if (file->phase == 0) {
        file->phase = 1;
        if (file->view.directory_available) {
            uint64_t index = file->view.directory_index;
            unsigned first = file->files.header.version < 5 ? 1U : 0U;
            file->view.directory_current = index == 0;
            if (index < first) {
                file->cursor = file->end = 0;
                return;
            }
            if (index - first >= file->files.directory_count) {
                file->status = TB_SYMBOL_SOURCE_FILE_MALFORMED;
                return;
            }
            if (!select_fields(file, (size_t)(index - first), true))
                file->status = TB_SYMBOL_SOURCE_FILE_INVALID;
            return;
        }
    }

    if (file->phase < 2) {
        file->phase = 2;
        if (file->files.header.version == 5 && file->files.directory_count) {
            if (!select_fields(file, 0, true))
                file->status = TB_SYMBOL_SOURCE_FILE_INVALID;
            return;
        }
    }

    file->status = TB_SYMBOL_SOURCE_FILE_FOUND;
}


TbSymbolSourceFileStatus_e tb_symbol_source_file_step(TbSymbolSourceFile_t *file, size_t max_steps)
{
    if (!file || !file->initialized || !max_steps || max_steps > 256)
        return TB_SYMBOL_SOURCE_FILE_INVALID;

    for (size_t i = 0; i < max_steps && file->status == TB_SYMBOL_SOURCE_FILE_PENDING; ++i) {
        if (file->cursor == file->end)
            next_scope(file);
        else
            field(file, file->files.fields[file->cursor++]);
    }

    return file->status;
}


bool tb_symbol_source_file_view(const TbSymbolSourceFile_t *file, TbSymbolSourceFileView_t *view)
{
    if (!file || !file->initialized || !view || file->status != TB_SYMBOL_SOURCE_FILE_FOUND)
        return false;

    *view = file->view;

    return true;
}


void tb_symbol_source_file_discard(TbSymbolSourceFile_t *file)
{
    if (file)
        memset(file, 0, sizeof(*file));
}
