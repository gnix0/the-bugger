#define _POSIX_C_SOURCE 200809L
#include "images.h"
#include "../support/check.h"
#include <elf.h>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <string.h>
#include <unistd.h>

static Elf_Scn *section(Elf *elf, unsigned type, unsigned flags, uint64_t address,
                        void *bytes, size_t size, Elf_Type encoding)
{
    Elf_Scn *scn = elf_newscn(elf); CHECK(scn); GElf_Shdr sh = {0};
    sh.sh_type = type; sh.sh_flags = flags; sh.sh_addr = address; sh.sh_addralign = 1;
    CHECK(gelf_update_shdr(scn, &sh)); Elf_Data *data = elf_newdata(scn); CHECK(data);
    data->d_buf = bytes; data->d_size = size; data->d_type = encoding; data->d_version = EV_CURRENT;
    return scn;
}
static void fixture(const char *path, int bits, int order)
{
    int fd = open(path, O_RDWR | O_TRUNC); CHECK(fd >= 0); CHECK(elf_version(EV_CURRENT) != EV_NONE);
    Elf *elf = elf_begin(fd, ELF_C_WRITE, NULL); CHECK(elf && gelf_newehdr(elf, bits)); GElf_Ehdr h;
    CHECK(gelf_getehdr(elf, &h)); h.e_ident[EI_DATA] = (unsigned char)order; h.e_type = ET_EXEC;
    h.e_machine = bits == ELFCLASS32 ? EM_ARM : EM_X86_64; h.e_version = EV_CURRENT; h.e_entry = 0x1000;
    CHECK(gelf_update_ehdr(elf, &h));
    char text[128] = {0}, data[64] = {0};
    Elf_Scn *code = section(elf, SHT_PROGBITS, SHF_ALLOC | SHF_EXECINSTR, 0x1000, text, sizeof text, ELF_T_BYTE);
    (void)section(elf, SHT_PROGBITS, SHF_ALLOC | SHF_WRITE, 0x2000, data, sizeof data, ELF_T_BYTE);
    char strings[] = "\0fn\0$d\0$t\0zero\0";
    Elf_Scn *str = section(elf, SHT_STRTAB, 0, 0, strings, sizeof strings, ELF_T_BYTE);
    size_t entry_size = gelf_fsize(elf, ELF_T_SYM, 1, EV_CURRENT); CHECK(entry_size);
    unsigned char symbols[6 * sizeof(Elf64_Sym)] = {0};
    Elf_Scn *table = section(elf, SHT_SYMTAB, 0, 0, symbols, 6 * entry_size, ELF_T_SYM);
    GElf_Shdr sh; CHECK(gelf_getshdr(table, &sh)); sh.sh_entsize = entry_size;
    sh.sh_link = (Elf64_Word)elf_ndxscn(str); sh.sh_info = 6; CHECK(gelf_update_shdr(table, &sh));
    for (int i = 1; i < 6; ++i) {
        GElf_Sym sym = {0}; sym.st_shndx = (Elf64_Section)elf_ndxscn(code);
        sym.st_name = i == 1 ? 1 : i == 2 ? 7 : i == 3 ? 4 : i == 4 ? 7 : 10;
        sym.st_value = i == 1 || i == 2 ? 0x1000 : i == 3 ? 0x1040 : i == 4 ? 0x1050 : 0x1020;
        sym.st_size = i == 1 ? 128 : i == 5 ? 4 : 0;
        sym.st_info = GELF_ST_INFO(STB_LOCAL, i == 1 ? STT_FUNC : i == 5 ? STT_OBJECT : STT_NOTYPE);
        CHECK(gelf_update_sym(elf_getdata(table, NULL), i, &sym));
    }
    CHECK(elf_update(elf, ELF_C_WRITE) > 0); CHECK(elf_end(elf) == 0 && close(fd) == 0);
}
static void exercise(const char *path, int bits, int order)
{
    fixture(path, bits, order); TbImages *images = tb_images_create(); CHECK(images);
    CHECK(tb_images_load(images, path, 0x100000)); TbImageRange range;
    CHECK(tb_images_lookup(images, 0x101010, &range) && range.kind == TB_IMAGE_CODE && range.anchor == 0x101000);
    CHECK(range.end == 0x101020); /* embedded object must end the code read */
    CHECK(tb_images_lookup(images, 0x101020, &range) && range.kind == TB_IMAGE_DATA);
    CHECK(tb_images_lookup(images, 0x102000, &range) && range.kind == TB_IMAGE_DATA);
    CHECK(!tb_images_lookup(images, 0x102040, &range));
    if (bits == ELFCLASS32) {
        CHECK(tb_images_lookup(images, 0x101045, &range) && range.kind == TB_IMAGE_DATA && range.end == 0x101050);
        CHECK(tb_images_lookup(images, 0x101050, &range) && range.kind == TB_IMAGE_CODE);
    }
    CHECK(tb_images_neighbor(images, 0x101010, -1, &range) && range.anchor == 0x101000);
    CHECK(!tb_images_load(images, path, UINT64_MAX - 10));
    tb_images_reset(images); CHECK(!tb_images_lookup(images, 0x101010, &range)); tb_images_destroy(images);
}
int main(void)
{
    char path[] = "/tmp/the-bugger-images-XXXXXX"; int fd = mkstemp(path); CHECK(fd >= 0 && close(fd) == 0);
    exercise(path, ELFCLASS64, ELFDATA2LSB); exercise(path, ELFCLASS64, ELFDATA2MSB);
    exercise(path, ELFCLASS32, ELFDATA2LSB); exercise(path, ELFCLASS32, ELFDATA2MSB);
    fixture(path, ELFCLASS64, ELFDATA2LSB); unsigned char original[8192];
    fd = open(path, O_RDONLY); CHECK(fd >= 0); ssize_t length = read(fd, original, sizeof original); CHECK(length > 0 && close(fd) == 0);
    uint32_t random = 0xC0DE;
    for (unsigned iteration = 0; iteration < 1000; ++iteration) {
        unsigned char bytes[8192]; memcpy(bytes, original, (size_t)length);
        random = random * 1664525U + 1013904223U; size_t at = random % (size_t)length;
        bytes[at] ^= (unsigned char)(random >> 24);
        size_t size = iteration % 3 == 0 ? iteration % (size_t)length : (size_t)length;
        fd = open(path, O_WRONLY | O_TRUNC); CHECK(fd >= 0 && write(fd, bytes, size) == (ssize_t)size && close(fd) == 0);
        TbImages *images = tb_images_create(); CHECK(images);
        if (tb_images_load(images, path, 0)) {
            TbImageRange range;
            if (tb_images_lookup(images, 0x1010, &range)) { CHECK(range.start <= 0x1010 && range.end > 0x1010 && range.anchor <= 0x1010); }
        }
        tb_images_destroy(images);
    }
    CHECK(unlink(path) == 0);
    puts("PASS M08 ELF32/64, both byte orders, code/data/ARM mappings, relocation overflow, reset and 1000 malformed/truncated metadata mutations");
    return EXIT_SUCCESS;
}
