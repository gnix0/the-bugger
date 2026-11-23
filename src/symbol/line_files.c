#include "symbol/line_files.h"

#include <string.h>

enum {
    FILES_LEGACY,
    FILES_FORMAT_COUNT,
    FILES_CONTENT,
    FILES_FORM,
    FILES_UNIQUENESS,
    FILES_COUNT,
    FILES_ENTRY,
    FILES_FIELD,
    FILES_VALUE,
    FILES_DONE
};

static void fail(TbSymbolLineFiles_t *files, TbSymbolLineFilesStatus_e status)
{
    files->status  = status;
    files->failure = (TbSymbolLineFilesFailure_t) {
        .status      = status,
        .offset      = files->cursor,
        .entry_index = files->ordinal,
        .directory   = files->table == 0
    };
    bool     field = files->phase == FILES_FIELD || files->phase == FILES_VALUE;
    unsigned index = field ? files->field_index : files->format_index;

    if (index < files->format_count[files->table]) {
        const TbSymbolLineFormat_t *format = &files->formats[files->table][index];
        files->failure.content         = format->content;
        files->failure.form            = format->form;
        files->failure.field_offset    = field ? files->field_offset : format->offset;
        files->failure.field_available = true;
    }
}


static bool allowed_form(uint64_t form)
{
    switch (form) {
    case TB_SYMBOL_FORM_BLOCK:
    case TB_SYMBOL_FORM_BLOCK1:
    case TB_SYMBOL_FORM_BLOCK2:
    case TB_SYMBOL_FORM_BLOCK4:
    case TB_SYMBOL_FORM_DATA1:
    case TB_SYMBOL_FORM_DATA2:
    case TB_SYMBOL_FORM_DATA4:
    case TB_SYMBOL_FORM_DATA8:
    case TB_SYMBOL_FORM_DATA16:
    case TB_SYMBOL_FORM_FLAG:
    case TB_SYMBOL_FORM_SDATA:
    case TB_SYMBOL_FORM_SEC_OFFSET:
    case TB_SYMBOL_FORM_STRING:
    case TB_SYMBOL_FORM_STRP:
    case TB_SYMBOL_FORM_LINE_STRP:
    case TB_SYMBOL_FORM_STRP_SUP:
    case TB_SYMBOL_FORM_STRX:
    case TB_SYMBOL_FORM_STRX1:
    case TB_SYMBOL_FORM_STRX2:
    case TB_SYMBOL_FORM_STRX3:
    case TB_SYMBOL_FORM_STRX4:
    case TB_SYMBOL_FORM_UDATA:
        return true;
    default:
        return false;
    }
}


static bool pairing(uint64_t content, uint64_t form)
{
    switch (content) {
    case 1:
        return form == TB_SYMBOL_FORM_STRING || form == TB_SYMBOL_FORM_STRP ||
               form == TB_SYMBOL_FORM_LINE_STRP || form == TB_SYMBOL_FORM_STRP_SUP ||
               form == TB_SYMBOL_FORM_STRX || (form >= TB_SYMBOL_FORM_STRX1 && form <= TB_SYMBOL_FORM_STRX4);
    case 2:
        return form == TB_SYMBOL_FORM_DATA1 || form == TB_SYMBOL_FORM_DATA2 || form == TB_SYMBOL_FORM_UDATA;
    case 3:
        return form == TB_SYMBOL_FORM_UDATA || form == TB_SYMBOL_FORM_DATA4 ||
               form == TB_SYMBOL_FORM_DATA8 || form == TB_SYMBOL_FORM_BLOCK;
    case 4:
        return form == TB_SYMBOL_FORM_UDATA || form == TB_SYMBOL_FORM_DATA1 ||
               form == TB_SYMBOL_FORM_DATA2 || form == TB_SYMBOL_FORM_DATA4 || form == TB_SYMBOL_FORM_DATA8;
    case 5:
        return form == TB_SYMBOL_FORM_DATA16;
    default:
        return true;
    }
}


static void begin_leb(TbSymbolLineFiles_t *files, unsigned phase)
{
    (void)tb_symbol_leb_init(&files->leb, false);
    files->phase = phase;
}


