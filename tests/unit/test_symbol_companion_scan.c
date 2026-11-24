#define _POSIX_C_SOURCE 200809L
#include "symbol/companion_receipt.h"
#include "symbol/protocol.h"
#include "symbol/crc32.h"
#include "../support/check.h"

#include <elf.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

static unsigned char image[2048];
static char retained_path[64];
static bool big_endian;
static unsigned width;
static unsigned allocation_index;
static unsigned fail_at;

void *__real_malloc(size_t bytes);
void *__real_calloc(size_t count, size_t bytes);
void *__wrap_malloc(size_t bytes);
void *__wrap_calloc(size_t count, size_t bytes);

void *__wrap_malloc(size_t bytes)
{
    return fail_at && ++allocation_index == fail_at ? NULL : __real_malloc(bytes);
}


void *__wrap_calloc(size_t count, size_t bytes)
{
    return fail_at && ++allocation_index == fail_at ? NULL : __real_calloc(count, bytes);
}


static void put(size_t at, unsigned bytes, uint64_t value)
{
    CHECK(at + bytes <= sizeof(image));
    for (unsigned i = 0; i < bytes; ++i)
        image[at + (big_endian ? bytes - 1 - i : i)] = (unsigned char)(value >> (i * 8));
}


static void section(unsigned index, uint32_t name, uint32_t type, uint64_t offset,
                    uint64_t bytes, unsigned alignment)
{
    size_t at = 128 + index * (width == 4 ? 40U : 64U);
    put(at, 4, name);
    put(at + 4, 4, type);
    put(at + (width == 4 ? 16 : 24), width, offset);
    put(at + (width == 4 ? 20 : 32), width, bytes);
    put(at + (width == 4 ? 32 : 48), width, alignment);
}


static void make_image(unsigned variant)
{
    memset(image, 0, sizeof(image));
    memcpy(image, ELFMAG, SELFMAG);
    image[EI_CLASS] = width == 4 ? ELFCLASS32 : ELFCLASS64;
    image[EI_DATA] = big_endian ? ELFDATA2MSB : ELFDATA2LSB;
    image[EI_VERSION] = EV_CURRENT;
    put(16, 2, ET_EXEC);
    put(18, 2, EM_RISCV);
    put(20, 4, EV_CURRENT);
    put(width == 4 ? 32 : 40, width, 128);
    put(width == 4 ? 40 : 52, 2, width == 4 ? 52 : 64);
    put(width == 4 ? 46 : 58, 2, width == 4 ? 40 : 64);
    put(width == 4 ? 48 : 60, 2, 5);
    put(width == 4 ? 50 : 62, 2, variant == 5 ? 0 : 1);
    static const char names[] = "\0.shstrtab\0.gnu_debuglink\0.note.custom\0";
    memcpy(image + 512, names, sizeof(names));
    section(1, 1, SHT_STRTAB, 512, sizeof(names), 1);
    section(2, 11, SHT_PROGBITS, 640, 12, 1);
    memcpy(image + 640, "x.debug", 8);
    put(648, 4, 0x12345678);
    section(3, 26, SHT_NOTE, 768, 24, 4);
    put(768, 4, 4);
    put(772, 4, 8);
    put(776, 4, 3);
    memcpy(image + 780, "GNU", 4);
    memcpy(image + 784, "abcdefgh", 8);
    section(4, 0, SHT_PROGBITS, 1024, 1, 1);
    if (variant == 5) {
        for (unsigned index = 1; index < 5; ++index)
            put(128 + index * (width == 4 ? 40U : 64U), 4, 0);
    }
    if (variant == 1) {
        section(4, 26, SHT_NOTE, 1024, 24, 4);
        memcpy(image + 1024, image + 768, 24);
    } else if (variant == 2) {
        section(4, 26, SHT_NOTE, 1024, 12, 4);
        put(1024, 4, 100);
    } else if (variant == 3) {
        put(128 + 3 * (width == 4 ? 40U : 64U) + (width == 4 ? 32 : 48), width, 16);
    } else if (variant == 4) {
        section(4, 11, SHT_PROGBITS, 1024, 12, 1);
        memcpy(image + 1024, image + 640, 12);
    } else if (variant == 6) {
        section(3, 26, SHT_PROGBITS, 768, 24, 4);
    } else if (variant == 7) {
        section(3, 26, SHT_NOTE, 768, 24, 8);
    } else if (variant == 8) {
        put(128 + 3 * (width == 4 ? 40U : 64U) + 8, width, SHF_COMPRESSED);
    } else if (variant == 9) {
        put(128 + (width == 4 ? 40U : 64U) + 8, width, SHF_COMPRESSED);
    } else if (variant == 10) {
        section(2, 11, SHT_NOTE, 640, 12, 4);
    } else if (variant == 11) {
        section(2, 11, SHT_PROGBITS, 640, 13, 1);
    } else if (variant == 12) {
        section(3, 26, SHT_NOTE, 768, 48, 4);
        memcpy(image + 792, image + 768, 24);
    } else if (variant == 13) {
        put(776, 4, 4);
    } else if (variant == 14) {
        put(772, 4, 0);
        section(3, 26, SHT_NOTE, 768, 16, 4);
    } else if (variant == 15) {
        memcpy(image + 640, "x/debug", 8);
    } else if (variant == 16) {
        section(3, 26, SHT_NOTE, 768, 0, 4);
    }
}


