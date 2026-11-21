#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- Native file fixture reads. */
#include "symbol/abbrev.h"
#include "symbol/dies.h"
#include "symbol/entities.h"
#include "symbol/relations.h"
#include "symbol/graph.h"
#include "symbol/inherit.h"
#include "symbol/inherited_text.h"
#include "symbol/context.h"
#include "symbol/scope_graph.h"
#include "symbol/constants.h"
#include "symbol/qualified.h"
#include "symbol/inline_scope.h"
#include "symbol/locality.h"
#include "symbol/parameters.h"
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
static uint64_t parameter_queries;
static uint64_t parameter_formals;
static uint64_t parameter_markers;
static uint64_t locality_nonlocal;
static uint64_t locality_local;
static uint64_t locality_unsupported;
static uint64_t qualified_ready;
static uint64_t qualified_unnamed;
static uint64_t qualified_unavailable;
static uint64_t inline_ancestry_queries;
static uint64_t inline_abstract_markers;
static uint64_t inline_concrete_markers;

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
    if (outcome != TB_SYMBOL_TEXT_FOUND) {
        TbSymbolTextFailure_t failure = tb_symbol_text_failure(text);
        fprintf(stderr, "text outcome %u reason %u, budget %llu/%llu, unit %llu record %llu\n",
                outcome, failure.reason, (unsigned long long)budget.used, (unsigned long long)budget.limit,
                (unsigned long long)tree.unit_offset, (unsigned long long)die->offset);
    }
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
    uint64_t inherited_queries;
    uint64_t inherited_owners;
    uint64_t inherited_indirect;
    uint64_t inherited_multiple;
    uint64_t graphs;
    uint64_t graph_nodes;
    uint64_t graph_dependencies;
    uint64_t graph_unsupported;
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

static void check_constant_field(TbSymbolDiesView_t tree, TbSymbolAbbrevView_t schema,
                                 uint64_t die, TbSymbolConstantField_e field,
                                 const TbSymbolEntityField_t *expected)
{
    TbSymbolConstant_t constant;
    CHECK(tb_symbol_budget_reserve(&budget, sizeof(constant)));
    CHECK(tb_symbol_constant_init(&constant, tree, die, field) == TB_SYMBOL_CONSTANT_PENDING);
    TbSymbolConstantStatus_e status = TB_SYMBOL_CONSTANT_PENDING;
    for (unsigned turn = 0; turn < 1000000 &&
                            (status == TB_SYMBOL_CONSTANT_PENDING || status == TB_SYMBOL_CONSTANT_SCHEMA);
         ++turn) {
        if (status == TB_SYMBOL_CONSTANT_SCHEMA)
            CHECK(tb_symbol_constant_schema(&constant, &schema) == TB_SYMBOL_CONSTANT_PENDING);
        status = tb_symbol_constant_step(&constant, 7);
    }
    if (status != TB_SYMBOL_CONSTANT_FOUND && status != TB_SYMBOL_CONSTANT_ABSENT) {
        fprintf(stderr, "native constant status %u, unit %llu, record %llu, field %u, form %llu, "
                        "numeric %u, signed %u, bits %llu\n",
                (unsigned)status, (unsigned long long)tree.unit_offset,
                (unsigned long long)tree.entries[die].offset, (unsigned)field,
                (unsigned long long)constant.view.attribute.value.form,
                (unsigned)constant.view.attribute.value.numeric,
                (unsigned)constant.view.attribute.value.is_signed,
                (unsigned long long)constant.view.attribute.value.bits);
    }
    CHECK(status == TB_SYMBOL_CONSTANT_FOUND || status == TB_SYMBOL_CONSTANT_ABSENT);
    TbSymbolAttributeResult_t selection;
    CHECK(tb_symbol_constant_selection(&constant, &selection));
    CHECK(selection.record_offset == tree.entries[die].offset && selection.name == (uint64_t)field);
    if (expected) {
        CHECK(selection.matches == expected->selection.matches);
        CHECK(selection.first_offset == expected->selection.first_offset);
    }
    if (status == TB_SYMBOL_CONSTANT_FOUND) {
        TbSymbolConstantView_t view;
        CHECK(tb_symbol_constant_view(&constant, &view));
        CHECK(view.selection.matches == 1 && view.selection.first_index < tree.attribute_count);
        const TbSymbolDieAttribute_t *source = &tree.attributes[view.selection.first_index];
        CHECK(view.attribute.offset == source->offset);
        if (view.from_abbreviation) {
            CHECK(view.value == (uint64_t)schema.attributes[source->schema_attribute].implicit_value);
            CHECK(!view.attribute.value.numeric && !view.attribute.value.bytes);
            CHECK(view.abbreviation.offset == schema.attributes[source->schema_attribute].offset);
        } else {
            CHECK(view.value == source->value.bits);
        }
        CHECK(view.attribute.value.form == source->value.form);
        CHECK(view.abbrev_section == tree.abbrev_section && view.abbrev_offset == tree.abbrev_offset);
        if (field == TB_SYMBOL_CONSTANT_INLINE) {
            TbSymbolInlineKind_e kind;
            CHECK(tb_symbol_inline_kind(view.value, &kind));
            CHECK((uint64_t)kind == view.value);
        }
    } else {
        CHECK(selection.matches == 0 && selection.first_index == UINT64_MAX);
    }
    CHECK(tb_symbol_budget_release(&budget, sizeof(constant)));
}


