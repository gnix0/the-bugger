#include "../support/check.h"
#include <elf.h>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static uint32_t word(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8U | (uint32_t)p[2] << 16U | (uint32_t)p[3] << 24U;
}
static void inspect(const char *path)
{
    int fd = open(path, O_RDONLY); CHECK(fd >= 0);
    Elf *elf = elf_begin(fd, ELF_C_READ, NULL); CHECK(elf);
    GElf_Ehdr header; CHECK(gelf_getehdr(elf, &header));
    CHECK(gelf_getclass(elf) == ELFCLASS32 && header.e_machine == EM_ARM);
    CHECK(header.e_type == ET_EXEC && header.e_ident[EI_DATA] == ELFDATA2LSB);
    CHECK((header.e_entry & 1U) && header.e_entry >= 0x08000000 && header.e_entry < 0x08080000);
    size_t names; CHECK(elf_getshdrstrndx(elf, &names) == 0);
    bool vectors = false, data = false, bss = false, debug = false, lines = false, checkpoint = false;
    const char *const required[] = {"fixture_flash", "fixture_buffer", "fixture_counter", "fixture_sample", "fixture_pointer"};
    unsigned found = 0;
    Elf_Scn *section = NULL;
    while ((section = elf_nextscn(elf, section))) {
        GElf_Shdr sh; CHECK(gelf_getshdr(section, &sh));
        const char *name = elf_strptr(elf, names, sh.sh_name); CHECK(name);
        if (!strcmp(name, ".vectors")) {
            Elf_Data *bytes = elf_getdata(section, NULL); CHECK(bytes && bytes->d_buf && bytes->d_size >= 448);
            const unsigned char *v = bytes->d_buf;
            CHECK(sh.sh_addr == 0x08000000 && word(v) == 0x20020000 && word(v + 4) == header.e_entry);
            for (size_t i = 2; i < 112; ++i) { CHECK((word(v + i * 4) & 1U) && word(v + i * 4) == word(v + 8)); }
            vectors = true;
        }
        if (!strcmp(name, ".data")) { CHECK(sh.sh_type == SHT_PROGBITS && sh.sh_size > 64); data = true; }
        if (!strcmp(name, ".bss")) { CHECK(sh.sh_type == SHT_NOBITS && sh.sh_size >= 8); bss = true; }
        const uint64_t writable = (uint64_t)SHF_ALLOC | (uint64_t)SHF_WRITE;
        if ((sh.sh_flags & writable) == writable) {
            CHECK(sh.sh_addr >= 0x20000000 && sh.sh_addr + sh.sh_size <= 0x2001f000);
        }
        if (!strcmp(name, ".debug_info")) { CHECK(sh.sh_size > 0); debug = true; }
        if (!strcmp(name, ".debug_line")) { CHECK(sh.sh_size > 0); lines = true; }
        if (sh.sh_type == SHT_SYMTAB) {
            Elf_Data *symbols = elf_getdata(section, NULL); CHECK(symbols && sh.sh_entsize);
            for (size_t i = 0; i < sh.sh_size / sh.sh_entsize; ++i) {
                GElf_Sym symbol; CHECK(gelf_getsym(symbols, (int)i, &symbol));
                const char *label = elf_strptr(elf, sh.sh_link, symbol.st_name); CHECK(label);
                if (!strcmp(label, "fixture_checkpoint")) {
                    CHECK(GELF_ST_TYPE(symbol.st_info) == STT_FUNC && symbol.st_size > 0); checkpoint = true;
                }
                for (size_t j = 0; j < sizeof required / sizeof required[0]; ++j) {
                    if (!strcmp(label, required[j])) { CHECK(symbol.st_shndx != SHN_UNDEF); found |= 1U << j; }
                }
            }
        }
    }
    CHECK(vectors && data && bss && debug && lines && checkpoint && found == 31);
    CHECK(elf_end(elf) == 0 && close(fd) == 0);
}
int main(int argc, char **argv)
{
    CHECK(argc == 3 && elf_version(EV_CURRENT) != EV_NONE);
    inspect(argv[1]); inspect(argv[2]);
    puts("PASS M17 cross-built STM32 ELF/Thumb reset vectors, SRAM initialization sections, stack reserve and DWARF; no hardware execution");
    return 0;
}
