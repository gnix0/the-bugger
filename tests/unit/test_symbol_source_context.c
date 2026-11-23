#include "symbol/source_context.h"
#include "../support/check.h"

#include <stdio.h>
#include <string.h>

static TbSymbolSourceCandidate_t candidate(const unsigned char *recorded, const unsigned char *local)
{
    return (TbSymbolSourceCandidate_t) {
        .path = {
            .recorded        = recorded,
            .local           = local,
            .recorded_bytes  = strlen((const char *)recorded),
            .local_bytes     = strlen((const char *)local),
            .local_available = true
        },
        .status = TB_SYMBOL_SOURCE_PATH_COMPLETE
    };
}


static TbSymbolSourceContextStatus_e finish(TbSymbolSourceContext_t *context)
{
    TbSymbolSourceContextStatus_e status = context->status;
    for (size_t i = 0; i < 10000 && status == TB_SYMBOL_SOURCE_CONTEXT_PENDING; ++i) {
        size_t cursor = context->cursor;
        unsigned phase = context->phase;
        status = tb_symbol_source_context_step(context, 1);
        CHECK(context->phase != phase || context->cursor <= cursor + 1);
    }
    return status;
}


int main(void)
{
    TbSymbolSourceFileView_t file = {
        .line_version = 5,
        .reference = {
            .source = {
                .backing = {
                    .key = {
                        .device = 1,
                        .inode  = 2
                    },
                    .bytes = 4096
                },
                .section_index = 3,
                .unit_offset   = 100,
                .record_offset = 112
            }
        },
        .compilation = {
            .status = TB_SYMBOL_SOURCE_TEXT_READY
        }
    };
    TbSymbolTextView_t root = {
        .backing        = file.reference.source.backing,
        .source_section = 3,
        .source_unit    = 100,
        .source_record  = 112,
        .attribute = {
            .name = 0x1b
        }
    };
    TbSymbolSourceCandidate_t primary = candidate((const unsigned char *)"/one/shared.h",
                                                  (const unsigned char *)"/local/shared.h");
    TbSymbolSourceCandidate_t alternate = candidate((const unsigned char *)"/two/shared.h",
                                                    (const unsigned char *)"/local/shared.h");
    TbSymbolSourceContext_t context;
    TbSymbolSourceContextView_t view;
    CHECK(tb_symbol_source_context_init(&context, file, primary, &alternate, &root, SIZE_MAX) ==
          TB_SYMBOL_SOURCE_CONTEXT_PENDING);
    CHECK(!tb_symbol_source_context_view(&context, &view));
    CHECK(finish(&context) == TB_SYMBOL_SOURCE_CONTEXT_AMBIGUOUS);
    CHECK(tb_symbol_source_context_view(&context, &view) && view.selected == SIZE_MAX && view.count == 2);
    for (size_t choice = 0; choice < 2; ++choice) {
        CHECK(tb_symbol_source_context_init(&context, file, primary, &alternate, &root, choice) ==
              TB_SYMBOL_SOURCE_CONTEXT_PENDING);
        CHECK(finish(&context) == TB_SYMBOL_SOURCE_CONTEXT_READY);
        CHECK(tb_symbol_source_context_view(&context, &view) && view.selected == choice);
        CHECK(view.unit_directory.source_unit == 100 && view.file.reference.source.record_offset == 112);
    }

    unsigned char long_path[1024];
    memset(long_path, 'x', sizeof(long_path));
    long_path[0] = '/';
    long_path[1023] = 0;
    primary = candidate(long_path, long_path);
    alternate = primary;
    CHECK(tb_symbol_source_context_init(&context, file, primary, &alternate, &root, SIZE_MAX) ==
          TB_SYMBOL_SOURCE_CONTEXT_PENDING);
    CHECK(tb_symbol_source_context_step(&context, 256) == TB_SYMBOL_SOURCE_CONTEXT_PENDING);
    CHECK(context.cursor == 256);
    CHECK(finish(&context) == TB_SYMBOL_SOURCE_CONTEXT_READY);
    CHECK(tb_symbol_source_context_view(&context, &view) && view.selected == 0 && view.count == 2);
    alternate.path.local = (const unsigned char *)"/different";
    alternate.path.local_bytes = 10;
    CHECK(tb_symbol_source_context_init(&context, file, primary, &alternate, &root, SIZE_MAX) ==
          TB_SYMBOL_SOURCE_CONTEXT_PENDING);
    CHECK(finish(&context) == TB_SYMBOL_SOURCE_CONTEXT_AMBIGUOUS);

    alternate.status = TB_SYMBOL_SOURCE_PATH_DEPENDENCY;
    alternate.path.local_available = false;
    CHECK(tb_symbol_source_context_init(&context, file, primary, &alternate, &root, SIZE_MAX) ==
          TB_SYMBOL_SOURCE_CONTEXT_PENDING);
    CHECK(finish(&context) == TB_SYMBOL_SOURCE_CONTEXT_DEPENDENCY);
    CHECK(tb_symbol_source_context_init(&context, file, primary, &alternate, &root, 0) ==
          TB_SYMBOL_SOURCE_CONTEXT_PENDING);
    CHECK(finish(&context) == TB_SYMBOL_SOURCE_CONTEXT_READY);
    alternate.status = TB_SYMBOL_SOURCE_PATH_AMBIGUOUS;
    CHECK(tb_symbol_source_context_init(&context, file, primary, &alternate, &root, SIZE_MAX) ==
          TB_SYMBOL_SOURCE_CONTEXT_PENDING);
    CHECK(finish(&context) == TB_SYMBOL_SOURCE_CONTEXT_AMBIGUOUS);
    CHECK(tb_symbol_source_context_init(&context, file, primary, NULL, &root, SIZE_MAX) ==
          TB_SYMBOL_SOURCE_CONTEXT_PENDING);
    CHECK(finish(&context) == TB_SYMBOL_SOURCE_CONTEXT_READY);

    TbSymbolSourceCandidate_t missing = primary;
    missing.status = TB_SYMBOL_SOURCE_PATH_DEPENDENCY;
    missing.path.local_available = false;
    file.compilation.status = TB_SYMBOL_SOURCE_TEXT_UNSUPPORTED;
    CHECK(tb_symbol_source_context_init(&context, file, missing, &primary, &root, SIZE_MAX) ==
          TB_SYMBOL_SOURCE_CONTEXT_PENDING);
    CHECK(finish(&context) == TB_SYMBOL_SOURCE_CONTEXT_DEPENDENCY);
    CHECK(tb_symbol_source_context_init(&context, file, missing, &primary, &root, 1) ==
          TB_SYMBOL_SOURCE_CONTEXT_PENDING);
    CHECK(finish(&context) == TB_SYMBOL_SOURCE_CONTEXT_READY);
    CHECK(tb_symbol_source_context_view(&context, &view) && view.selected == 1);
    file.compilation.status = TB_SYMBOL_SOURCE_TEXT_READY;

    TbSymbolSourceContext_t before = context;
    ++root.source_unit;
    CHECK(tb_symbol_source_context_init(&context, file, primary, NULL, &root, SIZE_MAX) ==
          TB_SYMBOL_SOURCE_CONTEXT_INVALID);
    CHECK(!memcmp(&before, &context, sizeof(context)));
    --root.source_unit;
    CHECK(tb_symbol_source_context_init(&context, file, primary, &alternate, NULL, SIZE_MAX) ==
          TB_SYMBOL_SOURCE_CONTEXT_INVALID);
    CHECK(tb_symbol_source_context_init(&context, file, primary, NULL, &root, 1) ==
          TB_SYMBOL_SOURCE_CONTEXT_INVALID);
    CHECK(tb_symbol_source_context_step(&context, 257) == TB_SYMBOL_SOURCE_CONTEXT_INVALID);
    tb_symbol_source_context_discard(&context);
    CHECK(!tb_symbol_source_context_view(&context, &view));
    puts("PASS full-path compilation-context agreement, explicit choices and retained provenance");
    return 0;
}