static void check_parameters(TbSymbolDiesView_t tree, uint64_t selected)
{
    TbSymbolParameters_t parameters;
    TbSymbolParameter_t  arena[64];
    size_t               charge = sizeof(parameters) + sizeof(arena);
    size_t               before = budget.used;
    CHECK(tb_symbol_budget_reserve(&budget, charge));
    CHECK(tb_symbol_parameters_init(&parameters, tree, selected, arena, 64) == TB_SYMBOL_PARAMETERS_PENDING);
    TbSymbolParametersStatus_e status = TB_SYMBOL_PARAMETERS_PENDING;
    for (unsigned step = 0; step < 100000 && status == TB_SYMBOL_PARAMETERS_PENDING; ++step)
        status = tb_symbol_parameters_step(&parameters, 127);
    CHECK(status == TB_SYMBOL_PARAMETERS_READY);
    TbSymbolParametersView_t view;
    CHECK(tb_symbol_parameters_view(&parameters, &view));
    CHECK(view.selected.entry_index == selected);
    size_t found = 0;
    for (uint64_t index = 0; index < tree.count; ++index) {
        const TbSymbolDieEntry_t *entry = &tree.entries[index];
        if (entry->parent != selected || (entry->tag != 0x05 && entry->tag != 0x18))
            continue;
        CHECK(found < view.count);
        const TbSymbolParameter_t *parameter = &view.parameters[found++];
        CHECK(parameter->record.entry_index == index && parameter->record.record_offset == entry->offset);
        CHECK(parameter->direct.tag == entry->tag);
        CHECK(parameter->unspecified == (entry->tag == 0x18));
        for (size_t field = 0; field < TB_SYMBOL_ENTITY_FIELDS; ++field) {
            const TbSymbolEntityField_t *fact = &parameter->direct.fields[field];
            CHECK(fact->selection.matches <= 1);
            if (fact->selection.matches) {
                CHECK(fact->selection.first_index >= entry->first_attribute);
                CHECK(fact->selection.first_index - entry->first_attribute < entry->attribute_count);
                const TbSymbolDieAttribute_t *raw = &tree.attributes[fact->selection.first_index];
                CHECK(raw->name == fact->attribute.name && raw->offset == fact->attribute.offset);
                CHECK(raw->value.form == fact->attribute.value.form);
                CHECK(raw->value.bits == fact->attribute.value.bits);
            }
        }
    }
    CHECK(found == view.count);
    ++parameter_queries;
    for (size_t i = 0; i < view.count; ++i) {
        if (view.parameters[i].unspecified)
            ++parameter_markers;
        else
            ++parameter_formals;
    }
    CHECK(tb_symbol_budget_release(&budget, charge) && budget.used == before);
}


static TbSymbolEntityKind_e check_entity(TbSymbolDiesView_t tree, TbSymbolAbbrevView_t schema,
                                         uint64_t die, uint64_t *declarations)
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
    TbSymbolEntityRole_e role;
    CHECK(tb_symbol_entity_role(&entity, &role));
    if (view.kind == TB_SYMBOL_ENTITY_FUNCTION || view.kind == TB_SYMBOL_ENTITY_DATA) {
        CHECK(role == (view.declaration.present && view.declaration.value
                          ? TB_SYMBOL_ROLE_DECLARATION : TB_SYMBOL_ROLE_DEFINITION));
    } else {
        CHECK(role == (view.kind == TB_SYMBOL_ENTITY_INLINE_INSTANCE
                          ? TB_SYMBOL_ROLE_INLINE_INSTANCE : TB_SYMBOL_ROLE_OTHER));
    }
    CHECK(view.kind <= TB_SYMBOL_ENTITY_INLINE_INSTANCE);
    if (view.tag == 0x2e || view.tag == 0x1d || view.tag == 0x15)
        check_parameters(tree, die);
    if (view.kind == TB_SYMBOL_ENTITY_FUNCTION)
        check_constant_field(tree, schema, die, TB_SYMBOL_CONSTANT_INLINE,
                             &view.fields[TB_SYMBOL_ENTITY_INLINE]);
    if (die == 0 && (view.tag == 0x11 || view.tag == 0x3c || view.tag == 0x41))
        check_constant_field(tree, schema, die, TB_SYMBOL_CONSTANT_LANGUAGE, NULL);
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


static bool check_graph(TbSymbolUnitsView_t units, TbSymbolDiesView_t tree, uint64_t unit_index,
                        uint64_t die_index, TbSymbolGraphNode_t *nodes, UnitCounts_t *counts,
                        TbSymbolGraphView_t *completed)
{
    TbSymbolGraph_t graph;
    CHECK(tb_symbol_graph_init(&graph, (TbSymbolReferenceTarget_t){
        .backing       = tree.backing,
        .section_index = tree.section_index,
        .unit_offset   = tree.unit_offset,
        .record_offset = tree.entries[die_index].offset,
        .entry_index   = die_index
    }, nodes, 64) == TB_SYMBOL_GRAPH_SOURCE);
    for (unsigned turn = 0; turn < 1000000; ++turn) {
        TbSymbolGraphNeed_t need;
        TbSymbolGraphView_t incomplete;
        CHECK(!tb_symbol_graph_view(&graph, &incomplete));
        if (tb_symbol_graph_need(&graph, &need)) {
            if (need.kind == TB_SYMBOL_GRAPH_SOURCE) {
                bool available = need.record.section_index == tree.section_index &&
                                 need.record.unit_offset == tree.unit_offset;
                (void)tb_symbol_graph_source(&graph, available ? &units : NULL,
                                             available ? &tree : NULL, unit_index);
            } else {
                TbSymbolRelationNeed_t target;
                TbSymbolRelationStatus_e status = tb_symbol_relation_need(need.source, &units, &target);
                bool available = status == TB_SYMBOL_RELATION_READY &&
                                 target.target.section_index == tree.section_index &&
                                 target.target.unit_offset == tree.unit_offset;
                (void)tb_symbol_graph_target(&graph, &units, available ? &tree : NULL);
            }
        }
        TbSymbolGraphStatus_e status = tb_symbol_graph_step(&graph, 31);
        if (status == TB_SYMBOL_GRAPH_READY) {
            TbSymbolGraphView_t view;
            CHECK(tb_symbol_graph_view(&graph, &view));
            CHECK(view.count && view.count <= 64);
            CHECK(view.nodes[0].record.record_offset == tree.entries[die_index].offset);
            for (size_t i = 0; i < view.count; ++i) {
                const TbSymbolGraphNode_t *node = &view.nodes[i];
                CHECK(node->complete && node->record.section_index == tree.section_index);
                CHECK(node->record.unit_offset == tree.unit_offset);
                CHECK(node->record.entry_index < tree.count);
                CHECK(node->record.record_offset == tree.entries[node->record.entry_index].offset);
                CHECK(node->context.tag == tree.entries[node->record.entry_index].tag);
                CHECK(node->context.version == units.entries[unit_index].header.version);
                for (unsigned kind = 0; kind < TB_SYMBOL_GRAPH_RELATIONS; ++kind) {
                    if (node->present & (1u << kind)) {
                        CHECK(node->edges[kind].source.reference.source_record == node->record.record_offset);
                        CHECK(node->edges[kind].source.kind == (TbSymbolRelationKind_e)kind);
                    }
                }
            }
            ++counts->graphs;
            counts->graph_nodes += view.count;
            *completed = view;
            return true;
        }
        if (status == TB_SYMBOL_GRAPH_DEPENDENCY) {
            ++counts->graph_dependencies;
            return false;
        }
        if (status == TB_SYMBOL_GRAPH_RELATION) {
            TbSymbolGraphFailure_t failure;
            CHECK(tb_symbol_graph_failure(&graph, &failure));
            CHECK(failure.relation_status == TB_SYMBOL_RELATION_UNSUPPORTED);
            ++counts->graph_unsupported;
            return false;
        }
        CHECK(status == TB_SYMBOL_GRAPH_PENDING || status == TB_SYMBOL_GRAPH_SOURCE ||
              status == TB_SYMBOL_GRAPH_TARGET);
    }
    CHECK(false);
    return false;
}