static int frozen(TbSymbolStorageInfo_t *info)
{
    CHECK(snprintf(retained_path, sizeof(retained_path), "/tmp/tb-companion-scan-XXXXXX") > 0);
    int writer = mkstemp(retained_path);
    CHECK(writer >= 0 && write(writer, image, sizeof(image)) == (ssize_t)sizeof(image));
    CHECK(fchmod(writer, 0400) == 0 && close(writer) == 0);
    int descriptor = open(retained_path, O_RDONLY | O_CLOEXEC);
    struct stat native;
    CHECK(descriptor >= 0 && fstat(descriptor, &native) == 0);
    *info = (TbSymbolStorageInfo_t) {
        .key = {
            .device = (uint64_t)native.st_dev,
            .inode  = (uint64_t)native.st_ino
        },
        .bytes = (uint64_t)native.st_size
    };
    return descriptor;
}


static void close_frozen(int descriptor)
{
    CHECK(close(descriptor) == 0 && unlink(retained_path) == 0);
}


static TbSymbolOutcome_e finish(TbSymbolCompanionScan_t *scan, size_t slice)
{
    uint64_t previous = 0;
    for (unsigned i = 0; i < 100000; ++i) {
        TbSymbolOutcome_e status = tb_symbol_companion_scan_step(scan, slice);
        uint64_t progress = tb_symbol_companion_scan_progress(scan);
        CHECK(progress >= previous);
        previous = progress;
        if (status != TB_SYMBOL_PENDING)
            return status;
    }
    CHECK(false);
    return TB_SYMBOL_PENDING;
}


