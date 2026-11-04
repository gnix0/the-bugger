#ifndef THE_BUGGER_OPENOCD_H
#define THE_BUGGER_OPENOCD_H

#include "session/profiles.h"
#include <poll.h>
#include <stdint.h>
#include <sys/types.h>

enum {
    TB_OPENOCD_ARGS    = 96,
    TB_OPENOCD_SCRIPTS = 1536
};

typedef struct {
    char     argv[TB_OPENOCD_ARGS][TB_PROFILE_PATH];
    size_t   count;
    char     endpoint[256];
    unsigned port;
    bool     start_server;
} TbOpenOcdPlan_t;

typedef struct {
    char path[TB_PROFILE_PATH];
    int  kind;
} TbOpenOcdScript_t;

typedef struct {
    TbOpenOcdScript_t scripts[TB_OPENOCD_SCRIPTS];
    size_t            count;
    bool              truncated;
} TbOpenOcdCatalog_t;

/* Catalog only: never evaluates Tcl or infers a board's script dependencies. */
void tb_openocd_catalog(TbOpenOcdCatalog_t *catalog, const TbProfileWords_t *search);
bool tb_openocd_plan(const TbProfile_t *profile, TbOpenOcdPlan_t *plan, char *error, size_t size);
typedef void (*tb_openocd_log_fn)(void *context, const char *line);
typedef struct tb_openocd TbOpenOcd_t;

TbOpenOcd_t *tb_openocd_create(tb_openocd_log_fn log, void *context);
bool tb_openocd_destroy(TbOpenOcd_t *server);
bool tb_openocd_start(TbOpenOcd_t *server, const TbOpenOcdPlan_t *plan);
void tb_openocd_pump(TbOpenOcd_t *server);
void tb_openocd_stop(TbOpenOcd_t *server);
bool tb_openocd_active(const TbOpenOcd_t *server);
bool tb_openocd_ready(const TbOpenOcd_t *server);
bool tb_openocd_failed(const TbOpenOcd_t *server);
const char *tb_openocd_message(const TbOpenOcd_t *server);
int tb_openocd_fd(const TbOpenOcd_t *server);
#endif // THE_BUGGER_OPENOCD_H
