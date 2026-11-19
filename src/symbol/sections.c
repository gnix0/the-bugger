#include "symbol/sections.h"

#include <elf.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

typedef enum {
    SECTIONS_BEGIN,
    SECTIONS_HEADER,
    SECTIONS_ZERO,
    SECTIONS_ALLOCATE,
    SECTIONS_NEXT,
    SECTIONS_RECORD,
    SECTIONS_SORT,
    SECTIONS_OVERLAP,
    SECTIONS_NAME_TABLE,
    SECTIONS_NAME_FIRST,
    SECTIONS_NAME_LAST,
    SECTIONS_NAME_NEXT,
    SECTIONS_NAME_SCAN,
    SECTIONS_DONE,
    SECTIONS_FAILED
} SectionsPhase_e;

typedef struct {
    uint64_t offset;
    uint64_t bytes;
} ByteSpan_t;

struct tb_symbol_sections {
    TbSymbolBudget_t     *construction;
    TbSymbolFile_t       *file;
    TbSymbolStorageInfo_t expected;

    TbSymbolElfHeader_t     header;
    TbSymbolElfLayout_t     layout;
    TbSymbolElfSection_t    zero;
    TbSymbolSectionEntry_t *entries;
    uint64_t              *order;
    size_t                 array_bytes;
    uint64_t               index;

    uint64_t order_count;
    uint64_t sort_build;
    uint64_t sort_remaining;
    uint64_t heap;
    uint64_t root;
    bool     sifting;

    bool     reading;
    uint64_t previous_end;
    uint64_t name_cursor;
    size_t   scan_at;
    uint64_t progress;

    TbSymbolSectionNames_e     names;
    SectionsPhase_e            phase;
    TbSymbolOutcome_e          outcome;
    TbSymbolSectionsFailure_t  failure;
};

TbSymbolSections_t *tb_symbol_sections_create(TbSymbolStorageInfo_t expected, int descriptor,
                                             TbSymbolBudget_t *construction)
{
    if (!construction || descriptor < 0 || expected.bytes > INT64_MAX ||
        !tb_symbol_budget_reserve(construction, sizeof(TbSymbolSections_t))) {
        return NULL;
    }
    TbSymbolSections_t *sections = calloc(1, sizeof(*sections));
    if (!sections) {
        (void)tb_symbol_budget_release(construction, sizeof(TbSymbolSections_t));
        return NULL;
    }
    sections->construction = construction;
    sections->expected     = expected;
    sections->index        = UINT64_MAX;
    sections->file = tb_symbol_file_create(expected, descriptor, construction);
    if (!sections->file) {
        tb_symbol_sections_destroy(sections);
        return NULL;
    }
    return sections;
}


static void fail(TbSymbolSections_t *sections, TbSymbolSectionsFailure_e reason, TbSymbolOutcome_e outcome)
{
    sections->failure.reason        = reason;
    sections->failure.section_index = sections->index;
    sections->outcome               = outcome;
    sections->phase                 = SECTIONS_FAILED;
}


static bool decoded(TbSymbolSections_t *sections, TbSymbolElfStatus_e status,
                     TbSymbolSectionsFailure_e reason)
{
    if (status == TB_SYMBOL_ELF_OK)
        return true;
    fail(sections, reason, status == TB_SYMBOL_ELF_UNSUPPORTED ? TB_SYMBOL_UNSUPPORTED :
                          status == TB_SYMBOL_ELF_MALFORMED ? TB_SYMBOL_MALFORMED :
                                                             TB_SYMBOL_OPERATIONAL_FAILURE);
    return false;
}


