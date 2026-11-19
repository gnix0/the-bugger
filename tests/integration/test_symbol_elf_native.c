#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- Native file fixture reads. */
#include "symbol/elf.h"
#include "../support/check.h"

#include <errno.h>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <limits.h>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static void read_record(int fd, uint64_t offset, size_t length, unsigned char *bytes)
{
    CHECK(length <= 64 && offset <= (uint64_t)INT64_MAX - length);
    size_t received = 0;
    while (received < length) {
        ssize_t count = pread(fd, bytes + received, length - received, (off_t)(offset + received));
        if (count < 0 && errno == EINTR)
            continue;
        CHECK(count > 0);
        received += (size_t)count;
    }
}


static void compare_section(TbSymbolElfSection_t actual, GElf_Shdr expected)
{
    CHECK(actual.name_offset == expected.sh_name && actual.type == expected.sh_type);
    CHECK(actual.flags == expected.sh_flags && actual.address == expected.sh_addr);
    CHECK(actual.offset == expected.sh_offset && actual.bytes == expected.sh_size);
    CHECK(actual.link == expected.sh_link && actual.info == expected.sh_info);
    CHECK(actual.alignment == expected.sh_addralign && actual.entry_bytes == expected.sh_entsize);
}


static void check_file(const char *path)
{
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    CHECK(fd >= 0);
    struct stat info;
    CHECK(!fstat(fd, &info) && info.st_size >= 64);
    unsigned char bytes[64];
    read_record(fd, 0, sizeof(bytes), bytes);
    TbSymbolElfHeader_t header;
    CHECK(tb_symbol_elf_header((uint64_t)info.st_size, bytes, sizeof(bytes), &header) == TB_SYMBOL_ELF_OK);
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
    read_record(fd, header.section_offset, header.section_stride, bytes);
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
    size_t symbols = 0;
    for (size_t i = 0; i < sections; ++i) {
        uint64_t offset;
        CHECK(tb_symbol_elf_entry(&layout.sections, i, &offset));
        read_record(fd, offset, (size_t)layout.sections.stride, bytes);
        TbSymbolElfSection_t actual;
        CHECK(tb_symbol_elf_section(&header, bytes, (size_t)layout.sections.stride, &actual) ==
              TB_SYMBOL_ELF_OK);
        Elf_Scn *section = elf_getscn(reference, i);
        GElf_Shdr expected;
        CHECK(section && gelf_getshdr(section, &expected));
        compare_section(actual, expected);
        if (actual.type != SHT_SYMTAB && actual.type != SHT_DYNSYM)
            continue;
        CHECK(actual.entry_bytes == (header.address_bytes == 4 ? 16U : 24U));
        CHECK(!(actual.bytes % actual.entry_bytes));
        TbSymbolElfTable_t table = {
            .offset = actual.offset,
            .count  = actual.bytes / actual.entry_bytes,
            .stride = actual.entry_bytes
        };
        Elf_Data *data = elf_getdata(section, NULL);
        CHECK(data);
        for (uint64_t entry = 0; entry < table.count; ++entry) {
            CHECK(entry <= INT_MAX && tb_symbol_elf_entry(&table, entry, &offset));
            read_record(fd, offset, (size_t)table.stride, bytes);
            TbSymbolElfSymbol_t symbol;
            CHECK(tb_symbol_elf_symbol(&header, bytes, (size_t)table.stride, &symbol) == TB_SYMBOL_ELF_OK);
            GElf_Sym expected_symbol;
            CHECK(gelf_getsym(data, (int)entry, &expected_symbol));
            CHECK(symbol.name_offset == expected_symbol.st_name && symbol.info == expected_symbol.st_info);
            CHECK(symbol.other == expected_symbol.st_other &&
                  symbol.section_index == expected_symbol.st_shndx);
            CHECK(symbol.value == expected_symbol.st_value && symbol.bytes == expected_symbol.st_size);
            ++symbols;
        }
    }
    for (size_t i = 0; i < programs; ++i) {
        uint64_t offset;
        CHECK(i <= INT_MAX && tb_symbol_elf_entry(&layout.programs, i, &offset));
        read_record(fd, offset, (size_t)layout.programs.stride, bytes);
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
    CHECK(symbols && sections && programs);
    CHECK(!elf_end(reference) && !close(fd));
    printf("PASS native ELF record agreement with libelf: %zu sections, %zu programs, %zu symbols\n",
           sections, programs, symbols);
}


int main(int argc, char **argv)
{
    CHECK(argc >= 2 && elf_version(EV_CURRENT) != EV_NONE);
    for (int i = 1; i < argc; ++i)
        check_file(argv[i]);
    return 0;
}
