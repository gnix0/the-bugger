#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- Standard POSIX feature selection. */
#include "images.h"

#include <elf.h>
#include <errno.h>
#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

enum { IMAGE_COUNT = 16, IMAGE_SECTIONS = 256, IMAGE_SYMBOLS = 8192,
       IMAGE_SCAN_SYMBOLS = 65536, IMAGE_FILE_BYTES = 64 * 1024 * 1024 };
typedef struct { uint64_t start, end; size_t index; bool code; char name[96]; } Section;
typedef struct { uint64_t start, end; size_t section; TbImageKind kind; bool mapping; } Symbol;
typedef struct {
    char path[4096]; uint64_t bias;
    Section sections[IMAGE_SECTIONS]; size_t section_count;
    Symbol *symbols; size_t symbol_count;
} Image;
struct TbImages { Image entries[IMAGE_COUNT]; size_t count; bool limited; };

TbImages *tb_images_create(void) { return calloc(1, sizeof(TbImages)); }
void tb_images_reset(TbImages *images)
{
    for (size_t i = 0; i < images->count; ++i) { free(images->entries[i].symbols); }
    memset(images, 0, sizeof *images);
}
void tb_images_destroy(TbImages *images) { if (images) { tb_images_reset(images); free(images); } }
bool tb_images_limited(const TbImages *images) { return images->limited; }
// NOLINTNEXTLINE(bugprone-easily-swappable-parameters): standard qsort comparator signature.
static int symbol_order(const void *left, const void *right)
{
    const Symbol *a = left, *b = right;
    if (a->start != b->start) { return a->start < b->start ? -1 : 1; }
    return (int)a->mapping - (int)b->mapping;
}
static bool add_bias(uint64_t value, uint64_t size, uint64_t bias, uint64_t *start, uint64_t *end)
{
    if (value > UINT64_MAX - bias || size > UINT64_MAX - value - bias) { return false; }
    *start = value + bias; *end = *start + size; return true;
}
static const char *bounded_name(Elf *elf, size_t table, size_t offset)
{
    GElf_Shdr sh; Elf_Scn *scn = elf_getscn(elf, table);
    if (!scn || !gelf_getshdr(scn, &sh) || sh.sh_type != SHT_STRTAB || offset >= sh.sh_size) { return NULL; }
    const char *name = elf_strptr(elf, table, offset);
    size_t available = sh.sh_size - offset > 96 ? 96 : (size_t)(sh.sh_size - offset);
    return name && memchr(name, 0, available) ? name : NULL;
}
bool tb_images_load(TbImages *images, const char *path, uint64_t bias)
{
    if (!path || strlen(path) >= sizeof images->entries[0].path) { return false; }
    for (size_t i = 0; i < images->count; ++i) {
        if (images->entries[i].bias == bias && strcmp(images->entries[i].path, path) == 0) { return true; }
    }
    if (images->count == IMAGE_COUNT) { images->limited = true; return false; }
    if (elf_version(EV_CURRENT) == EV_NONE) { return false; }
    int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK); struct stat info;
    if (fd < 0) { return false; }
    if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size <= 0 || info.st_size > IMAGE_FILE_BYTES) {
        (void)close(fd); return false;
    }
    Elf *elf = elf_begin(fd, ELF_C_READ, NULL); GElf_Ehdr header;
    Image *image = &images->entries[images->count]; memset(image, 0, sizeof *image);
    bool okay = elf && gelf_getehdr(elf, &header) && (header.e_type == ET_EXEC || header.e_type == ET_DYN);
    size_t count = 0, names = 0;
    if (okay) { okay = elf_getshdrnum(elf, &count) == 0 && count <= 8192 && elf_getshdrstrndx(elf, &names) == 0; }
    if (okay) { image->symbols = calloc(IMAGE_SYMBOLS, sizeof *image->symbols); okay = image->symbols != NULL; }
    for (size_t i = 0; okay && i < count; ++i) {
        GElf_Shdr section; Elf_Scn *scn = elf_getscn(elf, i);
        if (!scn || !gelf_getshdr(scn, &section)) { okay = false; break; }
        if (!(section.sh_flags & SHF_ALLOC) || !section.sh_size) { continue; }
        if (image->section_count == IMAGE_SECTIONS) { images->limited = true; break; }
        Section *out = &image->sections[image->section_count];
        if (!add_bias(section.sh_addr, section.sh_size, bias, &out->start, &out->end)) { okay = false; break; }
        out->index = i; out->code = (section.sh_flags & SHF_EXECINSTR) != 0;
        const char *name = bounded_name(elf, names, section.sh_name);
        (void)snprintf(out->name, sizeof out->name, "%s", name ? name : "ELF region");
        ++image->section_count;
    }
    size_t scanned = 0;
    for (size_t i = 0; okay && i < count; ++i) {
        GElf_Shdr section; Elf_Scn *scn = elf_getscn(elf, i);
        if (!scn || !gelf_getshdr(scn, &section)) { okay = false; break; }
        if (section.sh_type != SHT_SYMTAB && section.sh_type != SHT_DYNSYM) { continue; }
        if (!section.sh_entsize || section.sh_size / section.sh_entsize > IMAGE_SCAN_SYMBOLS - scanned) {
            images->limited = true; continue;
        }
        Elf_Data *data = elf_getdata(scn, NULL);
        size_t symbols = (size_t)(section.sh_size / section.sh_entsize); scanned += symbols;
        for (size_t j = 0; j < symbols; ++j) {
            GElf_Sym sym;
            if (!data || !gelf_getsym(data, (int)j, &sym)) { okay = false; break; }
            unsigned type = GELF_ST_TYPE(sym.st_info);
            const char *name = bounded_name(elf, section.sh_link, sym.st_name);
            bool mapping = header.e_machine == EM_ARM && name && name[0] == '$' &&
                           (name[1] == 'a' || name[1] == 't' || name[1] == 'd') && (name[2] == 0 || name[2] == '.');
            if (sym.st_shndx == SHN_UNDEF || sym.st_shndx >= SHN_LORESERVE ||
                (!mapping && type != STT_FUNC && type != STT_OBJECT)) { continue; }
            if (image->symbol_count == IMAGE_SYMBOLS) { images->limited = true; break; }
            Symbol *out = &image->symbols[image->symbol_count];
            uint64_t value = sym.st_value;
            if (header.e_machine == EM_ARM && type == STT_FUNC) { value &= ~UINT64_C(1); }
            if (!add_bias(value, sym.st_size, bias, &out->start, &out->end)) { okay = false; break; }
            out->section = sym.st_shndx; out->mapping = mapping;
            out->kind = mapping ? name[1] == 'd' ? TB_IMAGE_DATA : TB_IMAGE_CODE : type == STT_FUNC ? TB_IMAGE_CODE : TB_IMAGE_DATA;
            ++image->symbol_count;
        }
    }
    if (elf) { (void)elf_end(elf); } (void)close(fd);
    if (!okay || !image->section_count) { free(image->symbols); memset(image, 0, sizeof *image); return false; }
    qsort(image->symbols, image->symbol_count, sizeof *image->symbols, symbol_order);
    image->bias = bias; (void)snprintf(image->path, sizeof image->path, "%s", path); ++images->count; return true;
}
bool tb_images_lookup(const TbImages *images, uint64_t address, TbImageRange *range)
{
    memset(range, 0, sizeof *range);
    for (size_t n = 0; n < images->count; ++n) {
        const Image *image = &images->entries[n];
        for (size_t i = 0; i < image->section_count; ++i) {
            const Section *s = &image->sections[i];
            if (address < s->start || address >= s->end) { continue; }
            *range = (TbImageRange){.start = s->start, .end = s->end, .anchor = s->start,
                                   .kind = s->code ? TB_IMAGE_CODE : TB_IMAGE_DATA};
            (void)snprintf(range->name, sizeof range->name, "%s", s->name);
            for (size_t j = 0; j < image->symbol_count; ++j) {
                const Symbol *sym = &image->symbols[j];
                if (sym->section != s->index || sym->start < s->start || sym->start >= s->end) { continue; }
                if (sym->mapping) {
                    if (sym->start <= address) { range->start = range->anchor = sym->start; range->kind = sym->kind; }
                    else { if (sym->start < range->end) { range->end = sym->start; } break; }
                } else if (sym->start <= address && sym->kind == TB_IMAGE_CODE && sym->start >= range->anchor) { range->anchor = sym->start; }
                else if (sym->kind == TB_IMAGE_DATA && sym->start > address && sym->start < range->end) { range->end = sym->start; }
                else if (sym->kind == TB_IMAGE_DATA && sym->start <= address && address < sym->end) {
                    range->start = range->anchor = sym->start; range->end = sym->end < range->end ? sym->end : range->end;
                    range->kind = TB_IMAGE_DATA;
                }
            }
            return true;
        }
    }
    return false;
}
bool tb_images_neighbor(const TbImages *images, uint64_t address, int direction, TbImageRange *range)
{
    bool found = false; uint64_t best = direction < 0 ? 0 : UINT64_MAX;
    for (size_t n = 0; n < images->count; ++n) {
        const Image *image = &images->entries[n];
        for (size_t i = 0; i < image->section_count; ++i) {
            const Section *s = &image->sections[i]; if (!s->code) { continue; }
            uint64_t candidate = s->start;
            if ((direction < 0 ? candidate < address && (!found || candidate > best) : candidate >= address && (!found || candidate < best))) {
                TbImageRange r;
                if (tb_images_lookup(images, candidate, &r) && r.kind == TB_IMAGE_CODE) { best = candidate; *range = r; found = true; }
            }
        }
        for (size_t j = 0; j < image->symbol_count; ++j) {
            const Symbol *s = &image->symbols[j]; if (s->kind != TB_IMAGE_CODE) { continue; }
            uint64_t candidate = s->start;
            if ((direction < 0 ? candidate < address && (!found || candidate > best) : candidate >= address && (!found || candidate < best))) {
                TbImageRange r;
                if (tb_images_lookup(images, candidate, &r) && r.kind == TB_IMAGE_CODE) { best = candidate; r.anchor = candidate; *range = r; found = true; }
            }
        }
    }
    return found;
}
static bool map_number(char **cursor, const char *limit, uint64_t *out, char separator)
{
    uint64_t value = 0; unsigned digits = 0;
    while (*cursor < limit && **cursor != separator) {
        unsigned char c = (unsigned char)**cursor; unsigned digit;
        if (c >= '0' && c <= '9') { digit = c - '0'; }
        else if (c >= 'a' && c <= 'f') { digit = c - 'a' + 10; }
        else { return false; }
        if (++digits > 16) { return false; }
        value = value * 16 + digit; ++*cursor;
    }
    if (!digits || *cursor >= limit) { return false; }
    *out = value; ++*cursor; return true;
}
void tb_images_native(TbImages *images, pid_t pid, const char *executable, uint64_t pc)
{
    if (pid <= 1) { return; }
    char maps[64]; (void)snprintf(maps, sizeof maps, "/proc/%ld/maps", (long)pid);
    FILE *file = fopen(maps, "re"); if (!file) { return; }
    char line[8192]; unsigned lines = 0;
    while (fgets(line, sizeof line, file) && ++lines <= 4096) {
        if (!strchr(line, '\n')) { continue; }
        uint64_t low, high, offset; char *cursor = line; const char *limit = line + strlen(line);
        if (!map_number(&cursor, limit, &low, '-') || !map_number(&cursor, limit, &high, ' ')) { continue; }
        cursor += strspn(cursor, " ");
        if (strnlen(cursor, 5) < 5 || cursor[4] != ' ') { continue; }
        bool executable_mapping = cursor[2] == 'x'; cursor += 5;
        cursor += strspn(cursor, " "); if (!map_number(&cursor, limit, &offset, ' ')) { continue; }
        for (int token = 0; token < 2; ++token) {
            cursor += strspn(cursor, " "); cursor += strcspn(cursor, " \n");
        }
        cursor += strspn(cursor, " "); char *path = cursor; path[strcspn(path, "\n")] = 0;
        if (*path != '/' || !executable_mapping || pc < low || pc >= high) { continue; }
        /* procfs escapes newline/backslash names; do not guess the resulting path. */
        if (strchr(path, '\\')) { continue; }
        int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) { continue; }
        struct stat info;
        if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size <= 0 || info.st_size > IMAGE_FILE_BYTES) { (void)close(fd); continue; }
        Elf *elf = elf_version(EV_CURRENT) == EV_NONE ? NULL : elf_begin(fd, ELF_C_READ, NULL);
        GElf_Ehdr header; size_t phnum = 0; uint64_t bias = 0; bool mapped = false;
        if (elf && gelf_getehdr(elf, &header) && elf_getphdrnum(elf, &phnum) == 0 && phnum <= 1024) {
            long page = sysconf(_SC_PAGESIZE); uint64_t size = page > 0 ? (uint64_t)page : 4096;
            for (size_t i = 0; i < phnum; ++i) {
                GElf_Phdr p;
                if (gelf_getphdr(elf, (int)i, &p) && p.p_type == PT_LOAD && p.p_offset / size * size == offset &&
                    low >= p.p_vaddr / size * size) {
                    bias = low - p.p_vaddr / size * size; mapped = true; break;
                }
            }
        }
        if (elf) { (void)elf_end(elf); } (void)close(fd);
        if (mapped) { (void)tb_images_load(images, path, bias); }
        break;
    }
    (void)fclose(file); (void)executable;
}