static void request(TbSymbolSections_t *sections, ByteSpan_t span, SectionsPhase_e next)
{
    if (!span.bytes || span.bytes > TB_SYMBOL_FILE_WINDOW_BYTES ||
        !tb_symbol_range(span.offset, span.bytes, sections->expected.bytes)) {
        fail(sections, TB_SYMBOL_SECTIONS_READ, TB_SYMBOL_MALFORMED);
    } else if (!tb_symbol_file_request(sections->file, (TbSymbolFileRange_t){
        .offset = span.offset,
        .bytes  = (size_t)span.bytes
    })) {
        fail(sections, TB_SYMBOL_SECTIONS_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
    } else {
        sections->phase = next;
        sections->reading = true;
    }
}


static TbSymbolFileView_t bytes(TbSymbolSections_t *sections)
{
    TbSymbolFileView_t view = {0};
    if (!tb_symbol_file_view(sections->file, &view))
        fail(sections, TB_SYMBOL_SECTIONS_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
    return view;
}


static bool overlap(ByteSpan_t left, ByteSpan_t right)
{
    uint64_t left_end;
    uint64_t right_end;
    if (!left.bytes || !right.bytes)
        return false;
    if (!tb_symbol_add(left.offset, left.bytes, &left_end) ||
        !tb_symbol_add(right.offset, right.bytes, &right_end)) {
        return true;
    }
    return left.offset < right_end && right.offset < left_end;
}


static bool header_overlap(TbSymbolSections_t *sections, const TbSymbolElfSection_t *record)
{
    ByteSpan_t data = {
        .offset = record->offset,
        .bytes  = record->bytes
    };
    ByteSpan_t header = {
        .offset = 0,
        .bytes  = sections->header.header_bytes
    };
    ByteSpan_t section_table = {
        .offset = sections->layout.sections.offset
    };
    ByteSpan_t program_table = {
        .offset = sections->layout.programs.offset
    };
    if (!tb_symbol_multiply(sections->layout.sections.count, sections->layout.sections.stride,
                            &section_table.bytes) ||
        !tb_symbol_multiply(sections->layout.programs.count, sections->layout.programs.stride,
                            &program_table.bytes)) {
        return true;
    }
    return overlap(data, header) || overlap(data, section_table) || overlap(data, program_table);
}


static void allocate_entries(TbSymbolSections_t *sections)
{
    uint64_t entry_bytes;
    uint64_t order_bytes;
    uint64_t total;
    uint64_t count = sections->layout.sections.count;
    if (!count) {
        sections->phase = SECTIONS_DONE;
        sections->outcome = TB_SYMBOL_SUCCESS;
        return;
    }
    if (!tb_symbol_multiply(count, sizeof(*sections->entries), &entry_bytes) ||
        !tb_symbol_multiply(count, sizeof(*sections->order), &order_bytes) ||
        !tb_symbol_add(entry_bytes, order_bytes, &total) || total > SIZE_MAX ||
        !tb_symbol_budget_reserve(sections->construction, (size_t)total)) {
        fail(sections, TB_SYMBOL_SECTIONS_RESOURCE, TB_SYMBOL_RESOURCE_FAILURE);
        return;
    }
    sections->array_bytes = (size_t)total;
    sections->entries = malloc((size_t)entry_bytes);
    sections->order = malloc((size_t)order_bytes);
    if (!sections->entries || !sections->order) {
        sections->failure.system_error = ENOMEM;
        fail(sections, TB_SYMBOL_SECTIONS_RESOURCE, TB_SYMBOL_RESOURCE_FAILURE);
        return;
    }
    sections->entries[0] = (TbSymbolSectionEntry_t){
        .header = sections->zero
    };
    sections->index = 1;
    sections->phase = SECTIONS_NEXT;
}


static void record(TbSymbolSections_t *sections)
{
    TbSymbolFileView_t view = bytes(sections);
    if (sections->phase == SECTIONS_FAILED)
        return;
    TbSymbolElfSection_t entry;
    if (!decoded(sections, tb_symbol_elf_section(&sections->header, view.data, view.bytes, &entry),
                  TB_SYMBOL_SECTIONS_RECORD)) {
        return;
    }
    if (entry.type != SHT_NULL && ((entry.flags & SHF_GROUP) ||
        ((entry.flags & SHF_COMPRESSED) && ((entry.flags & SHF_ALLOC) || entry.type == SHT_NOBITS)))) {
        fail(sections, TB_SYMBOL_SECTIONS_RECORD, TB_SYMBOL_MALFORMED);
        return;
    }
    if (entry.type != SHT_NULL && entry.type != SHT_NOBITS && entry.bytes) {
        if (header_overlap(sections, &entry)) {
            fail(sections, TB_SYMBOL_SECTIONS_OVERLAP, TB_SYMBOL_MALFORMED);
            return;
        }
        sections->order[sections->order_count++] = sections->index;
    }
    sections->entries[sections->index] = (TbSymbolSectionEntry_t){
        .header = entry
    };
    ++sections->index;
    sections->phase = SECTIONS_NEXT;
}


static bool less(const TbSymbolSections_t *sections, uint64_t left, uint64_t right)
{
    uint64_t a = sections->entries[left].header.offset;
    uint64_t b = sections->entries[right].header.offset;
    return a < b || (a == b && left < right);
}


static void exchange(TbSymbolSections_t *sections, uint64_t left, uint64_t right)
{
    uint64_t saved = sections->order[left];
    sections->order[left] = sections->order[right];
    sections->order[right] = saved;
}


static void sort_step(TbSymbolSections_t *sections)
{
    if (sections->sifting) {
        /* Test for a leaf before doubling; a malicious count cannot overflow
         * child arithmetic or turn one service action into an unbounded sift. */
        if (sections->heap < 2 || sections->root > (sections->heap - 2) / 2) {
            sections->sifting = false;
            return;
        }
        uint64_t child = sections->root * 2 + 1;
        if (child + 1 < sections->heap &&
            less(sections, sections->order[child], sections->order[child + 1])) {
            ++child;
        }
        if (less(sections, sections->order[sections->root], sections->order[child])) {
            exchange(sections, sections->root, child);
            sections->root = child;
        } else {
            sections->sifting = false;
        }
    } else if (sections->sort_build) {
        sections->root = --sections->sort_build;
        sections->heap = sections->order_count;
        sections->sifting = true;
    } else if (sections->sort_remaining > 1) {
        --sections->sort_remaining;
        exchange(sections, 0, sections->sort_remaining);
        sections->root = 0;
        sections->heap = sections->sort_remaining;
        sections->sifting = true;
    } else {
        sections->index = 0;
        sections->phase = SECTIONS_OVERLAP;
    }
}


static void name_table(TbSymbolSections_t *sections)
{
    if (!sections->layout.section_names) {
        sections->names = TB_SYMBOL_SECTION_NAMES_ABSENT;
        sections->index = 1;
        sections->phase = SECTIONS_NAME_NEXT;
        return;
    }
    sections->index = sections->layout.section_names;
    const TbSymbolElfSection_t *table = &sections->entries[sections->layout.section_names].header;
    if (table->type != SHT_STRTAB) {
        fail(sections, TB_SYMBOL_SECTIONS_NAMES, TB_SYMBOL_MALFORMED);
    } else if (table->flags & SHF_COMPRESSED) {
        sections->names = TB_SYMBOL_SECTION_NAMES_COMPRESSED;
        sections->phase = SECTIONS_DONE;
        sections->outcome = TB_SYMBOL_SUCCESS;
    } else if (!table->bytes) {
        sections->names = TB_SYMBOL_SECTION_NAMES_AVAILABLE;
        sections->index = 1;
        sections->phase = SECTIONS_NAME_NEXT;
    } else {
        request(sections, (ByteSpan_t){
            .offset = table->offset,
            .bytes  = 1
        }, SECTIONS_NAME_FIRST);
    }
}


static void name_next(TbSymbolSections_t *sections)
{
    if (sections->index == sections->layout.sections.count) {
        sections->phase = SECTIONS_DONE;
        sections->outcome = TB_SYMBOL_SUCCESS;
        return;
    }
    TbSymbolSectionEntry_t *entry = &sections->entries[sections->index];
    if (entry->header.type == SHT_NULL || !entry->header.name_offset) {
        ++sections->index;
        return;
    }
    if (!sections->layout.section_names) {
        fail(sections, TB_SYMBOL_SECTIONS_NAMES, TB_SYMBOL_MALFORMED);
        return;
    }
    const TbSymbolElfSection_t *table = &sections->entries[sections->layout.section_names].header;
    if (entry->header.name_offset >= table->bytes) {
        fail(sections, TB_SYMBOL_SECTIONS_NAMES, TB_SYMBOL_MALFORMED);
        return;
    }
    entry->name_offset = table->offset + entry->header.name_offset;
    sections->name_cursor = entry->header.name_offset;
    uint64_t remaining = table->bytes - sections->name_cursor;
    sections->scan_at = 0;
    request(sections, (ByteSpan_t){
        .offset = table->offset + sections->name_cursor,
        .bytes  = remaining < TB_SYMBOL_FILE_WINDOW_BYTES ? remaining : TB_SYMBOL_FILE_WINDOW_BYTES
    }, SECTIONS_NAME_SCAN);
}


static void name_scan(TbSymbolSections_t *sections, size_t max_bytes)
{
    TbSymbolFileView_t view = bytes(sections);
    if (sections->phase == SECTIONS_FAILED)
        return;
    size_t length = view.bytes - sections->scan_at;
    if (length > max_bytes)
        length = max_bytes;
    const unsigned char *start = view.data + sections->scan_at;
    const unsigned char *end = memchr(start, 0, length);
    TbSymbolSectionEntry_t *entry = &sections->entries[sections->index];
    if (end) {
        entry->name_bytes += (uint64_t)(end - start);
        entry->has_name = true;
        ++sections->index;
        sections->phase = SECTIONS_NAME_NEXT;
        return;
    }
    entry->name_bytes += length;
    sections->scan_at += length;
    if (sections->scan_at != view.bytes)
        return;
    const TbSymbolElfSection_t *table = &sections->entries[sections->layout.section_names].header;
    sections->name_cursor += view.bytes;
    if (sections->name_cursor == table->bytes) {
        fail(sections, TB_SYMBOL_SECTIONS_NAMES, TB_SYMBOL_MALFORMED);
        return;
    }
    uint64_t remaining = table->bytes - sections->name_cursor;
    sections->scan_at = 0;
    request(sections, (ByteSpan_t){
        .offset = table->offset + sections->name_cursor,
        .bytes  = remaining < TB_SYMBOL_FILE_WINDOW_BYTES ? remaining : TB_SYMBOL_FILE_WINDOW_BYTES
    }, SECTIONS_NAME_SCAN);
}


static void advance(TbSymbolSections_t *sections, size_t max_bytes)
{
    switch (sections->phase) {
    case SECTIONS_BEGIN:
        if (sections->expected.bytes < EI_NIDENT) {
            fail(sections, TB_SYMBOL_SECTIONS_HEADER, TB_SYMBOL_MALFORMED);
            break;
        }
        request(sections, (ByteSpan_t){
            .offset = 0,
            .bytes  = sections->expected.bytes < 64 ? sections->expected.bytes : 64
        }, SECTIONS_HEADER);
        break;
    case SECTIONS_HEADER: {
        TbSymbolFileView_t view = bytes(sections);
        if (sections->phase == SECTIONS_FAILED ||
            !decoded(sections, tb_symbol_elf_header(sections->expected.bytes, view.data, view.bytes,
                                                   &sections->header), TB_SYMBOL_SECTIONS_HEADER)) {
            break;
        }
        if (sections->header.section_offset) {
            uint64_t stride = sections->header.address_bytes == 4 ? 40 : 64;
            if (sections->header.section_stride != stride) {
                fail(sections, TB_SYMBOL_SECTIONS_LAYOUT, sections->header.section_stride < stride
                                                            ? TB_SYMBOL_MALFORMED : TB_SYMBOL_UNSUPPORTED);
                break;
            }
            request(sections, (ByteSpan_t){
                .offset = sections->header.section_offset,
                .bytes  = stride
            }, SECTIONS_ZERO);
        } else if (decoded(sections, tb_symbol_elf_layout(&sections->header, NULL, &sections->layout),
                            TB_SYMBOL_SECTIONS_LAYOUT)) {
            sections->phase = SECTIONS_ALLOCATE;
        }
        break;
    }
    case SECTIONS_ZERO: {
        TbSymbolFileView_t view = bytes(sections);
        if (sections->phase != SECTIONS_FAILED &&
            decoded(sections, tb_symbol_elf_section(&sections->header, view.data, view.bytes,
                                                      &sections->zero), TB_SYMBOL_SECTIONS_RECORD) &&
            decoded(sections, tb_symbol_elf_layout(&sections->header, &sections->zero, &sections->layout),
                     TB_SYMBOL_SECTIONS_LAYOUT)) {
            sections->phase = SECTIONS_ALLOCATE;
        }
        break;
    }
    case SECTIONS_ALLOCATE:
        allocate_entries(sections);
        break;
    case SECTIONS_NEXT:
        if (sections->index == sections->layout.sections.count) {
            sections->sort_build = sections->order_count / 2;
            sections->sort_remaining = sections->order_count;
            sections->phase = SECTIONS_SORT;
        } else {
            uint64_t offset;
            if (!tb_symbol_elf_entry(&sections->layout.sections, sections->index, &offset)) {
                fail(sections, TB_SYMBOL_SECTIONS_LAYOUT, TB_SYMBOL_MALFORMED);
                break;
            }
            request(sections, (ByteSpan_t){
                .offset = offset,
                .bytes  = sections->layout.sections.stride
            }, SECTIONS_RECORD);
        }
        break;
    case SECTIONS_RECORD:
        record(sections);
        break;
    case SECTIONS_SORT:
        sort_step(sections);
        break;
    case SECTIONS_OVERLAP:
        if (sections->index == sections->order_count) {
            sections->phase = SECTIONS_NAME_TABLE;
        } else {
            uint64_t index = sections->order[sections->index];
            const TbSymbolElfSection_t *entry = &sections->entries[index].header;
            if (entry->offset < sections->previous_end) {
                sections->index = index;
                fail(sections, TB_SYMBOL_SECTIONS_OVERLAP, TB_SYMBOL_MALFORMED);
                break;
            }
            sections->previous_end = entry->offset + entry->bytes;
            ++sections->index;
        }
        break;
    case SECTIONS_NAME_TABLE:
        name_table(sections);
        break;
    case SECTIONS_NAME_FIRST: {
        TbSymbolFileView_t view = bytes(sections);
        if (sections->phase == SECTIONS_FAILED)
            break;
        if (view.data[0]) {
            fail(sections, TB_SYMBOL_SECTIONS_NAMES, TB_SYMBOL_MALFORMED);
            break;
        }
        const TbSymbolElfSection_t *table = &sections->entries[sections->layout.section_names].header;
        request(sections, (ByteSpan_t){
            .offset = table->offset + table->bytes - 1,
            .bytes  = 1
        }, SECTIONS_NAME_LAST);
        break;
    }
    case SECTIONS_NAME_LAST: {
        TbSymbolFileView_t view = bytes(sections);
        if (sections->phase == SECTIONS_FAILED)
            break;
        if (view.data[0]) {
            fail(sections, TB_SYMBOL_SECTIONS_NAMES, TB_SYMBOL_MALFORMED);
        } else {
            sections->names = TB_SYMBOL_SECTION_NAMES_AVAILABLE;
            sections->index = 1;
            sections->phase = SECTIONS_NAME_NEXT;
        }
        break;
    }
    case SECTIONS_NAME_NEXT:
        name_next(sections);
        break;
    case SECTIONS_NAME_SCAN:
        name_scan(sections, max_bytes);
        break;
    case SECTIONS_DONE:
    case SECTIONS_FAILED:
        break;
    }
}


TbSymbolOutcome_e tb_symbol_sections_step(TbSymbolSections_t *sections, size_t max_bytes)
{
    if (!sections)
        return TB_SYMBOL_OPERATIONAL_FAILURE;
    if (sections->phase == SECTIONS_DONE || sections->phase == SECTIONS_FAILED)
        return sections->outcome;
    if (max_bytes < 64 || max_bytes > TB_SYMBOL_FILE_WINDOW_BYTES) {
        fail(sections, TB_SYMBOL_SECTIONS_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        return sections->outcome;
    }
    bool progressed = true;
    if (sections->reading) {
        uint64_t before = tb_symbol_file_progress(sections->file);
        TbSymbolOutcome_e outcome = tb_symbol_file_step(sections->file, max_bytes);
        progressed = before != tb_symbol_file_progress(sections->file);
        if (outcome == TB_SYMBOL_SUCCESS) {
            sections->reading = false;
        } else if (outcome != TB_SYMBOL_PENDING) {
            sections->failure.read = tb_symbol_file_failure(sections->file);
            fail(sections, TB_SYMBOL_SECTIONS_READ, outcome);
        }
    } else {
        advance(sections, max_bytes);
    }
    if (progressed) {
        if (sections->progress == UINT64_MAX)
            fail(sections, TB_SYMBOL_SECTIONS_REQUEST, TB_SYMBOL_OPERATIONAL_FAILURE);
        else
            ++sections->progress;
    }
    return sections->outcome;
}


bool tb_symbol_sections_view(const TbSymbolSections_t *sections, TbSymbolSectionsView_t *view)
{
    if (!sections || !view || sections->phase != SECTIONS_DONE || sections->outcome != TB_SYMBOL_SUCCESS)
        return false;
    *view = (TbSymbolSectionsView_t){
        .header  = &sections->header,
        .layout  = &sections->layout,
        .entries = sections->entries,
        .count   = sections->layout.sections.count,
        .names   = sections->names
    };
    return true;
}


TbSymbolSectionsFailure_t tb_symbol_sections_failure(const TbSymbolSections_t *sections)
{
    return sections ? sections->failure : (TbSymbolSectionsFailure_t){
        .reason        = TB_SYMBOL_SECTIONS_REQUEST,
        .section_index = UINT64_MAX
    };
}


uint64_t tb_symbol_sections_progress(const TbSymbolSections_t *sections)
{
    return sections ? sections->progress : 0;
}


void tb_symbol_sections_destroy(TbSymbolSections_t *sections)
{
    if (!sections)
        return;
    tb_symbol_file_destroy(sections->file);
    free(sections->entries);
    free(sections->order);
    if (sections->array_bytes)
        (void)tb_symbol_budget_release(sections->construction, sections->array_bytes);
    (void)tb_symbol_budget_release(sections->construction, sizeof(*sections));
    free(sections);
}
