#include "symbol/source_path.h"
#include "../support/check.h"

#include <stdio.h>
#include <string.h>

static unsigned char recorded[8192];
static unsigned char local[8192];
static TbSymbolSourcePath_t path;

static TbSymbolSourcePathText_t text(const char *value)
{
    return (TbSymbolSourcePathText_t) {
        .data  = (const unsigned char *)value,
        .bytes = value ? strlen(value) : 0
    };
}


static TbSymbolSourcePathStatus_e run(TbSymbolSourcePathRequest_t request, size_t capacity)
{
    TbSymbolSourcePathStatus_e status = tb_symbol_source_path_init(&path, request, recorded, capacity,
                                                                  local, capacity);
    while (status == TB_SYMBOL_SOURCE_PATH_PENDING) {
        uint64_t before = path.progress;
        status = tb_symbol_source_path_step(&path, 7);
        CHECK(path.progress > before && path.progress - before <= 7);
    }
    return status;
}


static void composition(void)
{
    TbSymbolSourcePathRequest_t request = {
        .file          = text("file name.c"),
        .directory     = text("src/../include"),
        .compilation   = text("/build/./project"),
        .selected_rule = SIZE_MAX
    };
    CHECK(run(request, sizeof(recorded)) == TB_SYMBOL_SOURCE_PATH_COMPLETE);
    CHECK(!strcmp((char *)recorded, "/build/./project/src/../include/file name.c"));
    CHECK(!strcmp((char *)recorded, (char *)local));
    request.directory = text("/absolute//dir");
    CHECK(run(request, sizeof(recorded)) == TB_SYMBOL_SOURCE_PATH_COMPLETE);
    CHECK(!strcmp((char *)local, "/absolute//dir/file name.c"));
    request.directory_current = true;
    CHECK(run(request, sizeof(recorded)) == TB_SYMBOL_SOURCE_PATH_COMPLETE);
    CHECK(!strcmp((char *)local, "/build/./project/file name.c"));
    request.compilation = text("relative/build");
    CHECK(run(request, sizeof(recorded)) == TB_SYMBOL_SOURCE_PATH_DEPENDENCY);
    request.image_directory = text("/image/origin");
    CHECK(run(request, sizeof(recorded)) == TB_SYMBOL_SOURCE_PATH_COMPLETE);
    CHECK(!strcmp((char *)local, "/image/origin/relative/build/file name.c"));
    request.compilation = text(NULL);
    CHECK(run(request, sizeof(recorded)) == TB_SYMBOL_SOURCE_PATH_DEPENDENCY);
    CHECK(!strcmp((char *)recorded, "file name.c"));
    request.directory_unknown = true;
    request.file = text("/absolute/file.c");
    CHECK(run(request, sizeof(recorded)) == TB_SYMBOL_SOURCE_PATH_COMPLETE);
    CHECK(!strcmp((char *)local, "/absolute/file.c"));
}


static void mappings(void)
{
    TbSymbolSourcePathRule_t rules[] = {
        {
            .from = text("/build"),
            .to   = text("/one")
        },
        {
            .from = text("/build/src"),
            .to   = text("/two")
        },
        {
            .from = text("/build/src/"),
            .to   = text("/three")
        }
    };
    TbSymbolSourcePathRequest_t request = {
        .file          = text("/build/src/file.c"),
        .rules         = rules,
        .rule_count    = 3,
        .selected_rule = SIZE_MAX
    };
    CHECK(run(request, sizeof(recorded)) == TB_SYMBOL_SOURCE_PATH_AMBIGUOUS);
    TbSymbolSourcePathView_t view;
    CHECK(tb_symbol_source_path_view(&path, &view) && view.matching_rules == 3 && !view.local_available);
    CHECK(view.first_rule == 0 && view.second_rule == 1);
    for (size_t i = 0; i < 3; ++i) {
        request.selected_rule = i;
        CHECK(run(request, sizeof(recorded)) == TB_SYMBOL_SOURCE_PATH_COMPLETE);
        CHECK(tb_symbol_source_path_view(&path, &view) && view.mapped && view.selected_rule == i);
    }
    CHECK(!strcmp((char *)local, "/three/file.c"));
    TbSymbolSourcePathRule_t duplicate[] = {rules[0], rules[0]};
    request.rules = duplicate;
    request.rule_count = 2;
    request.selected_rule = SIZE_MAX;
    CHECK(run(request, sizeof(recorded)) == TB_SYMBOL_SOURCE_PATH_AMBIGUOUS);
    request.rules = rules;
    request.rule_count = 3;
    request.file = text("/builder/file.c");
    request.selected_rule = SIZE_MAX;
    CHECK(run(request, sizeof(recorded)) == TB_SYMBOL_SOURCE_PATH_COMPLETE);
    CHECK(tb_symbol_source_path_view(&path, &view) && !view.matching_rules && !view.mapped);
    request.selected_rule = 0;
    CHECK(run(request, sizeof(recorded)) == TB_SYMBOL_SOURCE_PATH_DEPENDENCY);
    rules[0].from = text("relative");
    rules[0].to = text("/mapped");
    request.file = text("relative/file.c");
    request.rule_count = 1;
    CHECK(run(request, sizeof(recorded)) == TB_SYMBOL_SOURCE_PATH_COMPLETE);
    CHECK(!strcmp((char *)local, "/mapped/file.c"));
    rules[0].from = text("/");
    rules[0].to = text("/root");
    request.file = text("/file.c");
    CHECK(run(request, sizeof(recorded)) == TB_SYMBOL_SOURCE_PATH_COMPLETE);
    CHECK(!strcmp((char *)local, "/root/file.c"));
}