TbSymbolLineFilesStatus_e tb_symbol_line_files_init(TbSymbolLineFiles_t *files, TbSymbolLineHeader_t header,
                                                    TbSymbolLineEntry_t *entries, size_t entry_capacity,
                                                    TbSymbolLineField_t *fields, size_t field_capacity)
{
    if (!files || !entries || !fields || entry_capacity > SIZE_MAX / sizeof(*entries) ||
        field_capacity > SIZE_MAX / sizeof(*fields) || header.version < 2 || header.version > 5 ||
        header.entries_offset <= header.table_offset || header.entries_offset > header.program_offset ||
        header.program_offset > header.extent.end_offset ||
        (header.extent.offset_bytes != 4 && header.extent.offset_bytes != 8) ||
        (header.byte_order != TB_SYMBOL_LITTLE_ENDIAN && header.byte_order != TB_SYMBOL_BIG_ENDIAN) ||
        (header.version == 5 && (!header.address_bytes || header.address_bytes > 255))) {
        return TB_SYMBOL_LINE_FILES_INVALID;
    }

    *files = (TbSymbolLineFiles_t) {
        .view = {
            .header  = header,
            .entries = entries,
            .fields  = fields
        },
        .entries        = entries,
        .fields         = fields,
        .status         = TB_SYMBOL_LINE_FILES_PENDING,
        .cursor         = header.entries_offset,
        .entry_capacity = entry_capacity,
        .field_capacity = field_capacity,
        .phase          = header.version == 5 ? FILES_FORMAT_COUNT : FILES_LEGACY,
        .initialized    = true
    };

    files->view.directory_format = files->formats[0];
    files->view.file_format      = files->formats[1];

    if (header.version < 5) {
        files->format_count[0] = 1;
        files->format_count[1] = 4;

        for (unsigned table = 0; table < 2; ++table) {
            for (unsigned index = 0; index < files->format_count[table]; ++index) {
                files->formats[table][index] = (TbSymbolLineFormat_t) {
                    .offset  = UINT64_MAX,
                    .content = index + 1,
                    .form    = index ? TB_SYMBOL_FORM_UDATA : TB_SYMBOL_FORM_STRING
                };
            }
        }
    }

    return files->status;
}


static void next_table(TbSymbolLineFiles_t *files)
{
    if (files->table == 1) {
        files->phase = FILES_DONE;
    } else {
        files->table        = 1;
        files->ordinal      = 0;
        files->format_index = 0;
        files->has_path     = false;
        files->phase        = files->view.header.version == 5 ? FILES_FORMAT_COUNT : FILES_LEGACY;
    }
}


static void start_entry(TbSymbolLineFiles_t *files)
{
    size_t   count  = files->view.directory_count + files->view.file_count;
    unsigned fields = files->format_count[files->table];

    if (count == files->entry_capacity || fields > files->field_capacity - files->view.field_count) {
        fail(files, TB_SYMBOL_LINE_FILES_RESOURCE);
        return;
    }

    files->entries[count] = (TbSymbolLineEntry_t) {
        .index       = files->ordinal + (files->view.header.version < 5 ? 1 : 0),
        .offset      = files->cursor,
        .first_field = files->view.field_count,
        .field_count = fields,
        .directory   = files->table == 0
    };

    files->field_index = 0;
    files->phase       = FILES_FIELD;
}


static void start_field(TbSymbolLineFiles_t *files)
{
    if (files->field_index == files->format_count[files->table]) {
        if (files->table == 0)
            ++files->view.directory_count;
        else
            ++files->view.file_count;

        ++files->ordinal;
        if (files->view.header.version < 5)
            files->phase = FILES_LEGACY;
        else if (--files->remaining)
            files->phase = FILES_ENTRY;
        else
            next_table(files);
        return;
    }

    const TbSymbolLineFormat_t *format = &files->formats[files->table][files->field_index];
    files->field_offset = files->cursor;

    if (tb_symbol_form_init(&files->form, (TbSymbolFormRequest_t) {
        .form          = format->form,
        .available     = files->view.header.program_offset - files->cursor,
        .version       = files->view.header.version == 5 ? 5 : 4,
        .offset_bytes  = files->view.header.extent.offset_bytes,
        .address_bytes = files->view.header.version == 5 ? files->view.header.address_bytes : 1,
        .byte_order    = files->view.header.byte_order
    }) != TB_SYMBOL_FORM_PENDING) {
        fail(files, TB_SYMBOL_LINE_FILES_MALFORMED);
    } else {
        files->phase = FILES_VALUE;
    }
}


