#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Private retained-file fixtures. */
#include "symbol/helper.h"
#include "symbol/protocol.h"
#include "symbol/admission.h"
#include "symbol/symbols.h"
#include "../support/check.h"

#include <elf.h>
#include <fcntl.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct {
    unsigned char bytes[1024];
    size_t        length;
} Arguments_t;

typedef struct {
    TbSymbolHelper_t       *helper;
    TbSymbolWorkIdentity_t  identity;
    TbSymbolHelperResult_t  result;
    unsigned char          bytes[56];
    uint64_t               process_generation;
    bool                   complete;
    bool                   retired;
} Fixture_t;

static unsigned width;
static bool big_endian;
static unsigned char image[2048];

static void put(size_t offset, unsigned bytes, uint64_t value)
{
    CHECK(offset <= sizeof(image) && bytes <= sizeof(image) - offset);
    for (unsigned i = 0; i < bytes; ++i) {
        unsigned index = big_endian ? bytes - 1 - i : i;
        image[offset + index] = (unsigned char)(value >> (i * 8));
    }
}


static size_t section_at(unsigned index)
{
    return 128 + index * (width == 4 ? 40U : 64U);
}


static void section(unsigned index, uint32_t type, uint64_t offset, uint64_t bytes)
{
    size_t at = section_at(index);
    put(at + 4, 4, type);
    put(at + (width == 4 ? 16 : 24), width, offset);
    put(at + (width == 4 ? 20 : 32), width, bytes);
    put(at + (width == 4 ? 32 : 48), width, 1);
}


static void make_image(void)
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
    unsigned stride = width == 4 ? 16 : 24;
    section(1, SHT_SYMTAB, 512, 2 * stride);
    put(section_at(1) + (width == 4 ? 24 : 40), 4, 2);
    put(section_at(1) + (width == 4 ? 28 : 44), 4, 1);
    put(section_at(1) + (width == 4 ? 36 : 56), width, stride);
    section(2, SHT_STRTAB, 1024, 7);
    section(3, SHT_PROGBITS, 1536, 16);
    section(4, SHT_DYNSYM, 768, 2 * stride);
    put(section_at(4) + (width == 4 ? 24 : 40), 4, 2);
    put(section_at(4) + (width == 4 ? 28 : 44), 4, 1);
    put(section_at(4) + (width == 4 ? 36 : 56), width, stride);
    size_t symbol = 512 + stride;
    put(symbol, 4, 1);
    put(symbol + (width == 4 ? 12 : 4), 1, ELF64_ST_INFO(STB_GLOBAL, STT_OBJECT));
    put(symbol + (width == 4 ? 14 : 6), 2, 3);
    put(symbol + (width == 4 ? 4 : 8), width, 0x1000);
    put(symbol + (width == 4 ? 8 : 16), width, 4);
    memcpy(image + 768, image + 512, 2 * stride);
    memcpy(image + 1024, "\0alpha\0", 7);
}


static bool emit(void *context, const TbSymbolWorkIdentity_t *identity, TbSymbolHelperResult_t result)
{
    Fixture_t *fixture = context;
    CHECK(!fixture->complete && identity->request == fixture->identity.request);
    CHECK(identity->consumer == 3 && identity->revision == 7 && identity->generation == 11);
    CHECK(result.length <= sizeof(fixture->bytes));
    fixture->result = result;
    if (result.length)
        memcpy(fixture->bytes, result.payload, result.length);
    fixture->result.payload = fixture->bytes;
    if (result.descriptor >= 0) {
        CHECK((fcntl(result.descriptor, F_GETFD) & FD_CLOEXEC) != 0);
        fixture->result.descriptor = fcntl(result.descriptor, F_DUPFD_CLOEXEC, 3);
        CHECK(fixture->result.descriptor >= 0);
    }
    fixture->complete = true;
    return true;
}


static bool retired(void *context, const TbSymbolWorkIdentity_t *identity, uint64_t generation)
{
    Fixture_t *fixture = context;
    CHECK(fixture->complete && !fixture->retired && identity->request == fixture->identity.request);
    CHECK(generation == fixture->process_generation);
    fixture->retired = true;
    return true;
}