typedef struct {
    TbSymbolSectionsView_t directory;
    TbSymbolUnitsView_t    units;
    TbSymbolDiesView_t     tree;
    StringSections_t      strings;
    uint64_t              unit_index;
    int                   descriptor;
} InheritedScope_t;

static void check_inherited_text(InheritedScope_t scope, TbSymbolGraphView_t graph,
                                  uint64_t name, uint64_t expected_owners)
{
    uint64_t before = budget.used;
    TbSymbolInheritedText_t *text = tb_symbol_inherited_text_create(scope.directory, graph,
        (TbSymbolInheritedTextSelection_t){
            .name                = name,
            .string_section      = scope.strings.strings,
            .line_string_section = scope.strings.line_strings,
            .offset_section      = scope.strings.offsets,
            .descriptor          = scope.descriptor
        }, &budget);
    CHECK(text);
    TbSymbolInheritedTextStatus_e status = TB_SYMBOL_INHERITED_TEXT_PENDING;
    for (unsigned turn = 0; turn < 1000000; ++turn) {
        TbSymbolInheritedTextNeed_t need;
        if (tb_symbol_inherited_text_need(text, &need)) {
            CHECK(need.source.record.unit_offset == scope.tree.unit_offset);
            CHECK(tb_symbol_inherited_text_source(text, &scope.units, &scope.tree, scope.unit_index) ==
                  TB_SYMBOL_INHERITED_TEXT_PENDING);
        }
        status = tb_symbol_inherited_text_step(text, 257);
        if (status != TB_SYMBOL_INHERITED_TEXT_PENDING && status != TB_SYMBOL_INHERITED_TEXT_SOURCE)
            break;
    }
    CHECK(status == (expected_owners ? TB_SYMBOL_INHERITED_TEXT_FOUND : TB_SYMBOL_INHERITED_TEXT_ABSENT));
    TbSymbolInheritedTextView_t view;
    if (expected_owners) {
        CHECK(tb_symbol_inherited_text_view(text, &view) && view.count == expected_owners);
        CHECK(view.selected.record_offset == graph.nodes[0].record.record_offset);
        for (size_t i = 0; i < view.count; ++i) {
            CHECK(view.facts[i].node_index < graph.count);
            CHECK(view.facts[i].text.source_record ==
                  graph.nodes[view.facts[i].node_index].record.record_offset);
            CHECK(view.facts[i].text.attribute.name == name);
            CHECK(view.facts[i].unit.offset == scope.tree.unit_offset);
            CHECK(view.facts[i].text.text.bytes == view.facts[0].text.text.bytes);
        }
    } else {
        CHECK(!tb_symbol_inherited_text_view(text, &view));
    }
    tb_symbol_inherited_text_destroy(text);
    CHECK(budget.used == before);
}


static void check_inherited(InheritedScope_t scope, TbSymbolGraphView_t graph,
                             TbSymbolInheritSlot_t *slots, UnitCounts_t *counts)
{
    const uint64_t names[] = {0x03, 0x6e, 0x49, 0x3a, 0x3b, 0x39};
    for (size_t field = 0; field < sizeof(names) / sizeof(names[0]); ++field) {
        TbSymbolInherit_t inherit;
        CHECK(tb_symbol_inherit_init(&inherit, graph, names[field], slots, 64) == TB_SYMBOL_INHERIT_PENDING);
        bool complete = false;
        for (unsigned turn = 0; turn < 100000 && !complete; ++turn) {
            TbSymbolInheritNeed_t need;
            if (tb_symbol_inherit_need(&inherit, &need)) {
                CHECK(need.record.section_index == scope.tree.section_index);
                CHECK(need.record.unit_offset == scope.tree.unit_offset);
                CHECK(tb_symbol_inherit_source(&inherit, &scope.units, &scope.tree, scope.unit_index) ==
                      TB_SYMBOL_INHERIT_PENDING);
            }
            TbSymbolInheritStatus_e status = tb_symbol_inherit_step(&inherit, 31);
            complete = status == TB_SYMBOL_INHERIT_UNIQUE || status == TB_SYMBOL_INHERIT_MULTIPLE ||
                       status == TB_SYMBOL_INHERIT_ABSENT;
            CHECK(complete || status == TB_SYMBOL_INHERIT_PENDING || status == TB_SYMBOL_INHERIT_SOURCE);
        }
        CHECK(complete);
        TbSymbolInheritResult_t result;
        CHECK(tb_symbol_inherit_result(&inherit, &result));
        CHECK(result.name == names[field] && result.owner_count <= graph.count);
        uint64_t owners = 0;
        for (size_t i = 0; i < graph.count; ++i) {
            TbSymbolInheritOwner_t owner;
            if (!tb_symbol_inherit_owner(&inherit, i, &owner))
                continue;
            ++owners;
            const TbSymbolGraphNode_t *node = &graph.nodes[i];
            const TbSymbolDieEntry_t *entry = &scope.tree.entries[node->record.entry_index];
            CHECK(owner.node_index == i && owner.selection.matches == 1);
            CHECK(owner.selection.section_index == node->record.section_index);
            CHECK(owner.selection.unit_offset == node->record.unit_offset);
            CHECK(owner.selection.record_offset == node->record.record_offset);
            CHECK(owner.selection.first_index >= entry->first_attribute);
            uint64_t index = owner.selection.first_index - entry->first_attribute;
            CHECK(index < entry->attribute_count);
            const TbSymbolDieAttribute_t *actual = &scope.tree.attributes[owner.selection.first_index];
            CHECK(actual->name == names[field] && actual->offset == owner.attribute.offset);
            CHECK(actual->value.form == owner.attribute.value.form);
            CHECK(actual->value.bits == owner.attribute.value.bits);
            if (names[field] == 0x03 || names[field] == 0x6e) {
                CHECK(check_attribute_string(scope.directory, scope.units, scope.tree,
                                             (TbSymbolReferenceSelection_t){
                                                 .unit_index      = scope.unit_index,
                                                 .die_index       = node->record.entry_index,
                                                 .attribute_index = index
                                             }, scope.strings, scope.descriptor));
            }
            if (i)
                ++counts->inherited_indirect;
        }
        CHECK(owners == result.owner_count);
        if (names[field] == 0x03 || names[field] == 0x6e)
            check_inherited_text(scope, graph, names[field], owners);
        ++counts->inherited_queries;
        counts->inherited_owners += owners;
        if (owners > 1)
            ++counts->inherited_multiple;
    }
}