static void receipt(const TbSymbolCompanionInventory_t *view)
{
    unsigned char bytes[256];
    size_t length = tb_symbol_companion_receipt_size(view);
    CHECK(length <= sizeof(bytes) && tb_symbol_companion_receipt_encode(view, bytes, length));
    TbSymbolCompanionInventory_t decoded;
    CHECK(tb_symbol_companion_receipt_decode(bytes, length, &decoded));
    CHECK(decoded.backing.key.inode == view->backing.key.inode && decoded.file_crc == view->file_crc);
    CHECK(decoded.name_bytes == view->name_bytes && decoded.id_bytes == view->id_bytes);
    CHECK(decoded.header.byte_order == view->header.byte_order);
    CHECK(decoded.id_bytes == 0 || memcmp(decoded.build_id, view->build_id, decoded.id_bytes) == 0);
    unsigned char sentinel[sizeof(decoded)];
    memcpy(sentinel, &decoded, sizeof(decoded));
    for (size_t i = 0; i < length; ++i) {
        CHECK(!tb_symbol_companion_receipt_decode(bytes, i, &decoded));
        CHECK(memcmp(&decoded, sentinel, sizeof(decoded)) == 0);
    }
    CHECK(!tb_symbol_companion_receipt_decode(bytes, length + 1, &decoded));
    tb_symbol_protocol_put(bytes + 76, 2, 4);
    CHECK(!tb_symbol_companion_receipt_decode(bytes, length, &decoded));
    CHECK(tb_symbol_companion_receipt_encode(view, bytes, length));
    tb_symbol_protocol_put(bytes + 92, TB_SYMBOL_COMPANION_ID_BYTES + 1, 4);
    CHECK(!tb_symbol_companion_receipt_decode(bytes, length, &decoded));
}


static void receipt_limits(void)
{
    unsigned char name[TB_SYMBOL_COMPANION_PATH_BYTES];
    unsigned char id[TB_SYMBOL_COMPANION_ID_BYTES];
    unsigned char encoded[TB_SYMBOL_COMPANION_RECEIPT_BYTES + sizeof(name) + sizeof(id)];
    memset(name, 'x', sizeof(name));
    memset(id, 0xff, sizeof(id));
    TbSymbolCompanionInventory_t input = {
        .header = {
            .byte_order    = TB_SYMBOL_BIG_ENDIAN,
            .address_bytes = 8
        },
        .name              = name,
        .build_id          = id,
        .name_bytes        = sizeof(name) - 1,
        .id_bytes          = sizeof(id),
        .debuglink_section = 1,
        .build_id_section  = 2,
        .build_id_note     = 0,
        .debuglink_known   = true
    };
    size_t bytes = tb_symbol_companion_receipt_size(&input);
    CHECK(bytes == 8287 && tb_symbol_companion_receipt_encode(&input, encoded, bytes));
    TbSymbolCompanionInventory_t output;
    CHECK(tb_symbol_companion_receipt_decode(encoded, bytes, &output));
    CHECK(output.name_bytes == sizeof(name) - 1 && output.id_bytes == sizeof(id));
    CHECK(memcmp(output.name, name, output.name_bytes) == 0);
    CHECK(memcmp(output.build_id, id, output.id_bytes) == 0);
    CHECK(!tb_symbol_companion_receipt_decode(encoded, bytes + 1, &output));
    ++input.id_bytes;
    CHECK(tb_symbol_companion_receipt_size(&input) == 0);
    --input.id_bytes;
    ++input.name_bytes;
    CHECK(tb_symbol_companion_receipt_size(&input) == 0);
}