static void pump(Fixture_t *fixture)
{
    tb_symbol_helper_pump_with_release(fixture->helper, emit, retired, fixture);
    struct pollfd fds[2];
    size_t count = tb_symbol_helper_pollfds(fixture->helper, fds);
    CHECK(count <= 2 && poll(fds, (nfds_t)count, tb_symbol_helper_timeout(fixture->helper, 2)) >= 0);
}


static void start(Fixture_t *fixture, const char *executable, size_t decoder_bytes)
{
    TbSymbolHelperConfig_t config;
    tb_symbol_helper_defaults(&config);
    CHECK(!config.decoder_bytes);
    config.decoder_bytes = decoder_bytes;
    config.checkpoint_bytes = 64;
    config.checkpoint_steps = 1;
    fixture->helper = tb_symbol_helper_create(&config, NULL);
    CHECK(fixture->helper && tb_symbol_helper_start(fixture->helper, executable));
    for (unsigned turn = 0; turn < 4000 &&
         tb_symbol_helper_state(fixture->helper) != TB_SYMBOL_HELPER_READY; ++turn) {
        pump(fixture);
    }
    CHECK(tb_symbol_helper_state(fixture->helper) == TB_SYMBOL_HELPER_READY);
    fixture->process_generation = tb_symbol_helper_process_generation(fixture->helper);
}


static void close_helper(Fixture_t *fixture)
{
    tb_symbol_helper_close(fixture->helper);
    for (unsigned turn = 0; turn < 4000; ++turn) {
        if (tb_symbol_helper_destroy(fixture->helper)) {
            fixture->helper = NULL;
            return;
        }
        pump(fixture);
    }
    CHECK(false);
}


static void restart(Fixture_t *fixture, const char *executable)
{
    uint64_t generation = fixture->process_generation;
    tb_symbol_helper_close(fixture->helper);
    for (unsigned turn = 0; turn < 4000 &&
         tb_symbol_helper_reaped_generation(fixture->helper) != generation; ++turn) {
        pump(fixture);
    }
    CHECK(tb_symbol_helper_reaped_generation(fixture->helper) == generation);
    CHECK(tb_symbol_helper_start(fixture->helper, executable));
    for (unsigned turn = 0; turn < 4000 &&
         tb_symbol_helper_state(fixture->helper) != TB_SYMBOL_HELPER_READY; ++turn) {
        pump(fixture);
    }
    CHECK(tb_symbol_helper_state(fixture->helper) == TB_SYMBOL_HELPER_READY);
    fixture->process_generation = tb_symbol_helper_process_generation(fixture->helper);
    CHECK(fixture->process_generation == generation + 1);
}


static void word(Arguments_t *arguments, uint64_t value)
{
    CHECK(arguments->length + 8 <= sizeof(arguments->bytes));
    tb_symbol_protocol_put(arguments->bytes + arguments->length, value, 8);
    arguments->length += 8;
}


static void paths(Arguments_t *arguments, const char *first, const char *second)
{
    size_t left = strlen(first) + 1;
    size_t right = second ? strlen(second) + 1 : 0;
    size_t prefix = second ? 8 : 4;
    CHECK(arguments->length + prefix + left + right <= sizeof(arguments->bytes));
    tb_symbol_protocol_put(arguments->bytes + arguments->length, left, 4);
    if (second)
        tb_symbol_protocol_put(arguments->bytes + arguments->length + 4, right, 4);
    arguments->length += prefix;
    memcpy(arguments->bytes + arguments->length, first, left);
    arguments->length += left;
    if (second) {
        memcpy(arguments->bytes + arguments->length, second, right);
        arguments->length += right;
    }
}


static void submit(Fixture_t *fixture, uint32_t code, const Arguments_t *arguments)
{
    CHECK(!tb_symbol_helper_jobs(fixture->helper));
    ++fixture->identity.request;
    fixture->complete = false;
    fixture->retired = false;
    fixture->result = (TbSymbolHelperResult_t){
        .descriptor = -1
    };
    TbSymbolHelperRequest_t request = {
        .identity  = fixture->identity,
        .operation = code,
        .priority  = TB_SYMBOL_CONSTRUCTION,
        .heavy     = true,
        .arguments = arguments->bytes,
        .length    = arguments->length
    };
    TbSymbolAdmission_e admission = tb_symbol_helper_submit(fixture->helper, &request);
    if (admission != TB_SYMBOL_ADMITTED) {
        fprintf(stderr, "inspection admission %u for request %llu, helper %u: %s\n", admission,
                (unsigned long long)request.identity.request, tb_symbol_helper_state(fixture->helper),
                tb_symbol_helper_error(fixture->helper));
    }
    CHECK(admission == TB_SYMBOL_ADMITTED);
}


