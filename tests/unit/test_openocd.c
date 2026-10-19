#define _POSIX_C_SOURCE 200809L
#include "openocd.h"
#include "../support/check.h"
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
static void file(const char *path)
{ int fd = open(path, O_CREAT | O_WRONLY | O_EXCL, 0600); CHECK(fd >= 0); CHECK(write(fd, "# metadata only\n", 16) == 16); CHECK(close(fd) == 0); }
int main(void)
{
    char root[] = "/tmp/the-bugger-m16-XXXXXX"; CHECK(mkdtemp(root));
    char board[1024], adapter[1024], target[1024], custom[1024], path[1024];
    (void)snprintf(path, sizeof path, "%s/board", root); CHECK(mkdir(path, 0700) == 0);
    (void)snprintf(board, sizeof board, "%s/board/selected board.cfg", root); file(board);
    (void)snprintf(path, sizeof path, "%s/interface", root); CHECK(mkdir(path, 0700) == 0);
    (void)snprintf(adapter, sizeof adapter, "%s/interface/literal;$(false).cfg", root); file(adapter);
    (void)snprintf(path, sizeof path, "%s/target", root); CHECK(mkdir(path, 0700) == 0);
    (void)snprintf(target, sizeof target, "%s/target/test.cfg", root); file(target);
    (void)snprintf(custom, sizeof custom, "%s/extra.cfg", root); file(custom);
    TbProfile *p = calloc(1, sizeof *p); TbOpenOcdPlan *plan = malloc(sizeof *plan); TbOpenOcdCatalog *catalog = malloc(sizeof *catalog); CHECK(p && plan && catalog);
    p->kind = 1; p->start_server = true;
    (void)snprintf(p->endpoint, sizeof p->endpoint, "localhost:3333");
    (void)snprintf(p->openocd, sizeof p->openocd, "/literal path/openocd");
    (void)snprintf(p->board, sizeof p->board, "%s", board);
    (void)snprintf(p->adapter, sizeof p->adapter, "%s", adapter);
    (void)snprintf(p->target, sizeof p->target, "%s", target);
    char error[256]; CHECK(tb_openocd_plan(p, plan, error, sizeof error));
    CHECK(plan->port == 3333 && plan->count == 11 && !strcmp(plan->argv[8], board));
    /* Board mode must not append either dependency, even when populated. */
    for (size_t i = 0; i < plan->count; ++i) { CHECK(strcmp(plan->argv[i], adapter) && strcmp(plan->argv[i], target)); }
    p->adapter_target = true; CHECK(tb_openocd_plan(p, plan, error, sizeof error));
    CHECK(plan->count == 13 && !strcmp(plan->argv[8], adapter) && !strcmp(plan->argv[10], target));
    p->search.count = 1; (void)snprintf(p->search.values[0], TB_PROFILE_WORD, "%s", root);
    p->scripts.count = 2; (void)snprintf(p->scripts.values[0], TB_PROFILE_WORD, "board/selected board.cfg"); (void)snprintf(p->scripts.values[1], TB_PROFILE_WORD, "extra.cfg");
    CHECK(tb_openocd_plan(p, plan, error, sizeof error)); CHECK(!strcmp(plan->argv[8], root) && !strcmp(plan->argv[10], "board/selected board.cfg") && !strcmp(plan->argv[12], "extra.cfg"));
    (void)snprintf(p->scripts.values[1], TB_PROFILE_WORD, "%s", p->scripts.values[0]); CHECK(!tb_openocd_plan(p, plan, error, sizeof error));
    (void)snprintf(p->scripts.values[1], TB_PROFILE_WORD, "missing.cfg"); CHECK(!tb_openocd_plan(p, plan, error, sizeof error));
    (void)snprintf(p->scripts.values[1], TB_PROFILE_WORD, "literal#bad.cfg"); CHECK(!tb_openocd_plan(p, plan, error, sizeof error));
    (void)snprintf(p->endpoint, sizeof p->endpoint, "external.example:3333"); CHECK(!tb_openocd_plan(p, plan, error, sizeof error));
    p->start_server = false; CHECK(tb_openocd_plan(p, plan, error, sizeof error) && plan->count == 0);
    tb_openocd_catalog(catalog, &p->search); bool got_board = false, got_adapter = false, got_target = false;
    for (size_t i = 0; i < catalog->count; ++i) {
        const TbOpenOcdScript *v = &catalog->scripts[i];
        got_board = got_board || (v->kind == 0 && !strcmp(v->path, board));
        got_adapter = got_adapter || (v->kind == 1 && !strcmp(v->path, adapter));
        got_target = got_target || (v->kind == 2 && !strcmp(v->path, target));
    }
    CHECK(got_board && got_adapter && got_target);
    CHECK(unlink(board) == 0 && unlink(adapter) == 0 && unlink(target) == 0 && unlink(custom) == 0);
    (void)snprintf(path, sizeof path, "%s/board", root); CHECK(rmdir(path) == 0);
    (void)snprintf(path, sizeof path, "%s/interface", root); CHECK(rmdir(path) == 0);
    (void)snprintf(path, sizeof path, "%s/target", root); CHECK(rmdir(path) == 0); CHECK(rmdir(root) == 0);
    free(p); free(plan); free(catalog);
    puts("PASS M16 installed script metadata, board/dependency exclusion, ordered explicit scripts/search paths, literal argv, validation and external-server configuration");
    return 0;
}
