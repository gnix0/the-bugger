#include "symbol/source_check.h"
#include "../support/check.h"

#include <stdio.h>
#include <string.h>

int main(void)
{
    const unsigned char digest[] = {
        0x90, 0x01, 0x50, 0x98, 0x3c, 0xd2, 0x4f, 0xb0,
        0xd6, 0x96, 0x3f, 0x7d, 0x28, 0xe1, 0x7f, 0x72
    };
    TbSymbolSourceFileView_t file = {
        .file_index    = 7,
        .file_offset   = 123,
        .md5_offset    = 160,
        .md5_available = true
    };
    TbSymbolSourceBytes_t source = {
        .data     = (const unsigned char *)"abc",
        .bytes    = 3,
        .identity = 1,
        .revision = 1,
        .complete = true
    };
    TbSymbolSourceCheck_t check;
    TbSymbolSourceCheckView_t view;
    CHECK(tb_symbol_source_check_init(&check, file, source, digest) == TB_SYMBOL_SOURCE_CHECK_PENDING);
    for (unsigned i = 0; i < 2; ++i) {
        CHECK(!tb_symbol_source_check_view(&check, source, &view));
        CHECK(tb_symbol_source_check_step(&check, source, 1) == TB_SYMBOL_SOURCE_CHECK_PENDING);
    }
    CHECK(tb_symbol_source_check_step(&check, source, 1) == TB_SYMBOL_SOURCE_CHECK_MATCH);
    CHECK(tb_symbol_source_check_view(&check, source, &view));
    CHECK(view.file.file_offset == 123 && view.file.md5_offset == 160 &&
          view.source.identity == 1 && view.source.revision == 1 && !memcmp(view.calculated, digest, 16));

    TbSymbolSourceBytes_t changed = source;
    ++changed.revision;
    CHECK(!tb_symbol_source_check_view(&check, changed, &view));
    CHECK(tb_symbol_source_check_step(&check, changed, 1) == TB_SYMBOL_SOURCE_CHECK_STALE);
    CHECK(!tb_symbol_source_check_view(&check, source, &view));
    CHECK(tb_symbol_source_check_step(&check, source, 1) == TB_SYMBOL_SOURCE_CHECK_STALE);
    CHECK(tb_symbol_source_check_init(&check, file, changed, digest) == TB_SYMBOL_SOURCE_CHECK_PENDING);
    changed.data = (const unsigned char *)"abd";
    ++changed.revision;
    CHECK(tb_symbol_source_check_step(&check, changed, 2) == TB_SYMBOL_SOURCE_CHECK_STALE);
    CHECK(tb_symbol_source_check_init(&check, file, changed, digest) == TB_SYMBOL_SOURCE_CHECK_PENDING);
    CHECK(tb_symbol_source_check_step(&check, changed, 65536) == TB_SYMBOL_SOURCE_CHECK_MISMATCH);
    CHECK(tb_symbol_source_check_view(&check, changed, &view) && view.digest_available);

    /* Replacing the byte lease with equal bytes still invalidates correspondence. */
    CHECK(tb_symbol_source_check_init(&check, file, source, digest) == TB_SYMBOL_SOURCE_CHECK_PENDING);
    changed = source;
    ++changed.identity;
    CHECK(tb_symbol_source_check_step(&check, changed, 3) == TB_SYMBOL_SOURCE_CHECK_STALE);
    CHECK(tb_symbol_source_check_init(&check, file, source, digest) == TB_SYMBOL_SOURCE_CHECK_PENDING);
    changed = source;
    --changed.bytes;
    CHECK(tb_symbol_source_check_step(&check, changed, 3) == TB_SYMBOL_SOURCE_CHECK_STALE);
    CHECK(tb_symbol_source_check_init(&check, file, source, digest) == TB_SYMBOL_SOURCE_CHECK_PENDING);
    changed = source;
    changed.complete = false;
    CHECK(tb_symbol_source_check_step(&check, changed, 3) == TB_SYMBOL_SOURCE_CHECK_STALE);
    CHECK(tb_symbol_source_check_init(&check, file, changed, digest) == TB_SYMBOL_SOURCE_CHECK_RECORDED);
    CHECK(tb_symbol_source_check_view(&check, changed, &view) && !view.source.complete);
    CHECK(check.cursor == 0 && check.hash.state == TB_MD5_UNINITIALIZED);

    file.md5_available = false;
    CHECK(tb_symbol_source_check_init(&check, file, source, NULL) == TB_SYMBOL_SOURCE_CHECK_RECORDED);
    CHECK(tb_symbol_source_check_view(&check, source, &view) && !view.digest_available);
    TbSymbolSourceCheck_t before = check;
    CHECK(tb_symbol_source_check_init(&check, file, source, digest) == TB_SYMBOL_SOURCE_CHECK_INVALID);
    CHECK(!memcmp(&before, &check, sizeof(check)));
    CHECK(tb_symbol_source_check_step(&check, source, 0) == TB_SYMBOL_SOURCE_CHECK_INVALID);
    CHECK(tb_symbol_source_check_step(&check, source, 65537) == TB_SYMBOL_SOURCE_CHECK_INVALID);
    changed = source;
    changed.identity = 0;
    CHECK(tb_symbol_source_check_init(&check, file, changed, NULL) == TB_SYMBOL_SOURCE_CHECK_INVALID);
    changed = source;
    changed.data = NULL;
    CHECK(tb_symbol_source_check_init(&check, file, changed, NULL) == TB_SYMBOL_SOURCE_CHECK_INVALID);

    const unsigned char empty_digest[] = {
        0xd4, 0x1d, 0x8c, 0xd9, 0x8f, 0x00, 0xb2, 0x04,
        0xe9, 0x80, 0x09, 0x98, 0xec, 0xf8, 0x42, 0x7e
    };
    changed = (TbSymbolSourceBytes_t) {
        .identity = 3,
        .revision = 1,
        .complete = true
    };
    file.md5_available = true;
    CHECK(tb_symbol_source_check_init(&check, file, changed, empty_digest) == TB_SYMBOL_SOURCE_CHECK_PENDING);
    CHECK(tb_symbol_source_check_step(&check, changed, 1) == TB_SYMBOL_SOURCE_CHECK_MATCH);
    tb_symbol_source_check_discard(&check);
    CHECK(!tb_symbol_source_check_view(&check, changed, &view));
    CHECK(tb_symbol_source_check_step(&check, changed, 1) == TB_SYMBOL_SOURCE_CHECK_INVALID);
    puts("PASS exact raw-source revisions, complete-byte checks, mismatch and recorded-only browsing");
    return 0;
}
