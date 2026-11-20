#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- Native file fixture reads. */
#include "symbol/abbrev.h"
#include "symbol/dies.h"
#include "symbol/entities.h"
#include "symbol/relations.h"
#include "symbol/references.h"
#include "symbol/text.h"
#include "symbol/elf.h"
#include "symbol/file.h"
#include "symbol/sections.h"
#include "symbol/symbols.h"
#include "symbol/units.h"
#include "symbol/acquire.h"
#include "symbol/admission.h"
#include "../support/check.h"

#include <gelf.h>
#include <libelf.h>
#include <limits.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static TbSymbolBudget_t budget;
static TbSymbolAdmissionLedger_t *ledger;
static TbSymbolStorage_t *storage;
static uint64_t next_slot;

static void read_record(TbSymbolFile_t *file, uint64_t offset, size_t length, unsigned char *bytes)
{
    CHECK(length <= 64);
    CHECK(tb_symbol_file_request(file, (TbSymbolFileRange_t){
        .offset = offset,
        .bytes  = length
    }));
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 128 && outcome == TB_SYMBOL_PENDING; ++i)
        outcome = tb_symbol_file_step(file, 31);
    CHECK(outcome == TB_SYMBOL_SUCCESS);
    TbSymbolFileView_t view;
    CHECK(tb_symbol_file_view(file, &view) && view.offset == offset && view.bytes == length);
    memcpy(bytes, view.data, length);
}


typedef struct {
    uint64_t strings;
    uint64_t line_strings;
    uint64_t offsets;
} StringSections_t;

static bool check_attribute_string(TbSymbolSectionsView_t directory, TbSymbolUnitsView_t units,
                                    TbSymbolDiesView_t tree, TbSymbolReferenceSelection_t selected,
                                    StringSections_t sections, int descriptor)
{
    const TbSymbolDieEntry_t *die = &tree.entries[selected.die_index];
    const TbSymbolDieAttribute_t *attribute = &tree.attributes[die->first_attribute +
                                                              selected.attribute_index];
    uint64_t form = attribute->value.form;
    bool indexed = form == TB_SYMBOL_FORM_STRX ||
                   (form >= TB_SYMBOL_FORM_STRX1 && form <= TB_SYMBOL_FORM_STRX4);
    if (form != TB_SYMBOL_FORM_STRING && form != TB_SYMBOL_FORM_STRP &&
        form != TB_SYMBOL_FORM_LINE_STRP && !indexed) {
        return false;
    }
    TbSymbolText_t *text = tb_symbol_text_create(directory, units, tree, (TbSymbolTextSelection_t){
        .unit_index          = selected.unit_index,
        .die_index           = selected.die_index,
        .name                = attribute->name,
        .string_section      = sections.strings,
        .line_string_section = sections.line_strings,
        .offset_section      = sections.offsets,
        .descriptor          = descriptor
    }, &budget);
    CHECK(text);
    TbSymbolTextOutcome_e outcome = TB_SYMBOL_TEXT_PENDING;
    for (unsigned turn = 0; turn < 1000000 && outcome == TB_SYMBOL_TEXT_PENDING; ++turn)
        outcome = tb_symbol_text_step(text, 257);
    CHECK(outcome == TB_SYMBOL_TEXT_FOUND);
    TbSymbolTextView_t view;
    CHECK(tb_symbol_text_view(text, &view));
    CHECK(view.backing.key.inode == directory.backing.key.inode);
    CHECK(view.source_section == tree.section_index && view.source_unit == tree.unit_offset);
    CHECK(view.source_record == die->offset && view.attribute.offset == attribute->offset);
    CHECK(view.attribute.name == attribute->name && view.attribute.value.form == form);
    CHECK(view.abbrev_section == tree.abbrev_section && view.abbrev_offset == tree.abbrev_offset);
    CHECK(view.indexed == indexed);
    if (indexed) {
        CHECK(view.base_attribute.name == 0x72);
        CHECK(view.base_attribute.value.bits == view.offsets.base);
        CHECK(view.offsets.index == attribute->value.bits);
        CHECK(view.offsets.section_index == sections.offsets);
        CHECK(view.text.section_index == sections.strings && view.text.offset == view.offsets.string_offset);
    } else if (form == TB_SYMBOL_FORM_STRING) {
        CHECK(view.text.section_index == tree.section_index);
        CHECK(view.text.offset == attribute->value.payload_offset);
        CHECK(view.text.bytes == attribute->value.payload_bytes);
    } else {
        CHECK(view.text.section_index == (form == TB_SYMBOL_FORM_LINE_STRP
                                             ? sections.line_strings : sections.strings));
        CHECK(view.text.offset == attribute->value.bits);
    }
    tb_symbol_text_destroy(text);
    return true;
}