static TbSymbolLineFieldProfile_e profile(uint64_t content, TbSymbolFormView_t value)
{
    if (content == 1) {
        if (value.form == TB_SYMBOL_FORM_STRING)
            return TB_SYMBOL_LINE_FIELD_VALUE;
        if (value.form == TB_SYMBOL_FORM_STRP || value.form == TB_SYMBOL_FORM_LINE_STRP)
            return TB_SYMBOL_LINE_FIELD_REFERENCE;
    } else if (content == 5 || (content >= 2 && content <= 4 &&
                                (value.numeric || (content == 3 && value.form == TB_SYMBOL_FORM_BLOCK)))) {
        return TB_SYMBOL_LINE_FIELD_VALUE;
    }

    return TB_SYMBOL_LINE_FIELD_UNSUPPORTED;
}


static void value_byte(TbSymbolLineFiles_t *files, unsigned char byte)
{
    size_t               consumed;
    TbSymbolFormStatus_e status = tb_symbol_form_feed(&files->form, &byte, 1, &consumed);

    if (status == TB_SYMBOL_FORM_PENDING)
        return;

    if (status != TB_SYMBOL_FORM_COMPLETE) {
        fail(files, status == TB_SYMBOL_FORM_UNSUPPORTED ? TB_SYMBOL_LINE_FILES_UNSUPPORTED :
                                                        TB_SYMBOL_LINE_FILES_MALFORMED);
        return;
    }

    TbSymbolFormView_t value;
    if (!tb_symbol_form_view(&files->form, &value)) {
        fail(files, TB_SYMBOL_LINE_FILES_INVALID);
        return;
    }

    uint64_t                  content = files->formats[files->table][files->field_index].content;
    TbSymbolLineFieldProfile_e meaning = profile(content, value);

    files->fields[files->view.field_count++] = (TbSymbolLineField_t) {
        .offset  = files->field_offset,
        .content = content,
        .value   = value,
        .profile = meaning
    };

    if (meaning == TB_SYMBOL_LINE_FIELD_UNSUPPORTED)
        files->view.partial = true;
    ++files->field_index;
    files->phase = FILES_FIELD;
}


static void number(TbSymbolLineFiles_t *files, uint64_t value)
{
    unsigned table = files->table;

    if (files->phase == FILES_CONTENT) {
        TbSymbolLineFormat_t *format = &files->formats[table][files->format_index];
        format->content = value;
        begin_leb(files, FILES_FORM);
    } else if (files->phase == FILES_FORM) {
        TbSymbolLineFormat_t *format = &files->formats[table][files->format_index];
        format->form = value;
        bool known_form = value >= TB_SYMBOL_FORM_ADDR && value <= TB_SYMBOL_FORM_ADDRX4 && value != 2;
        if (known_form && !pairing(format->content, value)) {
            fail(files, TB_SYMBOL_LINE_FILES_MALFORMED);
        } else if (!allowed_form(value)) {
            fail(files, TB_SYMBOL_LINE_FILES_UNSUPPORTED);
        } else {
            files->scan  = 0;
            files->phase = FILES_UNIQUENESS;
        }
    } else if ((!files->format_count[table] || !files->has_path) && value) {
        fail(files, TB_SYMBOL_LINE_FILES_MALFORMED);
    } else if (value > files->entry_capacity - files->view.directory_count - files->view.file_count ||
               (files->format_count[table] &&
                value > (files->field_capacity - files->view.field_count) / files->format_count[table])) {
        fail(files, TB_SYMBOL_LINE_FILES_RESOURCE);
    } else {
        files->remaining = value;
        if (value)
            files->phase = FILES_ENTRY;
        else
            next_table(files);
    }
}


static void schema_byte(TbSymbolLineFiles_t *files, unsigned char byte)
{
    size_t               consumed;
    TbSymbolReadStatus_e status = tb_symbol_leb_feed(&files->leb, &byte, 1, &consumed);

    if (status == TB_SYMBOL_READ_LIMIT)
        return;

    uint64_t value;
    if (status != TB_SYMBOL_READ_OK || !tb_symbol_leb_uint(&files->leb, &value))
        fail(files, TB_SYMBOL_LINE_FILES_UNSUPPORTED);
    else
        number(files, value);
}


static void uniqueness(TbSymbolLineFiles_t *files)
{
    const TbSymbolLineFormat_t *format = &files->formats[files->table][files->format_index];

    if (format->content >= 1 && format->content <= 5 && files->scan < files->format_index) {
        if (files->formats[files->table][files->scan++].content == format->content)
            fail(files, TB_SYMBOL_LINE_FILES_CONFLICT);
        return;
    }

    if (format->content == 1)
        files->has_path = true;
    if (++files->format_index == files->format_count[files->table]) {
        begin_leb(files, FILES_COUNT);
    } else {
        files->formats[files->table][files->format_index].offset = files->cursor;
        begin_leb(files, FILES_CONTENT);
    }
}


