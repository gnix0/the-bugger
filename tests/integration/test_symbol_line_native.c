#define _POSIX_C_SOURCE 200809L
#include "symbol/line.h"
#include "../support/check.h"

#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static Elf_Data *section(Elf *elf, const char *name)
{
    size_t strings;
    CHECK(elf_getshdrstrndx(elf, &strings) == 0);
    Elf_Data *found = NULL;
    Elf_Scn *current = NULL;
    while ((current = elf_nextscn(elf, current))) {
        GElf_Shdr header;
        CHECK(gelf_getshdr(current, &header));
        const char *text = elf_strptr(elf, strings, header.sh_name);
        CHECK(text);
        if (strcmp(text, name))
            continue;
        CHECK(!found && !(header.sh_flags & SHF_COMPRESSED));
        found = elf_getdata(current, NULL);
        CHECK(found && found->d_off == 0 && found->d_size == header.sh_size);
        CHECK(!elf_getdata(current, found));
    }

    CHECK(found && found->d_buf && found->d_size);
    return found;
}


static void check_file(const char *path, unsigned expected_version)
{
    int descriptor = open(path, O_RDONLY | O_CLOEXEC);
    CHECK(descriptor >= 0);
    Elf *elf = elf_begin(descriptor, ELF_C_READ, NULL);
    CHECK(elf);
    GElf_Ehdr container;
    CHECK(gelf_getehdr(elf, &container));
    CHECK(container.e_type == ET_EXEC || container.e_type == ET_DYN);
    CHECK(container.e_ident[EI_DATA] == ELFDATA2LSB || container.e_ident[EI_DATA] == ELFDATA2MSB);
    TbSymbolByteOrder_e order = container.e_ident[EI_DATA] == ELFDATA2LSB ?
                                   TB_SYMBOL_LITTLE_ENDIAN : TB_SYMBOL_BIG_ENDIAN;
    Elf_Data *info = section(elf, ".debug_info");
    TbSymbolDwarfUnit_t unit;
    CHECK(tb_symbol_dwarf_unit(TB_SYMBOL_DWARF_INFO, (TbSymbolDwarfSpan_t) {
        .section_bytes = info->d_size,
        .byte_order    = order
    }, info->d_buf, info->d_size, &unit) == TB_SYMBOL_DWARF_OK);
    CHECK(unit.address_bytes == 4 || unit.address_bytes == 8);

    Elf_Data *data = section(elf, ".debug_line");
    CHECK(data->d_size < 1024 * 1024);
    /* These maintained native fixtures use a uniform CU address width, checked
     * against every CU header below. This is a test profile, not a production
     * rule relating ELF class, target addresses and arbitrary line tables. */
    for (uint64_t offset = 0; offset < info->d_size;) {
        TbSymbolDwarfUnit_t other;
        CHECK(tb_symbol_dwarf_unit(TB_SYMBOL_DWARF_INFO, (TbSymbolDwarfSpan_t) {
            .section_bytes = info->d_size,
            .unit_offset   = offset,
            .byte_order    = order
        }, (const unsigned char *)info->d_buf + offset, info->d_size - (size_t)offset, &other) ==
              TB_SYMBOL_DWARF_OK);
        CHECK(other.address_bytes == unit.address_bytes);
        offset = other.extent.end_offset;
    }

    size_t capacity = data->d_size + 1;
    TbSymbolLineRow_t *rows = calloc(capacity, sizeof(*rows));
    TbSymbolLineFile_t *files = calloc(capacity, sizeof(*files));
    CHECK(rows && files);
    size_t table_count = 0;
    size_t row_count = 0;
    size_t sequence_count = 0;
    bool main_seen = false;
    uint64_t main_address = 0;
    Elf_Data *symbols = section(elf, ".symtab");
    size_t symbol_count = symbols->d_size / gelf_fsize(elf, ELF_T_SYM, 1, EV_CURRENT);
    size_t string_index = 0;
    Elf_Scn *scan = NULL;
    while ((scan = elf_nextscn(elf, scan))) {
        GElf_Shdr header;
        CHECK(gelf_getshdr(scan, &header));
        if (header.sh_type == SHT_SYMTAB)
            string_index = header.sh_link;
    }
    CHECK(string_index);
    for (size_t i = 0; i < symbol_count; ++i) {
        GElf_Sym symbol;
        CHECK(gelf_getsym(symbols, (int)i, &symbol));
        const char *name = elf_strptr(elf, string_index, symbol.st_name);
        CHECK(name);
        if (!strcmp(name, "main")) {
            CHECK(symbol.st_shndx != SHN_UNDEF && symbol.st_value);
            main_address = symbol.st_value;
        }
    }
    CHECK(main_address);

    for (uint64_t offset = 0; offset < data->d_size;) {
        TbSymbolLineHeader_t header;
        CHECK(tb_symbol_line_header((TbSymbolDwarfSpan_t) {
            .section_bytes = data->d_size,
            .unit_offset   = offset,
            .byte_order    = order
        }, (const unsigned char *)data->d_buf + offset, data->d_size - (size_t)offset, &header) ==
              TB_SYMBOL_DWARF_OK);
        CHECK(header.version == expected_version);
        TbSymbolLine_t line;
        CHECK(tb_symbol_line_init(&line, header, unit.address_bytes, rows, capacity, files, capacity) ==
              TB_SYMBOL_LINE_PENDING);
        size_t cursor = (size_t)header.program_offset;
        while (line.status == TB_SYMBOL_LINE_PENDING) {
            size_t consumed;
            (void)tb_symbol_line_feed(&line, (const unsigned char *)data->d_buf + cursor,
                                      data->d_size - cursor, &consumed);
            CHECK(consumed && consumed <= 256);
            cursor += consumed;
        }
        CHECK(line.status == TB_SYMBOL_LINE_COMPLETE && cursor == header.extent.end_offset);
        TbSymbolLineView_t view;
        CHECK(tb_symbol_line_view(&line, &view) && view.row_count);
        CHECK(view.rows[view.row_count - 1].end_sequence);
        for (size_t i = 0; i < view.row_count; ++i) {
            if (view.rows[i].end_sequence)
                ++sequence_count;
            else if (view.rows[i].address == main_address)
                main_seen = true;
        }
        row_count += view.row_count;
        ++table_count;
        tb_symbol_line_discard(&line);
        offset = header.extent.end_offset;
    }
    CHECK(table_count && row_count > table_count && sequence_count && main_seen);
    printf("PASS native line programs v%u: %zu tables, %zu rows, %zu sequences, exact main address\n",
           expected_version, table_count, row_count, sequence_count);

    free(files);
    free(rows);
    CHECK(elf_end(elf) == 0);
    CHECK(close(descriptor) == 0);
}


int main(int argc, char **argv)
{
    CHECK(argc == 3 && elf_version(EV_CURRENT) != EV_NONE);
    check_file(argv[1], 4);
    check_file(argv[2], 5);
    return 0;
}