static void finish(Fixture_t *fixture, TbSymbolOutcome_e expected)
{
    for (unsigned turn = 0; turn < 6000 && !fixture->retired; ++turn)
        pump(fixture);
    if (!fixture->retired || fixture->result.outcome != expected) {
        fprintf(stderr, "inspection request %llu: expected %u, got %u, detail %u; %s\n",
                (unsigned long long)fixture->identity.request, expected, fixture->result.outcome,
                fixture->result.detail, tb_symbol_helper_error(fixture->helper));
    }
    CHECK(fixture->complete && fixture->retired && fixture->result.outcome == expected);
    CHECK(!tb_symbol_helper_jobs(fixture->helper));
    if (expected != TB_SYMBOL_SUCCESS)
        CHECK(!fixture->result.length && fixture->result.descriptor < 0);
}


static Arguments_t inspection(const char *path, TbSymbolStorageKey_t root, uint64_t slot,
                              TbSymbolStorageKey_t file, uint64_t selected)
{
    Arguments_t arguments = {0};
    word(&arguments, sizeof(image));
    word(&arguments, slot);
    word(&arguments, root.device);
    word(&arguments, root.inode);
    word(&arguments, file.device);
    word(&arguments, file.inode);
    word(&arguments, selected);
    paths(&arguments, path, NULL);
    return arguments;
}


static void check_summary(const Fixture_t *fixture, uint64_t selected)
{
    CHECK(fixture->result.length == 56 && fixture->result.descriptor < 0);
    CHECK(tb_symbol_protocol_get(fixture->bytes, 8) == 5);
    CHECK(tb_symbol_protocol_get(fixture->bytes + 8, 8) == selected);
    CHECK(tb_symbol_protocol_get(fixture->bytes + 16, 8) == (selected == UINT64_MAX ? 0U : 2U));
    CHECK(tb_symbol_protocol_get(fixture->bytes + 24, 4) == ET_EXEC);
    CHECK(tb_symbol_protocol_get(fixture->bytes + 28, 4) == EM_RISCV);
    CHECK(tb_symbol_protocol_get(fixture->bytes + 32, 4) == width);
    CHECK(tb_symbol_protocol_get(fixture->bytes + 36, 4) == (big_endian ? 2U : 1U));
    CHECK(!tb_symbol_protocol_get(fixture->bytes + 40, 4));
    CHECK(tb_symbol_protocol_get(fixture->bytes + 44, 4) ==
          (selected == UINT64_MAX ? 0U : selected == 1 ? SHT_SYMTAB : SHT_DYNSYM));
    CHECK(tb_symbol_protocol_get(fixture->bytes + 48, 8) == sizeof(image));
}


