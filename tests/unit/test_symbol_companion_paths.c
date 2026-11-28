#include "symbol/companion_paths.h"
#include "../support/check.h"

#include <stdio.h>
#include <string.h>

static TbSymbolSourcePathText_t text(const char *value)
{
    return (TbSymbolSourcePathText_t) {
        .data  = (const unsigned char *)value,
        .bytes = strlen(value)
    };
}


static TbSymbolCompanionPathStatus_e finish(TbSymbolCompanionPaths_t *paths, size_t slice)
{
    for (size_t i = 0; i < 100000; ++i) {
        TbSymbolCompanionPathStatus_e status = tb_symbol_companion_paths_step(paths, slice);
        if (status != TB_SYMBOL_COMPANION_PATH_PENDING)
            return status;
    }
    CHECK(false);
    return TB_SYMBOL_COMPANION_PATH_INVALID;
}


static void inventory(size_t slice)
{
    unsigned char id[] = {0, 0xab, 0xff};
    TbSymbolSourcePathText_t roots[] = {text("/usr/lib/debug"), text("/symbols/")};
    TbSymbolCompanionPathsRequest_t request = {
        .origin     = text("/tmp/a b/prog"),
        .debuglink  = text("prog.debug"),
        .build_id = {
            .data  = id,
            .bytes = sizeof(id)
        },
        .roots      = roots,
        .root_count = 2
    };
    const char *expected[] = {
        "/tmp/a b/prog.debug", "/tmp/a b/.debug/prog.debug",
        "/usr/lib/debug/tmp/a b/prog.debug", "/symbols/tmp/a b/prog.debug",
        "/usr/lib/debug/.build-id/00/abff.debug", "/symbols/.build-id/00/abff.debug"
    };
    unsigned char output[TB_SYMBOL_COMPANION_PATH_BYTES];
    TbSymbolCompanionPaths_t paths;
    CHECK(tb_symbol_companion_paths_init(&paths, request, output, sizeof(output)) ==
          TB_SYMBOL_COMPANION_PATH_PENDING);
    TbSymbolCompanionPathView_t view;
    CHECK(!tb_symbol_companion_paths_view(&paths, &view));
    CHECK(!tb_symbol_companion_paths_next(&paths));
    for (size_t i = 0; i < sizeof(expected) / sizeof(expected[0]); ++i) {
        CHECK(finish(&paths, slice) == TB_SYMBOL_COMPANION_PATH_READY);
        CHECK(tb_symbol_companion_paths_view(&paths, &view) && view.ordinal == i);
        CHECK(view.bytes == strlen(expected[i]) && strcmp((const char *)view.path, expected[i]) == 0);
        CHECK(view.root == (i < 2 ? SIZE_MAX : (i - 2) % 2));
        CHECK(view.kind == (i == 0 ? TB_SYMBOL_COMPANION_BESIDE : i == 1 ? TB_SYMBOL_COMPANION_DOT_DEBUG :
                           i < 4 ? TB_SYMBOL_COMPANION_MIRROR : TB_SYMBOL_COMPANION_BUILD_ID));
        CHECK(tb_symbol_companion_paths_step(&paths, slice) == TB_SYMBOL_COMPANION_PATH_READY);
        CHECK(tb_symbol_companion_paths_next(&paths));
        CHECK(!tb_symbol_companion_paths_view(&paths, &view));
    }
    CHECK(finish(&paths, slice) == TB_SYMBOL_COMPANION_PATH_END);
    tb_symbol_companion_paths_discard(&paths);
    CHECK(!tb_symbol_companion_paths_view(&paths, &view));
}