TbSymbolLineFilesStatus_e tb_symbol_line_files_feed(TbSymbolLineFiles_t *files, const void *bytes,
                                                    size_t length, size_t *consumed)
{
    if (!files || !files->initialized || !consumed || (length && !bytes))
        return TB_SYMBOL_LINE_FILES_INVALID;

    const unsigned char *input = bytes;

    *consumed = 0;

    for (unsigned work = 0; work < 256 && files->status == TB_SYMBOL_LINE_FILES_PENDING; ++work) {
        bool internal = files->phase == FILES_UNIQUENESS || files->phase == FILES_ENTRY ||
                        files->phase == FILES_FIELD || files->phase == FILES_DONE;

        if (!internal && files->cursor == files->view.header.program_offset) {
            fail(files, TB_SYMBOL_LINE_FILES_MALFORMED);
            break;
        }
        if (!internal && *consumed == length)
            break;

        if (files->progress == UINT64_MAX) {
            fail(files, TB_SYMBOL_LINE_FILES_RESOURCE);
            break;
        }
        ++files->progress;

        if (files->phase == FILES_UNIQUENESS) {
            uniqueness(files);
        } else if (files->phase == FILES_ENTRY) {
            start_entry(files);
        } else if (files->phase == FILES_FIELD) {
            start_field(files);
        } else if (files->phase == FILES_DONE) {
            if (files->cursor != files->view.header.program_offset) {
                fail(files, TB_SYMBOL_LINE_FILES_MALFORMED);
            } else {
                files->view.directory_formats = files->format_count[0];
                files->view.file_formats      = files->format_count[1];
                files->status = files->view.partial ? TB_SYMBOL_LINE_FILES_PARTIAL :
                                                    TB_SYMBOL_LINE_FILES_COMPLETE;
            }
        } else if (files->phase == FILES_LEGACY) {
            if (!input[*consumed]) {
                ++*consumed;
                ++files->cursor;
                next_table(files);
            } else {
                start_entry(files);
            }
        } else {
            unsigned char byte = input[(*consumed)++];
            ++files->cursor;

            if (files->phase == FILES_FORMAT_COUNT) {
                files->format_count[files->table] = byte;
                files->format_index = 0;
                if (byte) {
                    files->formats[files->table][0].offset = files->cursor;
                    begin_leb(files, FILES_CONTENT);
                } else {
                    begin_leb(files, FILES_COUNT);
                }
            } else if (files->phase == FILES_VALUE) {
                value_byte(files, byte);
            } else {
                schema_byte(files, byte);
            }
        }
    }

    return files->status;
}


TbSymbolLineFilesStatus_e tb_symbol_line_files_finish(TbSymbolLineFiles_t *files)
{
    if (!files || !files->initialized)
        return TB_SYMBOL_LINE_FILES_INVALID;

    if (files->status == TB_SYMBOL_LINE_FILES_PENDING) {
        size_t consumed;
        (void)tb_symbol_line_files_feed(files, NULL, 0, &consumed);
        if (files->status == TB_SYMBOL_LINE_FILES_PENDING)
            fail(files, TB_SYMBOL_LINE_FILES_MALFORMED);
    }

    return files->status;
}


bool tb_symbol_line_files_view(const TbSymbolLineFiles_t *files, TbSymbolLineFilesView_t *view)
{
    if (!files || !files->initialized || !view ||
        (files->status != TB_SYMBOL_LINE_FILES_COMPLETE && files->status != TB_SYMBOL_LINE_FILES_PARTIAL)) {
        return false;
    }

    *view = files->view;
    return true;
}


bool tb_symbol_line_files_failure(const TbSymbolLineFiles_t *files, TbSymbolLineFilesFailure_t *failure)
{
    if (!files || !files->initialized || !failure || files->status < TB_SYMBOL_LINE_FILES_MALFORMED)
        return false;

    *failure = files->failure;
    return true;
}


uint64_t tb_symbol_line_files_progress(const TbSymbolLineFiles_t *files)
{
    return files && files->initialized ? files->progress : 0;
}


void tb_symbol_line_files_discard(TbSymbolLineFiles_t *files)
{
    if (files)
        memset(files, 0, sizeof(*files));
}