typedef struct {
    uint64_t total;
    uint64_t unsupported;
    uint64_t abbreviations;
    uint64_t dies;
    uint64_t attributes;
    uint64_t references;
    uint64_t dependencies;
    uint64_t strings;
    uint64_t indexed_strings;
    uint64_t entity_kinds[4];
    uint64_t declarations;
    uint64_t specifications;
    uint64_t origins;
    uint64_t extensions;
    uint64_t relation_dependencies;
    uint64_t relation_unsupported;
    uint64_t vendor_call_sites;
    uint64_t legacy_members;
    uint64_t dwarf4_units;
    uint64_t dwarf5_units;
} UnitCounts_t;

static TbSymbolEntityKind_e check_entity(TbSymbolDiesView_t tree, uint64_t die, uint64_t *declarations)
{
    TbSymbolEntity_t entity;
    CHECK(tb_symbol_entity_init(&entity, tree, die) == TB_SYMBOL_ENTITY_PENDING);
    TbSymbolEntityStatus_e status = TB_SYMBOL_ENTITY_PENDING;
    for (uint64_t turn = 0; turn <= tree.entries[die].attribute_count &&
                            status == TB_SYMBOL_ENTITY_PENDING; ++turn) {
        status = tb_symbol_entity_step(&entity, 7);
    }
    CHECK(status == TB_SYMBOL_ENTITY_READY);
    TbSymbolEntityView_t view;
    CHECK(tb_symbol_entity_view(&entity, &view));
    CHECK(view.backing.key.inode == tree.backing.key.inode && view.backing.bytes == tree.backing.bytes);
    CHECK(view.section_index == tree.section_index && view.unit_offset == tree.unit_offset);
    CHECK(view.record_offset == tree.entries[die].offset && view.tag == tree.entries[die].tag);
    CHECK(view.parent_index == tree.entries[die].parent);
    CHECK(view.abbrev_section == tree.abbrev_section && view.abbrev_offset == tree.abbrev_offset);
    for (size_t i = 0; i < TB_SYMBOL_ENTITY_FIELDS; ++i) {
        const TbSymbolEntityField_t *field = &view.fields[i];
        CHECK(field->selection.matches <= 1);
        if (!field->selection.matches) {
            CHECK(field->selection.first_index == UINT64_MAX);
            continue;
        }
        uint64_t index = field->selection.first_index;
        CHECK(index >= tree.entries[die].first_attribute);
        CHECK(index - tree.entries[die].first_attribute < tree.entries[die].attribute_count);
        const TbSymbolDieAttribute_t *source = &tree.attributes[index];
        CHECK(field->selection.record_offset == view.record_offset);
        CHECK(field->selection.first_offset == source->offset);
        CHECK(field->attribute.offset == source->offset && field->attribute.name == source->name);
        CHECK(field->attribute.value.form == source->value.form);
        CHECK(field->attribute.value.bits == source->value.bits);
        CHECK(field->attribute.value.payload_offset == source->value.payload_offset);
        CHECK(field->attribute.value.payload_bytes == source->value.payload_bytes);
    }
    if (view.declaration.present && view.declaration.value)
        ++*declarations;
    CHECK(view.kind <= TB_SYMBOL_ENTITY_INLINE_INSTANCE);
    return view.kind;
}