static void boundaries(void)
{
    unsigned char output[TB_SYMBOL_COMPANION_PATH_BYTES];
    TbSymbolSourcePathText_t roots[TB_SYMBOL_COMPANION_ROOTS];
    for (size_t i = 0; i < TB_SYMBOL_COMPANION_ROOTS; ++i)
        roots[i] = text("/");
    unsigned char id[] = {0xff, 0};
    TbSymbolCompanionPathsRequest_t request = {
        .origin     = text("/p"),
        .debuglink  = text("x"),
        .build_id = {
            .data  = id,
            .bytes = sizeof(id)
        },
        .roots      = roots,
        .root_count = TB_SYMBOL_COMPANION_ROOTS
    };
    TbSymbolCompanionPaths_t paths;
    CHECK(tb_symbol_companion_paths_init(&paths, request, output, sizeof(output)) ==
          TB_SYMBOL_COMPANION_PATH_PENDING);
    size_t count = 0;
    while (finish(&paths, 256) == TB_SYMBOL_COMPANION_PATH_READY) {
        ++count;
        CHECK(tb_symbol_companion_paths_next(&paths));
    }
    CHECK(count == 66 && paths.status == TB_SYMBOL_COMPANION_PATH_END);
    ++request.root_count;
    CHECK(tb_symbol_companion_paths_init(&paths, request, output, sizeof(output)) ==
          TB_SYMBOL_COMPANION_PATH_RESOURCE);
    request.root_count = 0;
    request.build_id.bytes = 0;
    for (size_t capacity = 1; capacity <= 3; ++capacity) {
        CHECK(tb_symbol_companion_paths_init(&paths, request, output, capacity) ==
              TB_SYMBOL_COMPANION_PATH_PENDING);
        CHECK(finish(&paths, 1) == (capacity == 3 ? TB_SYMBOL_COMPANION_PATH_READY :
                                                                 TB_SYMBOL_COMPANION_PATH_RESOURCE));
        TbSymbolCompanionPathView_t view;
        CHECK(tb_symbol_companion_paths_view(&paths, &view) == (capacity == 3));
    }
    request.debuglink = text("");
    CHECK(tb_symbol_companion_paths_init(&paths, request, output, sizeof(output)) ==
          TB_SYMBOL_COMPANION_PATH_PENDING);
    CHECK(finish(&paths, 1) == TB_SYMBOL_COMPANION_PATH_END);

    const char *bad[] = {".", "..", "a/b", "a\nb", "a\rb"};
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); ++i) {
        request.debuglink = text(bad[i]);
        CHECK(tb_symbol_companion_paths_init(&paths, request, output, sizeof(output)) ==
              TB_SYMBOL_COMPANION_PATH_PENDING);
        CHECK(finish(&paths, 1) == TB_SYMBOL_COMPANION_PATH_UNSUPPORTED);
    }
    unsigned char embedded[] = {'x', 0, 'z'};
    request.debuglink = (TbSymbolSourcePathText_t) {
        .data  = embedded,
        .bytes = sizeof(embedded)
    };
    CHECK(tb_symbol_companion_paths_init(&paths, request, output, sizeof(output)) ==
          TB_SYMBOL_COMPANION_PATH_PENDING);
    CHECK(finish(&paths, 256) == TB_SYMBOL_COMPANION_PATH_UNSUPPORTED);
    request.debuglink = text("x");
    request.origin = text("relative/p");
    CHECK(tb_symbol_companion_paths_init(&paths, request, output, sizeof(output)) ==
          TB_SYMBOL_COMPANION_PATH_UNSUPPORTED);
    request.origin = text("/dir/");
    CHECK(tb_symbol_companion_paths_init(&paths, request, output, sizeof(output)) ==
          TB_SYMBOL_COMPANION_PATH_PENDING);
    CHECK(finish(&paths, 256) == TB_SYMBOL_COMPANION_PATH_UNSUPPORTED);
    request.origin = text("/p");
    request.root_count = 2;
    roots[1] = text("relative");
    CHECK(tb_symbol_companion_paths_init(&paths, request, output, sizeof(output)) ==
          TB_SYMBOL_COMPANION_PATH_PENDING);
    CHECK(finish(&paths, 256) == TB_SYMBOL_COMPANION_PATH_INVALID);

    unsigned char huge[TB_SYMBOL_COMPANION_ID_BYTES] = {0};
    request.root_count = 1;
    request.debuglink = text("");
    request.build_id = (TbSymbolSourcePathText_t) {
        .data  = huge,
        .bytes = sizeof(huge)
    };
    CHECK(tb_symbol_companion_paths_init(&paths, request, output, sizeof(output)) ==
          TB_SYMBOL_COMPANION_PATH_PENDING);
    CHECK(finish(&paths, 256) == TB_SYMBOL_COMPANION_PATH_RESOURCE);
    request.build_id.bytes = 1;
    CHECK(tb_symbol_companion_paths_init(&paths, request, output, sizeof(output)) ==
          TB_SYMBOL_COMPANION_PATH_PENDING);
    CHECK(finish(&paths, 1) == TB_SYMBOL_COMPANION_PATH_READY);
    CHECK(strcmp((const char *)output, "/.build-id/00/.debug") == 0);
}


int main(void)
{
    inventory(1);
    inventory(17);
    inventory(256);
    boundaries();
    puts("PASS bounded GNU companion path order, roots, byte profiles and exhaustion");
    return 0;
}