static void cases(void)
{
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 1024 * 1024));
    for (unsigned variant = 0; variant <= 16; ++variant) {
        make_image(variant);
        TbSymbolStorageInfo_t info;
        int descriptor = frozen(&info);
        TbSymbolCompanionScan_t *scan = tb_symbol_companion_scan_create(info, descriptor, 64, 64, &budget);
        CHECK(scan);
        TbSymbolCompanionInventory_t view;
        CHECK(!tb_symbol_companion_scan_view(scan, &view));
        bool malformed = variant == 1 || variant == 2 || variant == 4 || variant == 10 ||
                         variant == 11 || variant == 12 || variant == 14;
        TbSymbolOutcome_e expected = malformed ? TB_SYMBOL_MALFORMED :
                                    variant == 3 || variant == 8 || variant == 15 ? TB_SYMBOL_UNSUPPORTED :
                                                                                 TB_SYMBOL_SUCCESS;
        TbSymbolOutcome_e actual = finish(scan, 64);
        if (actual != expected) {
            TbSymbolCompanionScanFailure_t failure = tb_symbol_companion_scan_failure(scan);
            fprintf(stderr, "ELF%u order=%u variant=%u actual=%u expected=%u directory=%u metadata=%u\n",
                    width * 8, big_endian ? 2U : 1U, variant, (unsigned)actual, (unsigned)expected,
                    (unsigned)failure.directory.reason, (unsigned)failure.metadata);
        }
        CHECK(actual == expected);
        CHECK(tb_symbol_companion_scan_view(scan, &view) == (expected == TB_SYMBOL_SUCCESS));
        if (expected == TB_SYMBOL_SUCCESS) {
            CHECK(view.debuglink_known == (variant != 5 && variant != 9));
            CHECK(view.name_bytes == (variant == 5 || variant == 9 ? 0 : 7));
            CHECK(view.id_bytes == (variant == 6 || variant == 13 || variant == 16 ? 0 : 8));
            CHECK(view.name_bytes == 0 || memcmp(view.name, "x.debug", 7) == 0);
            CHECK(view.debuglink_crc == (variant == 5 || variant == 9 ? 0 : 0x12345678));
            TbCrc32_t crc;
            uint32_t expected_crc;
            CHECK(tb_crc32_init(&crc) && tb_crc32_update(&crc, image, sizeof(image)) &&
                  tb_crc32_final(&crc, sizeof(image), &expected_crc));
            CHECK(view.file_crc == expected_crc);
            receipt(&view);
        } else {
            TbSymbolCompanionScanFailure_t failure = tb_symbol_companion_scan_failure(scan);
            CHECK(failure.metadata != TB_SYMBOL_COMPANION_METADATA_PENDING);
        }
        tb_symbol_companion_scan_destroy(scan);
        CHECK(budget.used == 0);
        close_frozen(descriptor);
    }
}


static void failures(void)
{
    make_image(0);
    TbSymbolStorageInfo_t info;
    int descriptor = frozen(&info);
    TbSymbolBudget_t budget;
    CHECK(tb_symbol_budget_init(&budget, 1024 * 1024));
    for (unsigned failure = 1; failure <= 9; ++failure) {
        allocation_index = 0;
        fail_at = failure;
        TbSymbolCompanionScan_t *scan = tb_symbol_companion_scan_create(info, descriptor, 64, 64, &budget);
        if (scan) {
            TbSymbolOutcome_e status = finish(scan, 257);
            CHECK(status == TB_SYMBOL_SUCCESS || status == TB_SYMBOL_RESOURCE_FAILURE);
            tb_symbol_companion_scan_destroy(scan);
        }
        fail_at = 0;
        CHECK(budget.used == 0);
    }
    bool cancelled_through_completion = false;
    for (unsigned stop = 0; stop < 1000; ++stop) {
        TbSymbolCompanionScan_t *scan = tb_symbol_companion_scan_create(info, descriptor, 64, 64, &budget);
        CHECK(scan);
        TbSymbolOutcome_e status = TB_SYMBOL_PENDING;
        for (unsigned i = 0; i < stop && status == TB_SYMBOL_PENDING; ++i)
            status = tb_symbol_companion_scan_step(scan, 64);
        tb_symbol_companion_scan_destroy(scan);
        CHECK(budget.used == 0 && fcntl(descriptor, F_GETFD) >= 0);
        if (status == TB_SYMBOL_SUCCESS) {
            cancelled_through_completion = true;
            break;
        }
    }
    CHECK(cancelled_through_completion);
    TbSymbolCompanionScan_t *scan = tb_symbol_companion_scan_create(info, descriptor, 4, 64, &budget);
    CHECK(scan && finish(scan, 64) == TB_SYMBOL_RESOURCE_FAILURE);
    tb_symbol_companion_scan_destroy(scan);
    CHECK(budget.used == 0);
    scan = tb_symbol_companion_scan_create(info, descriptor, 64, 4, &budget);
    CHECK(scan && finish(scan, 64) == TB_SYMBOL_RESOURCE_FAILURE);
    tb_symbol_companion_scan_destroy(scan);
    CHECK(budget.used == 0);
    CHECK(tb_symbol_budget_init(&budget, 1));
    CHECK(!tb_symbol_companion_scan_create(info, descriptor, 64, 64, &budget) && budget.used == 0);
    close_frozen(descriptor);
}