static void check_relation(TbSymbolUnitsView_t units, TbSymbolDiesView_t tree,
                           TbSymbolReferenceSelection_t selected, UnitCounts_t *counts)
{
    const TbSymbolDieEntry_t *die = &tree.entries[selected.die_index];
    const TbSymbolDieAttribute_t *attribute = &tree.attributes[die->first_attribute +
                                                              selected.attribute_index];
    TbSymbolRelationKind_e kind;
    switch (attribute->name) {
    case 0x47:
        kind = TB_SYMBOL_RELATION_SPECIFICATION;
        break;
    case 0x31:
        kind = TB_SYMBOL_RELATION_ORIGIN;
        break;
    case 0x54:
        kind = TB_SYMBOL_RELATION_EXTENSION;
        break;
    default:
        return;
    }
    TbSymbolRelation_t relation;
    CHECK(tb_symbol_relation_init(&relation, units, tree, (TbSymbolRelationSelection_t){
        .unit_index = selected.unit_index,
        .die_index  = selected.die_index,
        .kind       = kind
    }) == TB_SYMBOL_RELATION_PENDING);
    TbSymbolRelationStatus_e status = TB_SYMBOL_RELATION_PENDING;
    for (uint64_t turn = 0; turn <= die->attribute_count && status == TB_SYMBOL_RELATION_PENDING; ++turn)
        status = tb_symbol_relation_step(&relation, 7);
    CHECK(status == TB_SYMBOL_RELATION_READY);
    TbSymbolRelationSource_t source;
    CHECK(tb_symbol_relation_source(&relation, &source));
    CHECK(source.reference.source_record == die->offset && source.field.offset == attribute->offset);
    CHECK(source.source_version == units.entries[selected.unit_index].header.version);
    TbSymbolRelationNeed_t need;
    status = tb_symbol_relation_need(source, units.kind == TB_SYMBOL_DWARF_INFO ? &units : NULL, &need);
    if (status == TB_SYMBOL_RELATION_UNSUPPORTED) {
        ++counts->relation_unsupported;
        if (kind == TB_SYMBOL_RELATION_ORIGIN && source.source_version == 4 &&
            (source.source_tag == 0x4109 || source.source_tag == 0x410a)) {
            ++counts->vendor_call_sites;
        }
        return;
    }
    if (status == TB_SYMBOL_RELATION_DEPENDENCY) {
        ++counts->relation_dependencies;
        return;
    }
    CHECK(status == TB_SYMBOL_RELATION_READY);
    if (need.target.section_index != tree.section_index || need.target.unit_offset != tree.unit_offset) {
        ++counts->relation_dependencies;
        return;
    }
    TbSymbolRelationEdge_t edge;
    status = tb_symbol_relation_target(need, &tree, &edge);
    if (status != TB_SYMBOL_RELATION_FOUND)
        fprintf(stderr, "native relation failure %u, kind %u, source %llu, target %llu\n", status,
                kind, (unsigned long long)source.reference.source_record,
                (unsigned long long)need.target.record_offset);
    CHECK(status == TB_SYMBOL_RELATION_FOUND);
    CHECK(edge.target.record_offset == need.target.record_offset);
    CHECK(edge.target_version == source.source_version);
    CHECK(edge.target_abbrev_section == tree.abbrev_section && edge.target_abbrev_offset == tree.abbrev_offset);
    switch (kind) {
    case TB_SYMBOL_RELATION_SPECIFICATION:
        ++counts->specifications;
        if (edge.target_tag == 0x0d) {
            CHECK(edge.target_version == 4);
            ++counts->legacy_members;
        }
        break;
    case TB_SYMBOL_RELATION_ORIGIN:
        ++counts->origins;
        break;
    case TB_SYMBOL_RELATION_EXTENSION:
        ++counts->extensions;
        break;
    }
}