static void admitted_fixture(Fixture_t *fixture, const char *origin, const char *path,
                             TbSymbolStorageKey_t root, uint64_t slot, unsigned scenario,
                             const char *executable)
{
    make_image();
    if (scenario == 1)
        put(512 + (width == 4 ? 16 : 24), 4, 7);
    if (scenario == 2)
        put(section_at(1) + 8, width, SHF_COMPRESSED);
    if (scenario == 3)
        image[EI_VERSION] = 0;
    int writer = open(origin, O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0600);
    CHECK(writer >= 0 && write(writer, image, sizeof(image)) == (ssize_t)sizeof(image));
    CHECK(!close(writer));
    TbSymbolAdmissionLedger_t *ledger = tb_symbol_admission_create(sizeof(image), sizeof(image), NULL);
    TbSymbolReservation_t *ticket = tb_symbol_admission_prepare(ledger);
    CHECK(ledger && ticket);
    Arguments_t arguments = {0};
    word(&arguments, sizeof(image));
    word(&arguments, slot);
    word(&arguments, root.device);
    word(&arguments, root.inode);
    paths(&arguments, origin, path);
    submit(fixture, TB_SYMBOL_OPERATION_ACQUIRE, &arguments);
    TbSymbolAcquireOffer_t offer;
    bool offered = false;
    for (unsigned turn = 0; turn < 4000 && !offered; ++turn) {
        pump(fixture);
        offered = tb_symbol_helper_offer(fixture->helper, fixture->identity, &offer);
    }
    CHECK(offered && offer.bytes == sizeof(image) && tb_symbol_admission_assign(ticket, offer.bytes));
    CHECK(tb_symbol_helper_grant(fixture->helper, fixture->identity, offer.bytes));
    finish(fixture, TB_SYMBOL_SUCCESS);
    int retained = fixture->result.descriptor;
    CHECK(retained >= 0);
    TbSymbolBackingCharge_t *backing = tb_symbol_admission_commit(ticket, NULL);
    CHECK(backing);
    writer = open(origin, O_WRONLY | O_TRUNC | O_CLOEXEC);
    CHECK(writer >= 0 && write(writer, "changed origin", 14) == 14 && !close(writer));

    if (slot == 1) {
        restart(fixture, executable);
        close_helper(fixture);
        start(fixture, executable, 80 * 1024);
        arguments = inspection(path, root, slot, offer.key, 1);
        submit(fixture, TB_SYMBOL_OPERATION_ELF_VALIDATE, &arguments);
        finish(fixture, TB_SYMBOL_RESOURCE_FAILURE);
        arguments = inspection(path, root, slot, offer.key, UINT64_MAX);
        submit(fixture, TB_SYMBOL_OPERATION_ELF_VALIDATE, &arguments);
        finish(fixture, TB_SYMBOL_SUCCESS);
        check_summary(fixture, UINT64_MAX);
        CHECK(fcntl(retained, F_GETFD) >= 0);
        close_helper(fixture);
        start(fixture, executable, 512 * 1024);
    }

    arguments = inspection(path, root, slot, offer.key, UINT64_MAX);
    submit(fixture, TB_SYMBOL_OPERATION_ELF_VALIDATE, &arguments);
    finish(fixture, scenario == 3 ? TB_SYMBOL_MALFORMED : TB_SYMBOL_SUCCESS);
    if (scenario == 3) {
        CHECK(fixture->result.detail == (TB_SYMBOL_DETAIL_ELF_SECTIONS | TB_SYMBOL_SECTIONS_HEADER));
        goto cleanup;
    }
    check_summary(fixture, UINT64_MAX);
    arguments = inspection(path, root, slot, offer.key, 1);
    submit(fixture, TB_SYMBOL_OPERATION_ELF_VALIDATE, &arguments);
    TbSymbolOutcome_e expected = scenario == 1 ? TB_SYMBOL_MALFORMED :
                                scenario == 2 ? TB_SYMBOL_UNSUPPORTED : TB_SYMBOL_SUCCESS;
    finish(fixture, expected);
    if (!scenario) {
        check_summary(fixture, 1);
    } else {
        CHECK(fixture->result.detail == (TB_SYMBOL_DETAIL_ELF_SYMBOLS |
              (scenario == 1 ? TB_SYMBOL_SYMBOLS_NAME : TB_SYMBOL_SYMBOLS_TABLE)));
        CHECK(fixture->result.record_index == (scenario == 1 ? 1U : UINT64_MAX));
    }
    arguments = inspection(path, root, slot, offer.key, 4);
    submit(fixture, TB_SYMBOL_OPERATION_ELF_VALIDATE, &arguments);
    finish(fixture, TB_SYMBOL_SUCCESS);
    check_summary(fixture, 4);
    arguments = inspection(path, root, slot, offer.key, 999);
    submit(fixture, TB_SYMBOL_OPERATION_ELF_VALIDATE, &arguments);
    finish(fixture, TB_SYMBOL_MALFORMED);
    CHECK(fixture->result.detail == TB_SYMBOL_DETAIL_ELF_SELECTION && fixture->result.record_index == 999);

    arguments = inspection(path, root, slot, offer.key, 4);
    submit(fixture, TB_SYMBOL_OPERATION_ELF_VALIDATE, &arguments);
    TbSymbolHelperTiming_t timing;
    bool serviced = false;
    for (unsigned turn = 0; turn < 4000 && !serviced; ++turn) {
        pump(fixture);
        serviced = tb_symbol_helper_timing(fixture->helper, fixture->identity.request, &timing) &&
                   timing.serviced && timing.progress >= 40;
    }
    CHECK(serviced && !fixture->complete && tb_symbol_helper_cancel(fixture->helper, fixture->identity));
    finish(fixture, TB_SYMBOL_CANCELLED);
    CHECK(fcntl(retained, F_GETFD) >= 0);

    TbSymbolStorageKey_t wrong = offer.key;
    ++wrong.inode;
    arguments = inspection(path, root, slot, wrong, 4);
    submit(fixture, TB_SYMBOL_OPERATION_ELF_VALIDATE, &arguments);
    finish(fixture, TB_SYMBOL_OPERATIONAL_FAILURE);
    CHECK(fixture->result.detail == TB_SYMBOL_DETAIL_ELF_OPEN);
cleanup:
    CHECK(!close(retained));
    CHECK(tb_symbol_admission_close(ticket) && tb_symbol_admission_evict(backing));
    arguments = (Arguments_t){0};
    word(&arguments, slot);
    word(&arguments, root.device);
    word(&arguments, root.inode);
    word(&arguments, offer.key.device);
    word(&arguments, offer.key.inode);
    paths(&arguments, path, NULL);
    submit(fixture, TB_SYMBOL_OPERATION_REMOVE_FILE, &arguments);
    finish(fixture, TB_SYMBOL_SUCCESS);
    CHECK(tb_symbol_admission_backing_cleaned(backing) && !tb_symbol_admission_usage(ledger).bytes);
    CHECK(tb_symbol_admission_destroy(ledger));
}