static void check_context_anchors(TbSymbolUnitsView_t units, TbSymbolDiesView_t tree,
                                   uint64_t unit_index, TbSymbolGraphView_t graph)
{
    TbSymbolContext_t context;
    TbSymbolContextAnchor_t anchors[64];
    size_t charge = sizeof(context) + sizeof(anchors);
    CHECK(tb_symbol_budget_reserve(&budget, charge));
    CHECK(tb_symbol_context_init(&context, graph, anchors, 64) == TB_SYMBOL_CONTEXT_PENDING);
    TbSymbolContextStatus_e status = TB_SYMBOL_CONTEXT_PENDING;
    for (unsigned turn = 0; turn < 100000; ++turn) {
        TbSymbolGraphNeed_t need;
        if (tb_symbol_context_need(&context, &need)) {
            CHECK(need.record.unit_offset == tree.unit_offset);
            CHECK(tb_symbol_context_source(&context, &units, &tree, unit_index) == TB_SYMBOL_CONTEXT_PENDING);
        }
        status = tb_symbol_context_step(&context, 31);
        if (status != TB_SYMBOL_CONTEXT_PENDING && status != TB_SYMBOL_CONTEXT_SOURCE)
            break;
    }
    CHECK(status == TB_SYMBOL_CONTEXT_READY);
    TbSymbolContextView_t view;
    CHECK(tb_symbol_context_view(&context, &view));
    size_t terminal_count = 0;
    for (size_t i = 0; i < graph.count; ++i) {
        if (!graph.nodes[i].present)
            ++terminal_count;
    }
    CHECK(view.count == terminal_count && view.selected.record_offset == graph.nodes[0].record.record_offset);
    for (size_t i = 0; i < view.count; ++i) {
        const TbSymbolContextAnchor_t *anchor = &view.anchors[i];
        CHECK(anchor->node_index < graph.count);
        CHECK(!graph.nodes[anchor->node_index].present);
        CHECK(anchor->record.record_offset == graph.nodes[anchor->node_index].record.record_offset);
        CHECK(anchor->unit.offset == units.entries[unit_index].offset);
        CHECK(anchor->unit_root.record_offset == tree.entries[0].offset);
        uint64_t parent = tree.entries[anchor->record.entry_index].parent;
        if (parent == UINT64_MAX) {
            CHECK(anchor->kind == TB_SYMBOL_CONTEXT_UNIT_ROOT && anchor->record.entry_index == 0);
        } else {
            CHECK(anchor->kind == (parent ? TB_SYMBOL_CONTEXT_LEXICAL_PARENT
                                          : TB_SYMBOL_CONTEXT_UNIT_PARENT));
            CHECK(anchor->parent.entry_index == parent &&
                  anchor->parent.record_offset == tree.entries[parent].offset);
            CHECK(anchor->parent_context.tag == tree.entries[parent].tag);
            CHECK(anchor->parent_context.version == units.entries[unit_index].header.version);
        }
    }
    CHECK(tb_symbol_budget_release(&budget, charge));
}


typedef struct {
    TbSymbolGraphNode_t     nodes[64];
    TbSymbolContextAnchor_t anchors[64];
    TbSymbolGraphView_t     relationships;
    TbSymbolContextView_t   contexts;
} NativeScopeInput_t;


static void build_scope_input(TbSymbolUnitsView_t units, TbSymbolDiesView_t tree,
                              uint64_t unit_index, TbSymbolReferenceTarget_t selected,
                              NativeScopeInput_t *input)
{
    TbSymbolGraph_t relationships;
    TbSymbolContext_t contexts;
    size_t charge = sizeof(relationships) + sizeof(contexts);
    CHECK(tb_symbol_budget_reserve(&budget, charge));
    CHECK(tb_symbol_graph_init(&relationships, selected, input->nodes, 64) == TB_SYMBOL_GRAPH_SOURCE);
    for (unsigned turn = 0; turn < 1000000 && relationships.status != TB_SYMBOL_GRAPH_READY; ++turn) {
        TbSymbolGraphNeed_t need;
        if (tb_symbol_graph_need(&relationships, &need)) {
            if (need.kind == TB_SYMBOL_GRAPH_SOURCE) {
                CHECK(need.record.unit_offset == tree.unit_offset);
                CHECK(tb_symbol_graph_source(&relationships, &units, &tree, unit_index) ==
                      TB_SYMBOL_GRAPH_PENDING);
            } else {
                TbSymbolRelationNeed_t target;
                CHECK(tb_symbol_relation_need(need.source, &units, &target) == TB_SYMBOL_RELATION_READY);
                CHECK(target.target.unit_offset == tree.unit_offset);
                CHECK(tb_symbol_graph_target(&relationships, &units, &tree) == TB_SYMBOL_GRAPH_PENDING);
            }
        }
        (void)tb_symbol_graph_step(&relationships, 31);
    }
    CHECK(tb_symbol_graph_view(&relationships, &input->relationships));
    CHECK(tb_symbol_context_init(&contexts, input->relationships, input->anchors, 64) ==
          TB_SYMBOL_CONTEXT_PENDING);
    for (unsigned turn = 0; turn < 1000000 && contexts.status != TB_SYMBOL_CONTEXT_READY; ++turn) {
        TbSymbolGraphNeed_t need;
        if (tb_symbol_context_need(&contexts, &need)) {
            CHECK(need.record.unit_offset == tree.unit_offset);
            CHECK(tb_symbol_context_source(&contexts, &units, &tree, unit_index) ==
                  TB_SYMBOL_CONTEXT_PENDING);
        }
        (void)tb_symbol_context_step(&contexts, 31);
    }
    CHECK(tb_symbol_context_view(&contexts, &input->contexts));
    CHECK(tb_symbol_budget_release(&budget, charge));
}