static UnitCounts_t check_units(TbSymbolSectionsView_t directory, TbSymbolUnitSelection_t selection,
                                uint64_t abbreviation_section, StringSections_t string_sections)
{
    TbSymbolUnits_t *units = tb_symbol_units_create(directory, selection, &budget);
    CHECK(units);
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned turn = 0; turn < 1000000 && outcome == TB_SYMBOL_PENDING; ++turn)
        outcome = tb_symbol_units_step(units, 257);
    CHECK(outcome == TB_SYMBOL_SUCCESS);
    TbSymbolUnitsView_t view;
    CHECK(tb_symbol_units_view(units, &view));
    CHECK(view.section_index == selection.section_index && view.kind == selection.kind);
    uint64_t cursor = 0;
    uint64_t unsupported = 0;
    uint64_t abbreviations = 0;
    uint64_t die_count = 0;
    uint64_t attribute_count = 0;
    uint64_t reference_count = 0;
    uint64_t dependency_count = 0;
    uint64_t string_count = 0;
    uint64_t indexed_count = 0;
    uint64_t entity_kinds[4] = {0};
    uint64_t declarations = 0;
    UnitCounts_t relations = {0};
    for (uint64_t i = 0; i < view.count; ++i) {
        const TbSymbolUnitEntry_t *entry = &view.entries[i];
        CHECK(entry->offset == cursor && entry->extent.total_bytes);
        if (entry->header_available) {
            CHECK(entry->header.extent.end_offset == entry->extent.end_offset);
            CHECK(entry->header.version == 4 || entry->header.version == 5);
            if (entry->header.version == 4)
                ++relations.dwarf4_units;
            else
                ++relations.dwarf5_units;
            CHECK(entry->header.header_bytes < entry->extent.total_bytes);
            CHECK(abbreviation_section < directory.count);
            TbSymbolAbbrev_t *abbrev = tb_symbol_abbrev_create(directory, (TbSymbolAbbrevSelection_t){
                .section_index = abbreviation_section,
                .table_offset  = entry->header.abbrev_offset,
                .version       = entry->header.version,
                .descriptor    = selection.descriptor
            }, &budget);
            CHECK(abbrev);
            TbSymbolOutcome_e decoded = TB_SYMBOL_PENDING;
            for (unsigned turn = 0; turn < 1000000 && decoded == TB_SYMBOL_PENDING; ++turn)
                decoded = tb_symbol_abbrev_step(abbrev, 257);
            CHECK(decoded == TB_SYMBOL_SUCCESS);
            TbSymbolAbbrevView_t table;
            CHECK(tb_symbol_abbrev_view(abbrev, &table));
            CHECK(table.table_offset == entry->header.abbrev_offset && table.count);
            for (uint64_t code = 0; code < table.count; ++code) {
                const TbSymbolAbbrevEntry_t *found = NULL;
                CHECK(tb_symbol_abbrev_find(abbrev, table.entries[code].code, &found) ==
                      TB_SYMBOL_ABBREV_FOUND);
                CHECK(found == &table.entries[code]);
            }
            abbreviations += table.count;
            TbSymbolDies_t *dies = tb_symbol_dies_create(directory, view, (TbSymbolDieSelection_t){
                .unit_index = i,
                .descriptor = selection.descriptor
            }, abbrev, &budget);
            CHECK(dies);
            decoded = TB_SYMBOL_PENDING;
            for (unsigned turn = 0; turn < 3000000 && decoded == TB_SYMBOL_PENDING; ++turn)
                decoded = tb_symbol_dies_step(dies, 257);
            if (decoded != TB_SYMBOL_SUCCESS) {
                TbSymbolDiesFailure_t failure = tb_symbol_dies_failure(dies);
                fprintf(stderr, "native DIE failure %u/%u at %llu in unit %llu\n", decoded,
                        failure.reason, (unsigned long long)failure.offset, (unsigned long long)i);
            }
            CHECK(decoded == TB_SYMBOL_SUCCESS);
            TbSymbolDiesView_t tree;
            CHECK(tb_symbol_dies_view(dies, &tree) && tree.count);
            CHECK(tree.section_index == selection.section_index && tree.unit_offset == entry->offset);
            CHECK(tree.abbrev_section == abbreviation_section && tree.abbrev_offset == table.table_offset);
            uint64_t previous = entry->offset + entry->header.header_bytes;
            uint64_t attributes = 0;
            for (uint64_t die = 0; die < tree.count; ++die) {
                const TbSymbolDieEntry_t *record = &tree.entries[die];
                CHECK(record->offset >= previous && record->end_offset > record->offset);
                CHECK(record->end_offset <= entry->extent.end_offset);
                CHECK(record->parent == UINT64_MAX ? !die : record->parent < die);
                CHECK(record->first_attribute == attributes);
                attributes += record->attribute_count;
                CHECK(attributes <= tree.attribute_count);
                ++entity_kinds[check_entity(tree, die, &declarations)];
                for (uint64_t attr = 0; attr < record->attribute_count; ++attr) {
                    TbSymbolReferenceSelection_t selected = {
                        .unit_index      = i,
                        .die_index       = die,
                        .attribute_index = attr
                    };
                    check_relation(view, tree, selected, &relations);
                    if (check_attribute_string(directory, view, tree, selected, string_sections,
                                               selection.descriptor)) {
                        ++string_count;
                        uint64_t form = tree.attributes[record->first_attribute + attr].value.form;
                        if (form == TB_SYMBOL_FORM_STRX ||
                            (form >= TB_SYMBOL_FORM_STRX1 && form <= TB_SYMBOL_FORM_STRX4)) {
                            ++indexed_count;
                        }
                    }
                    TbSymbolReference_t reference;
                    TbSymbolReferenceStatus_e prepared = tb_symbol_reference_prepare(view, tree, selected,
                                                                                      &reference);
                    if (prepared == TB_SYMBOL_REFERENCE_NOT_REFERENCE)
                        continue;
                    CHECK(prepared == TB_SYMBOL_REFERENCE_READY);
                    if (reference.kind == TB_SYMBOL_REFERENCE_UNIT) {
                        TbSymbolReferenceTarget_t target;
                        CHECK(tb_symbol_reference_unit(reference, tree, &target) ==
                              TB_SYMBOL_REFERENCE_FOUND);
                        CHECK(target.backing.key.inode == directory.backing.key.inode);
                        CHECK(target.section_index == tree.section_index &&
                              target.unit_offset == tree.unit_offset);
                        CHECK(target.record_offset == tree.entries[target.entry_index].offset);
                        ++reference_count;
                    } else {
                        ++dependency_count;
                    }
                }
                previous = record->end_offset;
            }
            CHECK(attributes == tree.attribute_count);
            die_count += tree.count;
            attribute_count += tree.attribute_count;
            tb_symbol_dies_destroy(dies);
            tb_symbol_abbrev_destroy(abbrev);
        } else {
            ++unsupported;
            CHECK(!entry->header.version && !entry->header.header_bytes);
        }
        cursor = entry->extent.end_offset;
    }
    CHECK(cursor == directory.entries[selection.section_index].header.bytes);
    CHECK(unsupported == view.unsupported);
    UnitCounts_t counts = {
        .total                 = view.count,
        .unsupported           = view.unsupported,
        .abbreviations         = abbreviations,
        .dies                  = die_count,
        .attributes            = attribute_count,
        .references            = reference_count,
        .dependencies          = dependency_count,
        .strings               = string_count,
        .indexed_strings       = indexed_count,
        .entity_kinds          = {
            entity_kinds[0], entity_kinds[1], entity_kinds[2], entity_kinds[3]
        },
        .declarations          = declarations,
        .specifications        = relations.specifications,
        .origins               = relations.origins,
        .extensions            = relations.extensions,
        .relation_dependencies = relations.relation_dependencies,
        .relation_unsupported  = relations.relation_unsupported,
        .vendor_call_sites     = relations.vendor_call_sites,
        .legacy_members        = relations.legacy_members,
        .dwarf4_units          = relations.dwarf4_units,
        .dwarf5_units          = relations.dwarf5_units
    };
    tb_symbol_units_destroy(units);
    return counts;
}