int main(int argc, char **argv)
{
    CHECK(argc == 2);
    Fixture_t fixture = {
        .identity = {
            .consumer   = 3,
            .revision   = 7,
            .generation = 11
        }
    };
    start(&fixture, argv[1], 0);
    Arguments_t arguments = inspection("/not/a/retained/namespace", (TbSymbolStorageKey_t){0},
                                       1, (TbSymbolStorageKey_t){0}, UINT64_MAX);
    submit(&fixture, TB_SYMBOL_OPERATION_ELF_VALIDATE, &arguments);
    finish(&fixture, TB_SYMBOL_UNSUPPORTED);
    CHECK(fixture.result.detail == TB_SYMBOL_DETAIL_ELF_DISABLED);
    close_helper(&fixture);

    char parent[] = "/tmp/tb-symbol-inspect-XXXXXX";
    CHECK(mkdtemp(parent));
    char path[256];
    char origin[256];
    CHECK(snprintf(path, sizeof(path), "%s/backing", parent) > 0);
    CHECK(snprintf(origin, sizeof(origin), "%s/input", parent) > 0);
    start(&fixture, argv[1], 512 * 1024);
    arguments = (Arguments_t){0};
    paths(&arguments, path, NULL);
    submit(&fixture, TB_SYMBOL_OPERATION_CREATE_STORAGE, &arguments);
    finish(&fixture, TB_SYMBOL_SUCCESS);
    TbSymbolStorageKey_t root = {
        .device = tb_symbol_protocol_get(fixture.bytes, 8),
        .inode  = tb_symbol_protocol_get(fixture.bytes + 8, 8)
    };
    uint64_t slot = 0;
    for (width = 4; width <= 8; width += 4) {
        for (unsigned order = 0; order < 2; ++order) {
            big_endian = order != 0;
            for (unsigned scenario = 0; scenario < 4; ++scenario)
                admitted_fixture(&fixture, origin, path, root, ++slot, scenario, argv[1]);
        }
    }
    arguments = (Arguments_t){0};
    word(&arguments, root.device);
    word(&arguments, root.inode);
    paths(&arguments, path, NULL);
    submit(&fixture, TB_SYMBOL_OPERATION_REMOVE_STORAGE, &arguments);
    finish(&fixture, TB_SYMBOL_SUCCESS);
    close_helper(&fixture);
    CHECK(!unlink(origin) && !rmdir(parent));
    puts("PASS supervised retained ELF validation, complete scopes, exact backing and bounded retirement");
    return 0;
}
