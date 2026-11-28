#define _POSIX_C_SOURCE 200809L
#include "symbol/companion_metadata.h"
#include "symbol/crc32.h"
#include "symbol/sections.h"
#include "../support/check.h"

#include <fcntl.h>
#include <gelf.h>
#include <libelf.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

typedef struct {
    TbSymbolBudget_t       budget;
    TbSymbolSections_t    *sections;
    TbSymbolSectionsView_t directory;
    Elf                   *elf;
    int                    descriptor;
    char                   path[64];
} Artifact_t;

static void open_artifact(Artifact_t *artifact, const char *path)
{
    *artifact = (Artifact_t) {0};
    (void)snprintf(artifact->path, sizeof(artifact->path), "/tmp/tb-companion-native-XXXXXX");
    int writer = mkstemp(artifact->path);
    int input = open(path, O_RDONLY | O_CLOEXEC);
    CHECK(writer >= 0 && input >= 0);
    unsigned char bytes[4096];
    ssize_t count;
    while ((count = read(input, bytes, sizeof(bytes))) > 0)
        CHECK(write(writer, bytes, (size_t)count) == count);
    CHECK(count == 0 && close(input) == 0 && fchmod(writer, 0400) == 0 && close(writer) == 0);
    artifact->descriptor = open(artifact->path, O_RDONLY | O_CLOEXEC);
    CHECK(artifact->descriptor >= 0);
    struct stat info;
    CHECK(fstat(artifact->descriptor, &info) == 0 && info.st_size > 0);
    CHECK(tb_symbol_budget_init(&artifact->budget, 1024 * 1024));
    artifact->sections = tb_symbol_sections_create((TbSymbolStorageInfo_t) {
        .key = {
            .device = (uint64_t)info.st_dev,
            .inode  = (uint64_t)info.st_ino
        },
        .bytes = (uint64_t)info.st_size
    }, artifact->descriptor, &artifact->budget);
    CHECK(artifact->sections);
    TbSymbolOutcome_e status = TB_SYMBOL_PENDING;
    while (status == TB_SYMBOL_PENDING)
        status = tb_symbol_sections_step(artifact->sections, 257);
    CHECK(status == TB_SYMBOL_SUCCESS && tb_symbol_sections_view(artifact->sections, &artifact->directory));
    artifact->elf = elf_begin(artifact->descriptor, ELF_C_READ, NULL);
    CHECK(artifact->elf);
}


static void close_artifact(Artifact_t *artifact)
{
    CHECK(elf_end(artifact->elf) == 0);
    tb_symbol_sections_destroy(artifact->sections);
    CHECK(artifact->budget.used == 0 && close(artifact->descriptor) == 0 && unlink(artifact->path) == 0);
}


static TbSymbolCompanionScope_t scope(const Artifact_t *artifact, uint64_t section)
{
    return (TbSymbolCompanionScope_t) {
        .backing       = artifact->directory.backing,
        .section_index = section,
        .bytes         = artifact->directory.entries[section].header.bytes,
        .byte_order    = artifact->directory.header->byte_order
    };
}


static uint32_t debuglink(const Artifact_t *artifact, const char *path)
{
    size_t strings;
    CHECK(elf_getshdrstrndx(artifact->elf, &strings) == 0);
    unsigned matches = 0;
    uint32_t crc = 0;
    for (uint64_t i = 0; i < artifact->directory.count; ++i) {
        const TbSymbolElfSection_t *header = &artifact->directory.entries[i].header;
        const char *name = elf_strptr(artifact->elf, strings, header->name_offset);
        CHECK(name);
        if (strcmp(name, ".gnu_debuglink"))
            continue;

        CHECK(header->type == SHT_PROGBITS && !(header->flags & SHF_COMPRESSED));
        Elf_Data *data = elf_getdata(elf_getscn(artifact->elf, (size_t)i), NULL);
        CHECK(data && data->d_size == header->bytes);
        unsigned char basename[4096];
        TbSymbolDebuglink_t link;
        TbSymbolCompanionMetadataStatus_e status = tb_symbol_debuglink_init(&link, scope(artifact, i),
            basename, sizeof(basename));
        size_t cursor = 0;
        while (status == TB_SYMBOL_COMPANION_METADATA_PENDING) {
            size_t consumed;
            size_t count = data->d_size - cursor < 3 ? data->d_size - cursor : 3;
            const unsigned char *input = count ? (unsigned char *)data->d_buf + cursor : NULL;
            status = tb_symbol_debuglink_feed(&link, input, count, &consumed);
            cursor += consumed;
        }
        TbSymbolDebuglinkView_t view;
        CHECK(status == TB_SYMBOL_COMPANION_METADATA_FOUND && tb_symbol_debuglink_view(&link, &view));
        const char *expected = strrchr(path, '/');
        expected = expected ? expected + 1 : path;
        CHECK(!strcmp((const char *)view.name, expected));
        crc = view.crc;
        printf("DEBUGLINK section=%llu name=%s crc=%08x\n", (unsigned long long)i, view.name, crc);
        ++matches;
        tb_symbol_debuglink_discard(&link);
    }
    CHECK(matches == 1);
    return crc;
}