static void compare_section(TbSymbolElfSection_t actual, GElf_Shdr expected)
{
    CHECK(actual.name_offset == expected.sh_name && actual.type == expected.sh_type);
    CHECK(actual.flags == expected.sh_flags && actual.address == expected.sh_addr);
    CHECK(actual.offset == expected.sh_offset && actual.bytes == expected.sh_size);
    CHECK(actual.link == expected.sh_link && actual.info == expected.sh_info);
    CHECK(actual.alignment == expected.sh_addralign && actual.entry_bytes == expected.sh_entsize);
}


static UnitCounts_t check_file(const char *path)
{
    TbSymbolReservation_t *ticket = tb_symbol_admission_prepare(ledger);
    CHECK(ticket);
    TbSymbolAcquire_t *acquisition = tb_symbol_acquire_create(path, storage, ++next_slot,
                                                             TB_SYMBOL_FILE_BYTES_DEFAULT, &budget);
    CHECK(acquisition);
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 100000 && outcome == TB_SYMBOL_PENDING; ++i) {
        uint64_t offered;
        if (tb_symbol_acquire_offer(acquisition, &offered)) {
            CHECK(tb_symbol_admission_assign(ticket, offered));
            CHECK(tb_symbol_acquire_grant(acquisition, offered));
        }
        outcome = tb_symbol_acquire_step(acquisition, 65536);
    }
    CHECK(outcome == TB_SYMBOL_SUCCESS);
    TbSymbolAcquiredBytes_t acquired;
    CHECK(tb_symbol_acquire_take(acquisition, &acquired));
    tb_symbol_acquire_destroy(acquisition);
    TbSymbolBackingCharge_t *backing = tb_symbol_admission_commit(ticket, NULL);
    CHECK(backing);
    int fd = acquired.descriptor;
    TbSymbolFile_t *input = tb_symbol_file_create((TbSymbolStorageInfo_t){
        .key   = acquired.key,
        .bytes = acquired.bytes
    }, fd, &budget);
    CHECK(input);
    TbSymbolSections_t *directory = tb_symbol_sections_create((TbSymbolStorageInfo_t){
        .key   = acquired.key,
        .bytes = acquired.bytes
    }, fd, &budget);
    CHECK(directory);
    outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 1000000 && outcome == TB_SYMBOL_PENDING; ++i)
        outcome = tb_symbol_sections_step(directory, 257);
    CHECK(outcome == TB_SYMBOL_SUCCESS);
    TbSymbolSectionsView_t directory_view;
    CHECK(tb_symbol_sections_view(directory, &directory_view));
    CHECK(directory_view.names == TB_SYMBOL_SECTION_NAMES_AVAILABLE);
    unsigned char bytes[64];
    read_record(input, 0, sizeof(bytes), bytes);
    TbSymbolElfHeader_t header;
    CHECK(tb_symbol_elf_header(acquired.bytes, bytes, sizeof(bytes), &header) == TB_SYMBOL_ELF_OK);
    Elf *reference = elf_begin(fd, ELF_C_READ, NULL);
    CHECK(reference);
    GElf_Ehdr expected_header;
    CHECK(gelf_getehdr(reference, &expected_header));
    CHECK(header.address_bytes == (gelf_getclass(reference) == ELFCLASS32 ? 4U : 8U));
    CHECK(header.os_abi == expected_header.e_ident[EI_OSABI]);
    CHECK(header.abi_version == expected_header.e_ident[EI_ABIVERSION]);
    CHECK(header.object_type == expected_header.e_type && header.machine == expected_header.e_machine);
    CHECK(header.flags == expected_header.e_flags && header.entry == expected_header.e_entry);
    CHECK(header.program_offset == expected_header.e_phoff &&
          header.section_offset == expected_header.e_shoff);
    CHECK(header.header_bytes == expected_header.e_ehsize &&
          header.program_stride == expected_header.e_phentsize);
    CHECK(header.section_stride == expected_header.e_shentsize);
    TbSymbolElfSection_t zero;
    CHECK(tb_symbol_range(header.section_offset, header.section_stride, header.file_bytes));
    read_record(input, header.section_offset, header.section_stride, bytes);
    CHECK(tb_symbol_elf_section(&header, bytes, header.section_stride, &zero) == TB_SYMBOL_ELF_OK);
    TbSymbolElfLayout_t layout;
    CHECK(tb_symbol_elf_layout(&header, &zero, &layout) == TB_SYMBOL_ELF_OK);
    size_t sections;
    size_t programs;
    size_t names;
    CHECK(!elf_getshdrnum(reference, &sections) && !elf_getphdrnum(reference, &programs));
    CHECK(!elf_getshdrstrndx(reference, &names));
    CHECK(layout.sections.count == sections && layout.programs.count == programs &&
          layout.section_names == names);
    CHECK(directory_view.count == sections && directory_view.layout->section_names == names);
    CHECK(directory_view.header->address_bytes == header.address_bytes);
    CHECK(directory_view.header->byte_order == header.byte_order);
    uint64_t abbreviation_section = UINT64_MAX;
    StringSections_t string_sections = {
        .strings      = UINT64_MAX,
        .line_strings = UINT64_MAX,
        .offsets      = UINT64_MAX
    };
    for (size_t i = 0; i < sections; ++i) {
        const TbSymbolSectionEntry_t *entry = &directory_view.entries[i];
        if (!entry->has_name)
            continue;
        const char *name = elf_strptr(reference, names, entry->header.name_offset);
        CHECK(name);
        if (!strcmp(name, ".debug_str")) {
            CHECK(string_sections.strings == UINT64_MAX);
            string_sections.strings = i;
        } else if (!strcmp(name, ".debug_line_str")) {
            CHECK(string_sections.line_strings == UINT64_MAX);
            string_sections.line_strings = i;
        } else if (!strcmp(name, ".debug_str_offsets")) {
            CHECK(string_sections.offsets == UINT64_MAX);
            string_sections.offsets = i;
        }
        if (!strcmp(name, ".debug_abbrev")) {
            CHECK(abbreviation_section == UINT64_MAX);
            abbreviation_section = i;
        }
    }
    size_t symbols = 0;
    UnitCounts_t unit_counts = {0};
    for (size_t i = 0; i < sections; ++i) {
        uint64_t offset;
        CHECK(tb_symbol_elf_entry(&layout.sections, i, &offset));
        read_record(input, offset, (size_t)layout.sections.stride, bytes);
        TbSymbolElfSection_t actual;
        CHECK(tb_symbol_elf_section(&header, bytes, (size_t)layout.sections.stride, &actual) ==
              TB_SYMBOL_ELF_OK);
        Elf_Scn *section = elf_getscn(reference, i);
        GElf_Shdr expected;
        CHECK(section && gelf_getshdr(section, &expected));
        compare_section(actual, expected);
        const TbSymbolSectionEntry_t *validated = &directory_view.entries[i];
        compare_section(validated->header, expected);
        if (actual.type != SHT_NULL && actual.name_offset) {
            const char *expected_name = elf_strptr(reference, names, actual.name_offset);
            CHECK(expected_name && validated->has_name);
            CHECK(validated->name_bytes == strlen(expected_name));
            if (!strcmp(expected_name, ".debug_info") || !strcmp(expected_name, ".debug_types")) {
                UnitCounts_t counts = check_units(directory_view, (TbSymbolUnitSelection_t){
                    .section_index = i,
                    .kind          = !strcmp(expected_name, ".debug_info")
                                         ? TB_SYMBOL_DWARF_INFO : TB_SYMBOL_DWARF_TYPES,
                    .descriptor = fd
                }, abbreviation_section, string_sections);
                unit_counts.abbreviations += counts.abbreviations;
                unit_counts.dies += counts.dies;
                unit_counts.attributes += counts.attributes;
                unit_counts.references += counts.references;
                unit_counts.dependencies += counts.dependencies;
                unit_counts.strings += counts.strings;
                unit_counts.indexed_strings += counts.indexed_strings;
                for (size_t kind = 0; kind < 4; ++kind)
                    unit_counts.entity_kinds[kind] += counts.entity_kinds[kind];
                unit_counts.declarations += counts.declarations;
                unit_counts.specifications += counts.specifications;
                unit_counts.origins += counts.origins;
                unit_counts.extensions += counts.extensions;
                unit_counts.relation_dependencies += counts.relation_dependencies;
                unit_counts.relation_unsupported += counts.relation_unsupported;
                unit_counts.vendor_call_sites += counts.vendor_call_sites;
                unit_counts.legacy_members += counts.legacy_members;
                unit_counts.dwarf4_units += counts.dwarf4_units;
                unit_counts.dwarf5_units += counts.dwarf5_units;
                unit_counts.total += counts.total;
                unit_counts.unsupported += counts.unsupported;
            }
            uint64_t compared = 0;
            while (compared < validated->name_bytes) {
                uint64_t remaining = validated->name_bytes - compared;
                size_t length = remaining < sizeof(bytes) ? (size_t)remaining : sizeof(bytes);
                read_record(input, validated->name_offset + compared, length, bytes);
                CHECK(!memcmp(bytes, expected_name + compared, length));
                compared += length;
            }
        } else {
            CHECK(!validated->has_name);
        }
        if (actual.type != SHT_SYMTAB && actual.type != SHT_DYNSYM)
            continue;
        CHECK(actual.entry_bytes == (header.address_bytes == 4 ? 16U : 24U));
        CHECK(!(actual.bytes % actual.entry_bytes));
        TbSymbolElfTable_t table = {
            .offset = actual.offset,
            .count  = actual.bytes / actual.entry_bytes,
            .stride = actual.entry_bytes
        };
        TbSymbolSymbols_t *validated_symbols = tb_symbol_symbols_create(directory_view,
                                                                       (TbSymbolTableSelection_t){
            .section_index = i,
            .descriptor    = fd
        }, &budget);
        CHECK(validated_symbols);
        outcome = TB_SYMBOL_PENDING;
        for (unsigned turn = 0; turn < 1000000 && outcome == TB_SYMBOL_PENDING; ++turn)
            outcome = tb_symbol_symbols_step(validated_symbols, 257);
        CHECK(outcome == TB_SYMBOL_SUCCESS);
        TbSymbolSymbolsView_t symbol_view;
        CHECK(tb_symbol_symbols_view(validated_symbols, &symbol_view));
        CHECK(symbol_view.count == table.count && symbol_view.table_section == i);
        CHECK(symbol_view.kind == (actual.type == SHT_SYMTAB
                                       ? TB_SYMBOL_RECORD_SYMTAB : TB_SYMBOL_RECORD_DYNSYM));
        Elf_Data *data = elf_getdata(section, NULL);
        CHECK(data);
        for (uint64_t entry = 0; entry < table.count; ++entry) {
            CHECK(entry <= INT_MAX && tb_symbol_elf_entry(&table, entry, &offset));
            read_record(input, offset, (size_t)table.stride, bytes);
            TbSymbolElfSymbol_t symbol;
            CHECK(tb_symbol_elf_symbol(&header, bytes, (size_t)table.stride, &symbol) == TB_SYMBOL_ELF_OK);
            GElf_Sym expected_symbol;
            CHECK(gelf_getsym(data, (int)entry, &expected_symbol));
            CHECK(symbol.name_offset == expected_symbol.st_name && symbol.info == expected_symbol.st_info);
            CHECK(symbol.other == expected_symbol.st_other &&
                  symbol.section_index == expected_symbol.st_shndx);
            CHECK(symbol.value == expected_symbol.st_value && symbol.bytes == expected_symbol.st_size);
            const TbSymbolElfEntry_t *validated_entry = &symbol_view.entries[entry];
            CHECK(validated_entry->record_offset == entry * table.stride);
            CHECK(validated_entry->raw.name_offset == symbol.name_offset &&
                  validated_entry->raw.info == symbol.info);
            CHECK(validated_entry->raw.other == symbol.other &&
                  validated_entry->raw.section_index == symbol.section_index);
            CHECK(validated_entry->raw.value == symbol.value && validated_entry->raw.bytes == symbol.bytes);
            if (symbol.name_offset) {
                const char *name = elf_strptr(reference, actual.link, symbol.name_offset);
                CHECK(name && validated_entry->has_name && validated_entry->name_bytes == strlen(name));
                uint64_t compared = 0;
                while (compared < validated_entry->name_bytes) {
                    uint64_t remaining = validated_entry->name_bytes - compared;
                    size_t length = remaining < sizeof(bytes) ? (size_t)remaining : sizeof(bytes);
                    read_record(input, validated_entry->name_offset + compared, length, bytes);
                    CHECK(!memcmp(bytes, name + compared, length));
                    compared += length;
                }
            } else {
                CHECK(!validated_entry->has_name);
            }
            ++symbols;
        }
        tb_symbol_symbols_destroy(validated_symbols);
    }
    for (size_t i = 0; i < programs; ++i) {
        uint64_t offset;
        CHECK(i <= INT_MAX && tb_symbol_elf_entry(&layout.programs, i, &offset));
        read_record(input, offset, (size_t)layout.programs.stride, bytes);
        TbSymbolElfProgram_t actual;
        CHECK(tb_symbol_elf_program(&header, bytes, (size_t)layout.programs.stride, &actual) ==
              TB_SYMBOL_ELF_OK);
        GElf_Phdr expected;
        CHECK(gelf_getphdr(reference, (int)i, &expected));
        CHECK(actual.type == expected.p_type && actual.flags == expected.p_flags);
        CHECK(actual.offset == expected.p_offset && actual.virtual_address == expected.p_vaddr);
        CHECK(actual.physical_address == expected.p_paddr && actual.file_bytes == expected.p_filesz);
        CHECK(actual.memory_bytes == expected.p_memsz && actual.alignment == expected.p_align);
    }
    CHECK(symbols && sections && programs && unit_counts.strings &&
          unit_counts.total > unit_counts.unsupported);
    printf("PASS native DWARF composition: %llu contributions, %llu unsupported, %llu abbreviations\n",
           (unsigned long long)unit_counts.total, (unsigned long long)unit_counts.unsupported,
           (unsigned long long)unit_counts.abbreviations);
    printf("PASS native DIE composition: %llu records, %llu attributes\n",
           (unsigned long long)unit_counts.dies, (unsigned long long)unit_counts.attributes);
    printf("PASS native reference coordinates: %llu unit references, %llu other dependencies\n",
           (unsigned long long)unit_counts.references, (unsigned long long)unit_counts.dependencies);
    printf("PASS native string dependencies: %llu validated strings, %llu indexed\n",
           (unsigned long long)unit_counts.strings, (unsigned long long)unit_counts.indexed_strings);
    CHECK(unit_counts.entity_kinds[0] + unit_counts.entity_kinds[1] +
          unit_counts.entity_kinds[2] + unit_counts.entity_kinds[3] == unit_counts.dies);
    printf("PASS native entry facts: %llu functions, %llu data, %llu inline, %llu declaration flags\n",
           (unsigned long long)unit_counts.entity_kinds[TB_SYMBOL_ENTITY_FUNCTION],
           (unsigned long long)unit_counts.entity_kinds[TB_SYMBOL_ENTITY_DATA],
           (unsigned long long)unit_counts.entity_kinds[TB_SYMBOL_ENTITY_INLINE_INSTANCE],
           (unsigned long long)unit_counts.declarations);
    printf("PASS native direct relations: %llu specification, %llu origin, %llu extension, "
           "%llu dependencies, %llu unsupported, %llu DWARF 4 members\n",
           (unsigned long long)unit_counts.specifications, (unsigned long long)unit_counts.origins,
           (unsigned long long)unit_counts.extensions, (unsigned long long)unit_counts.relation_dependencies,
           (unsigned long long)unit_counts.relation_unsupported, (unsigned long long)unit_counts.legacy_members);
    CHECK(!elf_end(reference));
    tb_symbol_sections_destroy(directory);
    tb_symbol_file_destroy(input);
    CHECK(!close(fd));
    CHECK(tb_symbol_admission_close(ticket) && tb_symbol_admission_evict(backing));
    CHECK(tb_symbol_storage_unlink(storage, next_slot, &acquired.key));
    CHECK(tb_symbol_admission_backing_cleaned(backing));
    CHECK(!tb_symbol_admission_usage(ledger).bytes);
    printf("PASS native ELF record agreement with libelf: %zu sections, %zu programs, %zu symbols\n",
           sections, programs, symbols);
    return unit_counts;
}