static TbSymbolInheritedTextStatus_e qualified_name_source(InheritedScope_t scope,
                                                          TbSymbolGraphView_t relationships,
                                                          TbSymbolInheritedTextView_t *adopted,
                                                          TbSymbolInheritedTextFact_t **owned)
{
    TbSymbolInheritedText_t *text = tb_symbol_inherited_text_create(scope.directory, relationships,
        (TbSymbolInheritedTextSelection_t){
            .name                = 3,
            .string_section      = scope.strings.strings,
            .line_string_section = scope.strings.line_strings,
            .offset_section      = scope.strings.offsets,
            .descriptor          = scope.descriptor
        }, &budget);
    CHECK(text);
    TbSymbolInheritedTextStatus_e status = TB_SYMBOL_INHERITED_TEXT_PENDING;
    for (unsigned turn = 0; turn < 1000000; ++turn) {
        TbSymbolInheritedTextNeed_t need;
        if (tb_symbol_inherited_text_need(text, &need)) {
            CHECK(need.source.record.unit_offset == scope.tree.unit_offset);
            CHECK(tb_symbol_inherited_text_source(text, &scope.units, &scope.tree, scope.unit_index) ==
                  TB_SYMBOL_INHERITED_TEXT_PENDING);
        }
        status = tb_symbol_inherited_text_step(text, 257);
        if (status != TB_SYMBOL_INHERITED_TEXT_PENDING && status != TB_SYMBOL_INHERITED_TEXT_SOURCE)
            break;
    }
    CHECK(status == TB_SYMBOL_INHERITED_TEXT_FOUND || status == TB_SYMBOL_INHERITED_TEXT_ABSENT);
    if (status == TB_SYMBOL_INHERITED_TEXT_FOUND) {
        TbSymbolInheritedTextView_t view;
        CHECK(tb_symbol_inherited_text_view(text, &view));
        CHECK(view.count && view.count <= relationships.count);
        size_t charge = view.count * sizeof(**owned);
        CHECK(tb_symbol_budget_reserve(&budget, charge));
        *owned = malloc(charge);
        CHECK(*owned);
        memcpy(*owned, view.facts, charge);
        *adopted = view;
        adopted->facts = *owned;
    }
    /* Adopt every completed pointer-free owner fact before child scratch ends;
     * the original unit/tree/schema/directory and frozen backing stay pinned. */
    tb_symbol_inherited_text_destroy(text);
    return status;
}


static void check_qualified_context(InheritedScope_t source, TbSymbolAbbrevView_t schema,
                                     TbSymbolScopeView_t scope)
{
    TbSymbolInheritedTextFact_t *names[64] = {0};
    size_t charges[64] = {0};
    size_t before = budget.used;
    size_t charge = sizeof(names) + sizeof(charges) + sizeof(TbSymbolConstant_t);
    CHECK(scope.node_count <= 64 && tb_symbol_budget_reserve(&budget, charge));
    TbSymbolQualified_t *qualified = tb_symbol_qualified_create(source.directory, scope, source.descriptor,
                                                               &budget);
    CHECK(qualified);
    TbSymbolQualifiedStatus_e status = TB_SYMBOL_QUALIFIED_PENDING;
    for (unsigned turn = 0; turn < 1000000; ++turn) {
        TbSymbolQualifiedNeed_t need;
        if (tb_symbol_qualified_need(qualified, &need)) {
            CHECK(need.record.unit_offset == source.tree.unit_offset);
            if (need.kind == TB_SYMBOL_QUALIFIED_NAME) {
                TbSymbolInheritedTextView_t value = {0};
                CHECK(need.node_index < scope.node_count && !names[need.node_index]);
                TbSymbolInheritedTextStatus_e found = qualified_name_source(source,
                    scope.nodes[need.node_index].relationships, &value, &names[need.node_index]);
                charges[need.node_index] = value.count * sizeof(*value.facts);
                status = tb_symbol_qualified_name(qualified, found, &value);
            } else {
                CHECK(need.record.entry_index == 0 &&
                      need.record.record_offset == source.tree.entries[0].offset);
                TbSymbolConstant_t constant;
                TbSymbolConstantStatus_e found = tb_symbol_constant_init(&constant, source.tree, 0,
                                                                          TB_SYMBOL_CONSTANT_LANGUAGE);
                CHECK(found == TB_SYMBOL_CONSTANT_PENDING);
                for (unsigned step = 0; step < 100000; ++step) {
                    if (found == TB_SYMBOL_CONSTANT_SCHEMA)
                        CHECK(tb_symbol_constant_schema(&constant, &schema) == TB_SYMBOL_CONSTANT_PENDING);
                    found = tb_symbol_constant_step(&constant, 31);
                    if (found != TB_SYMBOL_CONSTANT_PENDING && found != TB_SYMBOL_CONSTANT_SCHEMA)
                        break;
                }
                CHECK(found == TB_SYMBOL_CONSTANT_FOUND || found == TB_SYMBOL_CONSTANT_ABSENT);
                TbSymbolConstantView_t value = {0};
                if (found == TB_SYMBOL_CONSTANT_FOUND)
                    CHECK(tb_symbol_constant_view(&constant, &value));
                status = tb_symbol_qualified_language(qualified, found, &value);
            }
            if (status != TB_SYMBOL_QUALIFIED_PENDING)
                break;
        }
        status = tb_symbol_qualified_step(qualified, 257);
        if (status != TB_SYMBOL_QUALIFIED_PENDING && status != TB_SYMBOL_QUALIFIED_SOURCE)
            break;
    }
    /* Anonymous/local contexts and unit-root selections remain explicit profile
     * exclusions; decoder/resource/operational failure is never accepted here. */
    if (status != TB_SYMBOL_QUALIFIED_READY && status != TB_SYMBOL_QUALIFIED_UNNAMED &&
        status != TB_SYMBOL_QUALIFIED_UNSUPPORTED) {
        TbSymbolQualifiedFailure_t failure = tb_symbol_qualified_failure(qualified);
        fprintf(stderr, "qualified status %u reason %u node %zu anchor %zu, budget %llu/%llu\n",
                status, failure.reason, failure.node_index, failure.anchor_index,
                (unsigned long long)budget.used, (unsigned long long)budget.limit);
    }
    CHECK(status == TB_SYMBOL_QUALIFIED_READY || status == TB_SYMBOL_QUALIFIED_UNNAMED ||
          status == TB_SYMBOL_QUALIFIED_UNSUPPORTED);
    TbSymbolQualifiedView_t view;
    if (status == TB_SYMBOL_QUALIFIED_READY) {
        ++qualified_ready;
        CHECK(tb_symbol_qualified_view(qualified, &view) && view.count == scope.node_count);
        CHECK(view.selected.record_offset == scope.nodes[0].record.record_offset);
        CHECK(view.language_count == scope.edge_count);
        size_t node = 0;
        size_t components = 0;
        uint64_t bytes = 0;
        while (node != SIZE_MAX) {
            CHECK(node < view.count && components < view.count && view.nodes[node].complete);
            const TbSymbolQualifiedNode_t *part = &view.nodes[node];
            CHECK(part->name.facts == names[node] && part->name.count);
            uint64_t part_bytes = part->name.facts[0].text.text.bytes;
            CHECK(part_bytes && tb_symbol_add(bytes, part_bytes, &bytes));
            if (components)
                CHECK(tb_symbol_add(bytes, 2, &bytes));
            ++components;
            node = part->parent_node;
        }
        CHECK(components == view.nodes[0].part_count && bytes == view.nodes[0].bytes);
    } else {
        if (status == TB_SYMBOL_QUALIFIED_UNNAMED)
            ++qualified_unnamed;
        else
            ++qualified_unavailable;
        CHECK(!tb_symbol_qualified_view(qualified, &view));
    }
    tb_symbol_qualified_destroy(qualified);
    for (size_t i = 0; i < scope.node_count; ++i) {
        free(names[i]);
        CHECK(tb_symbol_budget_release(&budget, charges[i]));
    }
    CHECK(tb_symbol_budget_release(&budget, charge) && budget.used == before);
}