static size_t build_id(const Artifact_t *artifact, unsigned char *identifier, size_t capacity)
{
    size_t length = 0;
    unsigned matches = 0;
    for (uint64_t i = 0; i < artifact->directory.count; ++i) {
        const TbSymbolElfSection_t *header = &artifact->directory.entries[i].header;
        if (header->type != SHT_NOTE)
            continue;

        CHECK(!(header->flags & SHF_COMPRESSED) && (header->alignment == 4 || header->alignment == 8));
        Elf_Data *data = elf_getdata(elf_getscn(artifact->elf, (size_t)i), NULL);
        CHECK(data && data->d_size == header->bytes);
        TbSymbolBuildId_t id;
        TbSymbolCompanionMetadataStatus_e status = tb_symbol_build_id_init(&id, scope(artifact, i),
            (unsigned)header->alignment, identifier, capacity);
        size_t cursor = 0;
        while (status == TB_SYMBOL_COMPANION_METADATA_PENDING) {
            size_t consumed;
            size_t count = data->d_size - cursor < 5 ? data->d_size - cursor : 5;
            const unsigned char *input = count ? (unsigned char *)data->d_buf + cursor : NULL;
            status = tb_symbol_build_id_feed(&id, input, count, &consumed);
            cursor += consumed;
        }
        if (status == TB_SYMBOL_COMPANION_METADATA_FOUND) {
            TbSymbolBuildIdView_t view;
            CHECK(tb_symbol_build_id_view(&id, &view) && matches == 0);
            length = view.length;
            printf("BUILD_ID section=%llu note=%llu descriptor=%llu value=", (unsigned long long)i,
                   (unsigned long long)view.note_offset, (unsigned long long)view.descriptor_offset);
            for (size_t byte = 0; byte < length; ++byte)
                printf("%02x", view.bytes[byte]);
            puts("");
            ++matches;
        } else {
            CHECK(status == TB_SYMBOL_COMPANION_METADATA_ABSENT);
        }
        tb_symbol_build_id_discard(&id);
    }
    CHECK(matches == 1);
    return length;
}


static uint32_t checksum(const Artifact_t *artifact, bool changed)
{
    TbCrc32_t crc;
    CHECK(tb_crc32_init(&crc));
    unsigned char bytes[65536];
    uint64_t cursor = 0;
    while (cursor < artifact->directory.backing.bytes) {
        uint64_t remaining = artifact->directory.backing.bytes - cursor;
        size_t count = remaining < sizeof(bytes) ? (size_t)remaining : sizeof(bytes);
        CHECK(pread(artifact->descriptor, bytes, count, (off_t)cursor) == (ssize_t)count);
        if (changed && cursor == 0)
            bytes[0] ^= 1;
        CHECK(tb_crc32_update(&crc, bytes, count));
        cursor += count;
    }
    uint32_t value;
    CHECK(tb_crc32_final(&crc, artifact->directory.backing.bytes, &value));
    return value;
}


int main(int argc, char **argv)
{
    CHECK(argc == 3 && elf_version(EV_CURRENT) != EV_NONE);
    Artifact_t primary;
    Artifact_t companion;
    open_artifact(&primary, argv[1]);
    open_artifact(&companion, argv[2]);
    CHECK(primary.directory.backing.key.inode != companion.directory.backing.key.inode);
    uint32_t expected = debuglink(&primary, argv[2]);
    uint32_t value = checksum(&companion, false);
    CHECK(value == expected && checksum(&companion, true) != expected);
    printf("CRC32 bytes=%llu value=%08x changed-byte=mismatch\n",
           (unsigned long long)companion.directory.backing.bytes, value);

    unsigned char primary_id[128];
    unsigned char companion_id[128];
    size_t primary_bytes = build_id(&primary, primary_id, sizeof(primary_id));
    size_t companion_bytes = build_id(&companion, companion_id, sizeof(companion_id));
    CHECK(primary_bytes == companion_bytes && !memcmp(primary_id, companion_id, primary_bytes));
    CHECK(primary.directory.header->address_bytes == companion.directory.header->address_bytes);
    printf("PASS ELF%u retained companion metadata, complete CRC and matching build IDs; no attachment\n",
           primary.directory.header->address_bytes * 8U);
    close_artifact(&companion);
    close_artifact(&primary);
    return 0;
}
