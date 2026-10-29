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
#include <sys/sysmacros.h>
#include <unistd.h>

enum { IMAGE_COUNT = 16, IMAGE_SECTIONS = 256, IMAGE_SYMBOLS = 8192,
       IMAGE_SCAN_SYMBOLS = 65536, IMAGE_FILE_BYTES = 64 * 1024 * 1024 };
typedef struct { uint64_t start, end; size_t index; bool code; char name[96]; } Section;
typedef struct { uint64_t start, end; size_t section; TbImageKind kind; bool mapping; } Symbol;
typedef struct {
    char path[4096]; uint64_t bias;
    struct stat identity;
    uint64_t revision;
    TbImageTrust trust;
    bool fresh;
    Section sections[IMAGE_SECTIONS]; size_t section_count;
    Symbol *symbols; size_t symbol_count;
} Image;
struct TbImages { Image entries[IMAGE_COUNT]; size_t count; uint64_t revision; bool limited; };

TbImages *tb_images_create(void) { return calloc(1, sizeof(TbImages)); }
void tb_images_reset(TbImages *images)
{
    for (size_t i = 0; i < images->count; ++i) { free(images->entries[i].symbols); }
    uint64_t revision = images->revision + 1;
    memset(images, 0, sizeof *images); images->revision = revision;
}
void tb_images_destroy(TbImages *images) { if (images) { tb_images_reset(images); free(images); } }
bool tb_images_limited(const TbImages *images) { return images->limited; }
uint64_t tb_images_revision(const TbImages *images) { return images->revision; }
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
static bool same_file(const struct stat *a, const struct stat *b)
{
    return a->st_dev == b->st_dev && a->st_ino == b->st_ino && a->st_size == b->st_size &&
           a->st_mtim.tv_sec == b->st_mtim.tv_sec && a->st_mtim.tv_nsec == b->st_mtim.tv_nsec &&
           a->st_ctim.tv_sec == b->st_ctim.tv_sec && a->st_ctim.tv_nsec == b->st_ctim.tv_nsec;
}
bool tb_images_validate(TbImages *images)
{
    bool changed = false;
    for (size_t i = 0; i < images->count; ++i) {
        Image *image = &images->entries[i]; struct stat info;
        if (image->fresh && (stat(image->path, &info) || !S_ISREG(info.st_mode) || !same_file(&image->identity, &info))) {
            image->fresh = false; image->trust = TB_IMAGE_UNVERIFIED;
            image->revision = ++images->revision; changed = true;
        }
    }
    return changed;
}
bool tb_images_unverify(TbImages *images)
{
    bool revoked = false;
    for (size_t i = 0; i < images->count; ++i) {
        revoked = revoked || images->entries[i].trust != TB_IMAGE_UNVERIFIED;
        images->entries[i].trust = TB_IMAGE_UNVERIFIED;
        images->entries[i].revision = ++images->revision;
    }
    return revoked;
}
bool tb_images_load(TbImages *images, const char *path, uint64_t bias)
{
    if (!path || strlen(path) >= sizeof images->entries[0].path) { return false; }
    (void)tb_images_validate(images);
    for (size_t i = 0; i < images->count; ++i) {
        if (images->entries[i].bias == bias && strcmp(images->entries[i].path, path) == 0) {
            if (images->entries[i].fresh) { return true; }
            free(images->entries[i].symbols);
            memmove(images->entries + i, images->entries + i + 1, (images->count - i - 1) * sizeof images->entries[0]);
            --images->count; break;
        }
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
    struct stat after;
    if (okay && (fstat(fd, &after) || !same_file(&info, &after))) { okay = false; }
    if (elf) { (void)elf_end(elf); } (void)close(fd);
    if (!okay || !image->section_count) { free(image->symbols); memset(image, 0, sizeof *image); return false; }
    qsort(image->symbols, image->symbol_count, sizeof *image->symbols, symbol_order);
    image->bias = bias; image->identity = info; image->fresh = true; image->revision = ++images->revision;
    (void)snprintf(image->path, sizeof image->path, "%s", path); ++images->count; return true;
}
static const Image *review_image(const TbImages *images, const TbImageReview *review)
{
    if (!review || !memchr(review->path, 0, sizeof review->path)) { return NULL; }
    for (size_t i = 0; i < images->count; ++i) {
        const Image *image = &images->entries[i];
        if (image->fresh && image->revision == review->revision && image->bias == review->bias &&
            strcmp(image->path, review->path) == 0) { return image; }
    }
    return NULL;
}
bool tb_images_review(TbImages *images, const char *path, uint64_t bias, TbImageReview *review)
{
    if (!review || !tb_images_load(images, path, bias)) { return false; }
    for (size_t i = 0; i < images->count; ++i) {
        const Image *image = &images->entries[i];
        if (image->bias != bias || strcmp(image->path, path) != 0) { continue; }
        *review = (TbImageReview){.bias = bias, .revision = image->revision, .ranges = image->section_count};
        (void)snprintf(review->path, sizeof review->path, "%s", image->path); return true;
    }
    return false;
}
bool tb_images_review_range(const TbImages *images, const TbImageReview *review, size_t index, TbImageRange *range)
{
    const Image *image = review_image(images, review);
    if (!range || !image || index >= image->section_count) { return false; }
    const Section *section = &image->sections[index];
    *range = (TbImageRange){.start = section->start, .end = section->end, .anchor = section->start,
        .kind = section->code ? TB_IMAGE_CODE : TB_IMAGE_DATA, .trust = image->trust};
    (void)snprintf(range->name, sizeof range->name, "%s", section->name); return true;
}
bool tb_images_confirm(TbImages *images, const TbImageReview *review, TbImageTrust trust)
{
    if (trust != TB_IMAGE_USER_CONFIRMED && trust != TB_IMAGE_BACKEND_VERIFIED) { return false; }
    (void)tb_images_validate(images);
    const Image *image = review_image(images, review); if (!image) { return false; }
    if (trust == TB_IMAGE_USER_CONFIRMED) {
        for (size_t i = 0; i < images->count; ++i) {
            Image *other = &images->entries[i];
            if (other != image && other->trust == TB_IMAGE_USER_CONFIRMED && strcmp(other->path, image->path) == 0) {
                other->trust = TB_IMAGE_UNVERIFIED; other->revision = ++images->revision;
            }
        }
    }
    images->entries[(size_t)(image - images->entries)].trust = trust; return true;
}
static bool lookup(const TbImages *images, uint64_t address, TbImageRange *range, bool trusted)
{
    memset(range, 0, sizeof *range);
    for (size_t n = 0; n < images->count; ++n) {
        const Image *image = &images->entries[n];
        if (trusted && (!image->fresh || image->trust == TB_IMAGE_UNVERIFIED)) { continue; }
        for (size_t i = 0; i < image->section_count; ++i) {
            const Section *s = &image->sections[i];
            if (address < s->start || address >= s->end) { continue; }
            *range = (TbImageRange){.start = s->start, .end = s->end, .anchor = s->start,
                                   .kind = s->code ? TB_IMAGE_CODE : TB_IMAGE_DATA,
                                   .trust = image->trust};
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
bool tb_images_lookup(const TbImages *images, uint64_t address, TbImageRange *range)
{ return lookup(images, address, range, false); }
bool tb_images_trusted_lookup(const TbImages *images, uint64_t address, TbImageRange *range)
{ return lookup(images, address, range, true); }
static bool neighbor(const TbImages *images, uint64_t address, int direction, TbImageRange *range, bool trusted)
{
    bool found = false; uint64_t best = direction < 0 ? 0 : UINT64_MAX;
    for (size_t n = 0; n < images->count; ++n) {
        const Image *image = &images->entries[n];
        if (trusted && (!image->fresh || image->trust == TB_IMAGE_UNVERIFIED)) { continue; }
        for (size_t i = 0; i < image->section_count; ++i) {
            const Section *s = &image->sections[i]; if (!s->code) { continue; }
            uint64_t candidate = s->start;
            if ((direction < 0 ? candidate < address && (!found || candidate > best) : candidate >= address && (!found || candidate < best))) {
                TbImageRange r;
                if (lookup(images, candidate, &r, trusted) && r.kind == TB_IMAGE_CODE && (!trusted || r.trust != TB_IMAGE_UNVERIFIED)) { best = candidate; *range = r; found = true; }
            }
        }
        for (size_t j = 0; j < image->symbol_count; ++j) {
            const Symbol *s = &image->symbols[j]; if (s->kind != TB_IMAGE_CODE) { continue; }
            uint64_t candidate = s->start;
            if ((direction < 0 ? candidate < address && (!found || candidate > best) : candidate >= address && (!found || candidate < best))) {
                TbImageRange r;
                if (lookup(images, candidate, &r, trusted) && r.kind == TB_IMAGE_CODE && (!trusted || r.trust != TB_IMAGE_UNVERIFIED)) { best = candidate; r.anchor = candidate; *range = r; found = true; }
            }
        }
    }
    return found;
}
bool tb_images_neighbor(const TbImages *images, uint64_t address, int direction, TbImageRange *range)
{ return neighbor(images, address, direction, range, false); }
bool tb_images_trusted_neighbor(const TbImages *images, uint64_t address, int direction, TbImageRange *range)
{ return neighbor(images, address, direction, range, true); }
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
static bool native_mapping(TbImages *images, pid_t pid, uint64_t pc, TbSetupMapping *mapping)
{
    if (pid <= 1) { return false; }
    bool found = false;
    char maps[64]; (void)snprintf(maps, sizeof maps, "/proc/%ld/maps", (long)pid);
    FILE *file = fopen(maps, "re"); if (!file) { return false; }
    char line[8192]; unsigned lines = 0;
    while (fgets(line, sizeof line, file) && ++lines <= 4096) {
        if (!strchr(line, '\n')) { continue; }
        uint64_t low, high, offset, device_major, device_minor; char *cursor = line; const char *limit = line + strlen(line);
        if (!map_number(&cursor, limit, &low, '-') || !map_number(&cursor, limit, &high, ' ')) { continue; }
        cursor += strspn(cursor, " ");
        if (strnlen(cursor, 5) < 5 || cursor[4] != ' ') { continue; }
        bool executable_mapping = cursor[2] == 'x'; cursor += 5;
        cursor += strspn(cursor, " "); if (!map_number(&cursor, limit, &offset, ' ')) { continue; }
        cursor += strspn(cursor, " ");
        if (!map_number(&cursor, limit, &device_major, ':') || !map_number(&cursor, limit, &device_minor, ' ')) { continue; }
        cursor += strspn(cursor, " ");
        errno = 0; char *after_inode = NULL;
        if (*cursor < '0' || *cursor > '9') { continue; }
        unsigned long long inode = strtoull(cursor, &after_inode, 10);
        if (errno || after_inode == cursor || *after_inode != ' ') { continue; } cursor = after_inode;
        cursor += strspn(cursor, " "); char *path = cursor; path[strcspn(path, "\n")] = 0;
        if (*path != '/' || !executable_mapping || pc < low || pc >= high) { continue; }
        /* procfs escapes newline/backslash names; do not guess the resulting path. */
        if (strchr(path, '\\')) { continue; }
        int fd = open(path, O_RDONLY | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) { continue; }
        struct stat info;
        if (fstat(fd, &info) || !S_ISREG(info.st_mode) || info.st_size <= 0 || info.st_size > IMAGE_FILE_BYTES ||
            device_major != major(info.st_dev) || device_minor != minor(info.st_dev) || inode != info.st_ino) { (void)close(fd); continue; }
        Elf *elf = elf_version(EV_CURRENT) == EV_NONE ? NULL : elf_begin(fd, ELF_C_READ, NULL);
        GElf_Ehdr header; size_t phnum = 0; uint64_t bias = 0; bool mapped = false;
        if (elf && gelf_getehdr(elf, &header) && elf_getphdrnum(elf, &phnum) == 0 && phnum <= 1024) {
            long page = sysconf(_SC_PAGESIZE); uint64_t size = page > 0 ? (uint64_t)page : 4096;
            for (size_t i = 0; i < phnum; ++i) {
                GElf_Phdr p;
                if (gelf_getphdr(elf, (int)i, &p) && p.p_type == PT_LOAD && (p.p_flags & PF_X) && p.p_offset / size * size == offset &&
                    low >= p.p_vaddr / size * size) {
                    bias = low - p.p_vaddr / size * size; mapped = true; break;
                }
            }
        }
        if (elf) { (void)elf_end(elf); } (void)close(fd);
        if (mapped && strlen(path) < TB_SETUP_PATH) {
            struct stat current;
            if (stat(path, &current) == 0 && same_file(&info, &current)) {
                found = true;
                if (mapping) {
                    *mapping = (TbSetupMapping){.bias = bias, .begin = low, .end = high};
                    (void)snprintf(mapping->path, sizeof mapping->path, "%s", path);
                    (void)snprintf(mapping->identity, sizeof mapping->identity, "%llu:%llu:%lld:%lld:%ld:%lld:%ld",
                        (unsigned long long)info.st_dev, (unsigned long long)info.st_ino, (long long)info.st_size,
                        (long long)info.st_mtim.tv_sec, info.st_mtim.tv_nsec, (long long)info.st_ctim.tv_sec, info.st_ctim.tv_nsec);
                }
                if (images) { TbImageReview review;
                    if (tb_images_review(images, path, bias, &review)) { (void)tb_images_confirm(images, &review, TB_IMAGE_BACKEND_VERIFIED); }
                }
            }
        }
        break;
    }
    (void)fclose(file); return found;
}
void tb_images_native(TbImages *images, pid_t pid, const char *executable, uint64_t pc)
{ (void)executable; (void)native_mapping(images, pid, pc, NULL); }
bool tb_images_native_mapping(pid_t pid, uint64_t address, TbSetupMapping *mapping)
{ return mapping && native_mapping(NULL, pid, address, mapping); }
bool tb_images_mapping_equal(const TbSetupMapping *a, const TbSetupMapping *b, uint64_t address)
{
    return a && b && *a->path && *a->identity && !strcmp(a->path, b->path) && !strcmp(a->identity, b->identity) &&
        a->bias == b->bias && a->begin == b->begin && a->end == b->end && address >= a->begin && address < a->end;
}