/* Independent fixed-point oracle: resolve every complete node from its parent
 * classifications rather than following the production traversal stack. Unknown
 * outer contexts cannot erase a known local barrier. */
static unsigned locality_oracle(TbSymbolScopeView_t scope, unsigned values[64])
{
    CHECK(scope.node_count <= 64);
    memset(values, 0, 64 * sizeof(*values));
    for (size_t round = 0; round <= scope.node_count; ++round) {
        for (size_t i = 0; i < scope.node_count; ++i) {
            if (values[i])
                continue;
            const TbSymbolScopeNode_t *node = &scope.nodes[i];
            unsigned agreed = 0;
            bool pending = false;
            for (size_t j = 0; j < node->contexts.count; ++j) {
                const TbSymbolScopeEdge_t *edge = &scope.edges[node->first_edge + j];
                unsigned value = 1;
                if (edge->target_node != SIZE_MAX) {
                    CHECK(edge->target_node < scope.node_count);
                    uint64_t tag = scope.nodes[edge->target_node].context.tag;
                    if (tag == 0x2e || tag == 0x1d || tag == 0x0b || tag == 0x25 || tag == 0x32)
                        value = 2;
                    else if (tag == 0x39 || tag == 0x02 || tag == 0x13 || tag == 0x17)
                        value = values[edge->target_node];
                    else
                        value = 3;
                }
                if (!value) {
                    pending = true;
                } else if (!agreed || agreed == value) {
                    agreed = value;
                } else {
                    agreed = agreed == 3 || value == 3 ? 3 : 4;
                }
            }
            if (!pending)
                values[i] = agreed;
        }
    }
    CHECK(values[0]);
    return values[0];
}


static void check_logical_locality(TbSymbolScopeView_t scope)
{
    TbSymbolLocality_t locality;
    TbSymbolLocalitySlot_t slots[64];
    unsigned oracle[64];
    size_t charge = sizeof(locality) + sizeof(slots) + sizeof(oracle);
    CHECK(tb_symbol_budget_reserve(&budget, charge));
    CHECK(tb_symbol_locality_init(&locality, scope, slots, 64) == TB_SYMBOL_LOCALITY_PENDING);
    TbSymbolLocalityStatus_e status = TB_SYMBOL_LOCALITY_PENDING;
    for (unsigned step = 0; step < 100000 && status == TB_SYMBOL_LOCALITY_PENDING; ++step)
        status = tb_symbol_locality_step(&locality, 31);
    if (status != TB_SYMBOL_LOCALITY_READY && status != TB_SYMBOL_LOCALITY_UNSUPPORTED) {
        TbSymbolLocalityFailure_t failure;
        CHECK(tb_symbol_locality_failure(&locality, &failure));
        fprintf(stderr, "native locality failure %u node %zu anchor %zu tag %llu\n", status,
                failure.node_index, failure.anchor_index, (unsigned long long)failure.parent_tag);
    }
    CHECK(status == TB_SYMBOL_LOCALITY_READY || status == TB_SYMBOL_LOCALITY_UNSUPPORTED);
    unsigned expected = locality_oracle(scope, oracle);
    TbSymbolLocalityView_t result;
    if (status == TB_SYMBOL_LOCALITY_READY) {
        CHECK(tb_symbol_locality_view(&locality, &result));
        CHECK(result.selected.record_offset == scope.nodes[0].record.record_offset);
        CHECK(result.count == scope.node_count && result.slots[0].has_value);
        CHECK(result.scope.nodes == scope.nodes && result.scope.edges == scope.edges);
        CHECK(expected == 1 || expected == 2);
        CHECK(result.value == (expected == 1 ? TB_SYMBOL_SCOPE_NONLOCAL : TB_SYMBOL_SCOPE_LOCAL));
        if (result.value == TB_SYMBOL_SCOPE_NONLOCAL)
            ++locality_nonlocal;
        else
            ++locality_local;
    } else {
        CHECK(expected == 3);
        ++locality_unsupported;
        CHECK(!tb_symbol_locality_view(&locality, &result));
    }
    CHECK(tb_symbol_budget_release(&budget, charge));
}