int main(int argc, char **argv)
{
    CHECK(argc >= 2 && elf_version(EV_CURRENT) != EV_NONE);
    char directory[] = "/tmp/tb-symbol-elf-native-XXXXXX";
    CHECK(mkdtemp(directory));
    char pool[256];
    CHECK(snprintf(pool, sizeof(pool), "%s/pool", directory) > 0);
    CHECK(tb_symbol_budget_init(&budget, 2 * 1024 * 1024));
    ledger = tb_symbol_admission_create(TB_SYMBOL_FILE_BYTES_DEFAULT, TB_SYMBOL_ADMISSION_BYTES_DEFAULT,
                                         &budget);
    storage = tb_symbol_storage_create(pool, NULL, &budget);
    CHECK(ledger && storage);
    TbSymbolOutcome_e outcome = TB_SYMBOL_PENDING;
    for (unsigned i = 0; i < 16 && outcome == TB_SYMBOL_PENDING; ++i)
        outcome = tb_symbol_storage_step(storage);
    CHECK(outcome == TB_SYMBOL_SUCCESS);
    for (int i = 1; i < argc; ++i) {
        unsigned version = !strcmp(argv[i], "--relations-dwarf4") ? 4U
                               : !strcmp(argv[i], "--relations-dwarf5") ? 5U : 0U;
        if (version) {
            CHECK(i + 1 < argc);
            UnitCounts_t counts = check_file(argv[++i]);
            CHECK(counts.specifications && counts.origins && !counts.relation_dependencies);
            CHECK(counts.relation_unsupported == counts.vendor_call_sites);
            CHECK((version == 4 ? counts.dwarf4_units : counts.dwarf5_units) == counts.total);
            CHECK(version != 4 || counts.legacy_members);
            printf("PASS C++ direct relationship fixture DWARF %u\n", version);
        } else {
            (void)check_file(argv[i]);
        }
    }
    CHECK(tb_symbol_storage_remove(storage));
    tb_symbol_storage_destroy(storage);
    CHECK(tb_symbol_admission_destroy(ledger) && !budget.used);
    CHECK(!rmdir(directory));
    return 0;
}