static void failures(void)
{
    TbSymbolSourcePathRequest_t request = {
        .file          = text("/abc"),
        .selected_rule = SIZE_MAX
    };
    CHECK(run(request, 5) == TB_SYMBOL_SOURCE_PATH_COMPLETE);
    CHECK(run(request, 4) == TB_SYMBOL_SOURCE_PATH_RESOURCE);
    TbSymbolSourcePathView_t view = {0};
    CHECK(!tb_symbol_source_path_view(&path, &view));
    const unsigned char binary[] = {'/', 'a', 0, 'b'};
    request.file = (TbSymbolSourcePathText_t) {
        .data  = binary,
        .bytes = sizeof(binary)
    };
    CHECK(run(request, sizeof(recorded)) == TB_SYMBOL_SOURCE_PATH_INVALID);
    request.file = text("/bad\npath");
    CHECK(run(request, sizeof(recorded)) == TB_SYMBOL_SOURCE_PATH_INVALID);
    request.file = text("/bad\rpath");
    CHECK(run(request, sizeof(recorded)) == TB_SYMBOL_SOURCE_PATH_INVALID);
    request.file = text("/abc");
    CHECK(tb_symbol_source_path_init(&path, request, recorded, sizeof(recorded), local, 4) ==
          TB_SYMBOL_SOURCE_PATH_PENDING);
    while (path.status == TB_SYMBOL_SOURCE_PATH_PENDING)
        (void)tb_symbol_source_path_step(&path, 256);
    CHECK(path.status == TB_SYMBOL_SOURCE_PATH_RESOURCE);
    CHECK(tb_symbol_source_path_init(&path, request, recorded, sizeof(recorded), local, sizeof(local)) ==
          TB_SYMBOL_SOURCE_PATH_PENDING);
    TbSymbolSourcePath_t saved = path;
    CHECK(tb_symbol_source_path_step(&path, 0) == TB_SYMBOL_SOURCE_PATH_INVALID);
    CHECK(!memcmp(&path, &saved, sizeof(path)));
    CHECK(tb_symbol_source_path_step(&path, 257) == TB_SYMBOL_SOURCE_PATH_INVALID);
    CHECK(!memcmp(&path, &saved, sizeof(path)));
    tb_symbol_source_path_discard(&path);
    CHECK(!tb_symbol_source_path_view(&path, &view));

    static unsigned char long_name[4097];
    memset(long_name, 'x', sizeof(long_name));
    long_name[0] = '/';
    request.file = (TbSymbolSourcePathText_t) {
        .data  = long_name,
        .bytes = sizeof(long_name)
    };
    CHECK(run(request, 4096) == TB_SYMBOL_SOURCE_PATH_RESOURCE);
    CHECK(run(request, sizeof(recorded)) == TB_SYMBOL_SOURCE_PATH_COMPLETE);
    CHECK(path.view.local_bytes == sizeof(long_name) && path.progress > 8192);
}


static void bounded_rules(void)
{
    static TbSymbolSourcePathRule_t rules[520];
    for (size_t i = 0; i < 520; ++i) {
        rules[i] = (TbSymbolSourcePathRule_t) {
            .from = text("/none"),
            .to   = text("/other")
        };
    }
    TbSymbolSourcePathRequest_t request = {
        .file          = text("/actual/file.c"),
        .rules         = rules,
        .rule_count    = 520,
        .selected_rule = SIZE_MAX
    };
    CHECK(run(request, sizeof(recorded)) == TB_SYMBOL_SOURCE_PATH_COMPLETE);
    CHECK(path.progress > 3000 && !path.view.matching_rules);
    CHECK(tb_symbol_source_path_init(&path, request, recorded, sizeof(recorded), local, sizeof(local)) ==
          TB_SYMBOL_SOURCE_PATH_PENDING);
    CHECK(tb_symbol_source_path_step(&path, 1) == TB_SYMBOL_SOURCE_PATH_PENDING);
    tb_symbol_source_path_discard(&path);
    TbSymbolSourcePathView_t view;
    CHECK(!tb_symbol_source_path_view(&path, &view));
}


int main(void)
{
    composition();
    mappings();
    failures();
    bounded_rules();
    puts("PASS source paths, explicit context/mappings, ambiguity, byte preservation and capacity failures");
    return 0;
}