static void check_recursive_context(InheritedScope_t source, TbSymbolAbbrevView_t schema,
                                    TbSymbolReferenceTarget_t selected)
{
    TbSymbolUnitsView_t units      = source.units;
    TbSymbolDiesView_t  tree       = source.tree;
    uint64_t           unit_index = source.unit_index;

    TbSymbolScope_t scope;
    TbSymbolScopeNode_t nodes[64];
    TbSymbolScopeEdge_t edges[256];
    NativeScopeInput_t *inputs[64] = {0};
    size_t input_count = 0;
    size_t charge = sizeof(scope) + sizeof(nodes) + sizeof(edges) + sizeof(inputs);
    size_t before = budget.used;
    CHECK(tb_symbol_budget_reserve(&budget, charge));
    CHECK(tb_symbol_scope_init(&scope, selected, nodes, 64, edges, 256) == TB_SYMBOL_SCOPE_SOURCE);
    TbSymbolScopeStatus_e status = TB_SYMBOL_SCOPE_SOURCE;
    for (unsigned turn = 0; turn < 1000000; ++turn) {
        TbSymbolScopeNeed_t need;
        if (tb_symbol_scope_need(&scope, &need)) {
            CHECK(input_count < 64 && need.record.unit_offset == tree.unit_offset);
            CHECK(tb_symbol_budget_reserve(&budget, sizeof(NativeScopeInput_t)));
            NativeScopeInput_t *input = calloc(1, sizeof(*input));
            CHECK(input);
            inputs[input_count++] = input;
            build_scope_input(units, tree, unit_index, need.record, input);
            CHECK(tb_symbol_scope_source(&scope, &input->relationships, &input->contexts) ==
                  TB_SYMBOL_SCOPE_PENDING);
        }
        status = tb_symbol_scope_step(&scope, 31);
        if (status != TB_SYMBOL_SCOPE_PENDING && status != TB_SYMBOL_SCOPE_SOURCE)
            break;
    }
    CHECK(status == TB_SYMBOL_SCOPE_READY);
    TbSymbolScopeView_t view;
    CHECK(tb_symbol_scope_view(&scope, &view) && view.node_count == input_count);
    CHECK(view.nodes[0].record.record_offset == selected.record_offset);
    for (size_t i = 0; i < view.node_count; ++i) {
        const TbSymbolScopeNode_t *node = &view.nodes[i];
        CHECK(node->complete && node->first_edge <= view.edge_count);
        CHECK(node->contexts.count <= view.edge_count - node->first_edge);
        for (size_t j = 0; j < node->contexts.count; ++j) {
            const TbSymbolScopeEdge_t *edge = &view.edges[node->first_edge + j];
            const TbSymbolContextAnchor_t *anchor = &node->contexts.anchors[j];
            CHECK(edge->anchor_index == j);
            if (anchor->kind == TB_SYMBOL_CONTEXT_LEXICAL_PARENT) {
                CHECK(edge->target_node < view.node_count);
                const TbSymbolScopeNode_t *target = &view.nodes[edge->target_node];
                CHECK(target->record.record_offset == anchor->parent.record_offset);
                CHECK(target->record.unit_offset == anchor->parent.unit_offset);
                CHECK(target->context.tag == anchor->parent_context.tag);
                CHECK(target->context.parent_record == anchor->parent_context.parent_record);
            } else {
                CHECK(edge->target_node == SIZE_MAX);
            }
        }
    }
    check_logical_locality(view);
    check_qualified_context(source, schema, view);
    for (size_t i = 0; i < input_count; ++i) {
        free(inputs[i]);
        CHECK(tb_symbol_budget_release(&budget, sizeof(NativeScopeInput_t)));
    }
    CHECK(tb_symbol_budget_release(&budget, charge) && budget.used == before);
}


static void check_physical_inline(TbSymbolUnitsView_t units, TbSymbolDiesView_t tree,
                                  TbSymbolAbbrevView_t schema, uint64_t unit_index, uint64_t die)
{
    TbSymbolInlineScope_t     scope;
    TbSymbolInlineScopeFact_t facts[64];
    size_t                   charge = sizeof(scope) + sizeof(facts);
    CHECK(tb_symbol_budget_reserve(&budget, charge));
    TbSymbolInlineScopeStatus_e status = tb_symbol_inline_scope_init(&scope, units, tree, unit_index,
                                                                    die, facts, 64);
    CHECK(status == TB_SYMBOL_INLINE_SCOPE_PENDING);
    for (unsigned step = 0; step < 100000; ++step) {
        if (status == TB_SYMBOL_INLINE_SCOPE_SCHEMA)
            CHECK(tb_symbol_inline_scope_schema(&scope, &schema) == TB_SYMBOL_INLINE_SCOPE_PENDING);
        status = tb_symbol_inline_scope_step(&scope, 31);
        if (status != TB_SYMBOL_INLINE_SCOPE_PENDING && status != TB_SYMBOL_INLINE_SCOPE_SCHEMA)
            break;
    }
    if (status != TB_SYMBOL_INLINE_SCOPE_READY)
        fprintf(stderr, "physical inline scope failed status %u record %llu\n", status,
                (unsigned long long)tree.entries[die].offset);
    CHECK(status == TB_SYMBOL_INLINE_SCOPE_READY);
    TbSymbolInlineScopeView_t view;
    CHECK(tb_symbol_inline_scope_view(&scope, &view));
    CHECK(view.selected.entry_index == die && view.selected.record_offset == tree.entries[die].offset);
    uint64_t expected_abstract = UINT64_MAX;
    uint64_t expected_concrete = UINT64_MAX;
    size_t   expected_facts    = 0;
    for (uint64_t at = die; at != UINT64_MAX; at = tree.entries[at].parent) {
        CHECK(at < tree.count);
        const TbSymbolDieEntry_t *entry = &tree.entries[at];
        if (entry->tag == 0x1d && expected_concrete == UINT64_MAX)
            expected_concrete = at;
        if (entry->tag != 0x2e)
            continue;
        CHECK(expected_facts < view.count);
        const TbSymbolInlineScopeFact_t *fact = &view.facts[expected_facts++];
        CHECK(fact->record.entry_index == at && fact->record.record_offset == entry->offset);
        const TbSymbolDieAttribute_t *inline_value = NULL;
        for (uint64_t i = 0; i < entry->attribute_count; ++i) {
            const TbSymbolDieAttribute_t *attribute = &tree.attributes[entry->first_attribute + i];
            if (attribute->name == TB_SYMBOL_CONSTANT_INLINE) {
                CHECK(!inline_value);
                inline_value = attribute;
            }
        }
        if (!inline_value) {
            CHECK(fact->status == TB_SYMBOL_CONSTANT_ABSENT && !fact->selection.matches);
            continue;
        }
        CHECK(fact->status == TB_SYMBOL_CONSTANT_FOUND && fact->selection.matches == 1);
        uint64_t code = inline_value->value.bits;
        if (inline_value->value.form == TB_SYMBOL_FORM_IMPLICIT_CONST && !inline_value->value.indirect) {
            CHECK(inline_value->schema_attribute < schema.attribute_count);
            CHECK(schema.attributes[inline_value->schema_attribute].implicit_fits &&
                  schema.attributes[inline_value->schema_attribute].implicit_value >= 0);
            code = (uint64_t)schema.attributes[inline_value->schema_attribute].implicit_value;
        }
        CHECK(code <= 3 && fact->constant.value == code);
        if (code && expected_abstract == UINT64_MAX)
            expected_abstract = at;
    }
    CHECK(expected_facts == view.count);
    CHECK(view.abstract_available == (expected_abstract != UINT64_MAX));
    CHECK(view.concrete_available == (expected_concrete != UINT64_MAX));
    CHECK(view.selected_abstract == (expected_abstract == die));
    CHECK(view.selected_concrete == (expected_concrete == die));
    if (view.abstract_available)
        CHECK(view.abstract_root.entry_index == expected_abstract);
    if (view.concrete_available)
        CHECK(view.concrete_root.entry_index == expected_concrete);
    ++inline_ancestry_queries;
    inline_abstract_markers += view.abstract_available;
    inline_concrete_markers += view.concrete_available;
    CHECK(tb_symbol_budget_release(&budget, charge));
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
    uint64_t graph_bytes = 64 * sizeof(TbSymbolGraphNode_t);
    CHECK(tb_symbol_budget_reserve(&budget, graph_bytes));
    TbSymbolGraphNode_t *graph_nodes = malloc((size_t)graph_bytes);
    CHECK(graph_nodes);
    uint64_t inherited_bytes = 64 * sizeof(TbSymbolInheritSlot_t);
    CHECK(tb_symbol_budget_reserve(&budget, inherited_bytes));
    TbSymbolInheritSlot_t *inherited_slots = malloc((size_t)inherited_bytes);
    CHECK(inherited_slots);
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
                check_physical_inline(view, tree, table, i, die);
                TbSymbolEntityKind_e kind = check_entity(tree, table, die, &declarations);
                ++entity_kinds[kind];
                if (kind != TB_SYMBOL_ENTITY_OTHER || record->tag == 0x39) {
                    TbSymbolGraphView_t completed;
                    if (check_graph(view, tree, i, die, graph_nodes, &relations, &completed)) {
                        check_inherited((InheritedScope_t){
                            .directory  = directory,
                            .units      = view,
                            .tree       = tree,
                            .strings    = string_sections,
                            .unit_index = i,
                            .descriptor = selection.descriptor
                        }, completed, inherited_slots, &relations);
                        check_context_anchors(view, tree, i, completed);
                        check_recursive_context((InheritedScope_t){
                            .directory  = directory,
                            .units      = view,
                            .tree       = tree,
                            .strings    = string_sections,
                            .unit_index = i,
                            .descriptor = selection.descriptor
                        }, table, completed.nodes[0].record);
                    }
                }
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
        .inherited_queries     = relations.inherited_queries,
        .inherited_owners      = relations.inherited_owners,
        .inherited_indirect    = relations.inherited_indirect,
        .inherited_multiple    = relations.inherited_multiple,
        .graphs                = relations.graphs,
        .graph_nodes           = relations.graph_nodes,
        .graph_dependencies    = relations.graph_dependencies,
        .graph_unsupported     = relations.graph_unsupported,
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
    free(inherited_slots);
    CHECK(tb_symbol_budget_release(&budget, inherited_bytes));
    free(graph_nodes);
    CHECK(tb_symbol_budget_release(&budget, graph_bytes));
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
                unit_counts.inherited_queries += counts.inherited_queries;
                unit_counts.inherited_owners += counts.inherited_owners;
                unit_counts.inherited_indirect += counts.inherited_indirect;
                unit_counts.inherited_multiple += counts.inherited_multiple;
                unit_counts.graphs += counts.graphs;
                unit_counts.graph_nodes += counts.graph_nodes;
                unit_counts.graph_dependencies += counts.graph_dependencies;
                unit_counts.graph_unsupported += counts.graph_unsupported;
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
    printf("native inherited queries %llu, owners %llu, indirect owners %llu, multiple %llu\n",
           (unsigned long long)unit_counts.inherited_queries,
           (unsigned long long)unit_counts.inherited_owners,
           (unsigned long long)unit_counts.inherited_indirect,
           (unsigned long long)unit_counts.inherited_multiple);
    printf("native graphs %llu, reachable nodes %llu, dependencies %llu, unsupported %llu\n",
           (unsigned long long)unit_counts.graphs, (unsigned long long)unit_counts.graph_nodes,
           (unsigned long long)unit_counts.graph_dependencies,
           (unsigned long long)unit_counts.graph_unsupported);
    return unit_counts;
}