static void relations(void)
{
    static const unsigned char id[] = {1, 2, 3, 4};
    TbSymbolCompanionInventory_t primary = {
        .header = {
            .byte_order    = TB_SYMBOL_LITTLE_ENDIAN,
            .address_bytes = 8,
            .object_type   = ET_DYN,
            .machine       = EM_X86_64
        },
        .name              = (const unsigned char *)"x.debug",
        .build_id          = id,
        .name_bytes        = 7,
        .id_bytes          = sizeof(id),
        .debuglink_section = 4,
        .build_id_section  = 3,
        .build_id_note     = 0,
        .debuglink_crc     = 0x1234,
        .file_crc          = 0x1234,
        .debuglink_known   = true
    };
    TbSymbolCompanionInventory_t candidate = primary;
    CHECK(tb_symbol_companion_relation(&primary, &candidate, TB_SYMBOL_COMPANION_BY_DEBUGLINK) ==
          TB_SYMBOL_COMPANION_RELATION_MATCH);
    CHECK(tb_symbol_companion_relation(&primary, &candidate, TB_SYMBOL_COMPANION_BY_BUILD_ID) ==
          TB_SYMBOL_COMPANION_RELATION_MATCH);
    ++candidate.file_crc;
    CHECK(tb_symbol_companion_relation(&primary, &candidate, TB_SYMBOL_COMPANION_BY_DEBUGLINK) ==
          TB_SYMBOL_COMPANION_RELATION_CRC_MISMATCH);
    candidate = primary;
    candidate.build_id = (const unsigned char *)"wxyz";
    CHECK(tb_symbol_companion_relation(&primary, &candidate, TB_SYMBOL_COMPANION_BY_DEBUGLINK) ==
          TB_SYMBOL_COMPANION_RELATION_ID_MISMATCH);
    candidate = primary;
    candidate.id_bytes = 0;
    CHECK(tb_symbol_companion_relation(&primary, &candidate, TB_SYMBOL_COMPANION_BY_BUILD_ID) ==
          TB_SYMBOL_COMPANION_RELATION_MISSING);
    CHECK(tb_symbol_companion_relation(&primary, &candidate, TB_SYMBOL_COMPANION_BY_DEBUGLINK) ==
          TB_SYMBOL_COMPANION_RELATION_MATCH);
    candidate = primary;
    ++candidate.header.machine;
    CHECK(tb_symbol_companion_relation(&primary, &candidate, TB_SYMBOL_COMPANION_BY_DEBUGLINK) ==
          TB_SYMBOL_COMPANION_RELATION_ELF_MISMATCH);
    candidate = primary;
    candidate.header.object_type = ET_REL;
    CHECK(tb_symbol_companion_relation(&primary, &candidate, TB_SYMBOL_COMPANION_BY_DEBUGLINK) ==
          TB_SYMBOL_COMPANION_RELATION_UNSUPPORTED);
    CHECK(tb_symbol_companion_relation(NULL, &candidate, TB_SYMBOL_COMPANION_BY_DEBUGLINK) ==
          TB_SYMBOL_COMPANION_RELATION_INVALID);
}


int main(void)
{
    for (width = 4; width <= 8; width += 4) {
        for (unsigned order = 0; order < 2; ++order) {
            big_endian = order != 0;
            cases();
        }
    }
    width = 8;
    failures();
    relations();
    receipt_limits();
    puts("PASS complete section inventories, conflicts, receipts, ownership failures and relation policy");
    return 0;
}