int main(int argc, char **argv)
{
    CHECK(argc >= 2 && elf_version(EV_CURRENT) != EV_NONE);
    char directory[] = "/tmp/tb-symbol-elf-native-XXXXXX";
    CHECK(mkdtemp(directory));
    char pool[256];
    CHECK(snprintf(pool, sizeof(pool), "%s/pool", directory) > 0);
    /* The self-inspected harness includes every linked decoder's debug metadata.
     * Keep its construction allowance explicit and separate from production sizing. */
    CHECK(tb_symbol_budget_init(&budget, 4 * 1024 * 1024));
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
            CHECK(counts.graphs && counts.graph_nodes > counts.graphs);
            CHECK(!counts.graph_dependencies && !counts.graph_unsupported);
            CHECK(counts.inherited_queries == counts.graphs * 6 && counts.inherited_indirect);
            CHECK(counts.relation_unsupported == counts.vendor_call_sites);
            CHECK((version == 4 ? counts.dwarf4_units : counts.dwarf5_units) == counts.total);
            CHECK(version != 4 || counts.legacy_members);
            printf("PASS C++ direct relationship fixture DWARF %u\n", version);
        } else {
            (void)check_file(argv[i]);
        }
    }
    CHECK(inline_ancestry_queries && inline_abstract_markers && inline_concrete_markers);
    printf("PASS native physical inline ancestry: %llu queries, "
           "%llu with abstract, %llu with concrete marker\n",
           (unsigned long long)inline_ancestry_queries, (unsigned long long)inline_abstract_markers,
           (unsigned long long)inline_concrete_markers);
    CHECK(locality_nonlocal && locality_local);
    printf("PASS native logical locality: %llu nonlocal, %llu local, %llu unsupported\n",
           (unsigned long long)locality_nonlocal, (unsigned long long)locality_local,
           (unsigned long long)locality_unsupported);
    CHECK(parameter_queries && parameter_formals);
    printf("PASS native direct parameters: %llu queries, %llu formal records, %llu unspecified markers\n",
           (unsigned long long)parameter_queries, (unsigned long long)parameter_formals,
           (unsigned long long)parameter_markers);
    CHECK(qualified_ready);
    printf("PASS native qualified recipes: %llu ready, %llu unnamed, %llu unavailable profiles\n",
           (unsigned long long)qualified_ready, (unsigned long long)qualified_unnamed,
           (unsigned long long)qualified_unavailable);
    CHECK(tb_symbol_storage_remove(storage));
    tb_symbol_storage_destroy(storage);
    CHECK(tb_symbol_admission_destroy(ledger) && !budget.used);
    CHECK(!rmdir(directory));
    return 0;
}
