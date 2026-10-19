#ifndef THE_BUGGER_OPENOCD_H
#define THE_BUGGER_OPENOCD_H
#include "profiles.h"
#include <poll.h>
#include <stdint.h>
#include <sys/types.h>

enum { TB_OPENOCD_ARGS = 96, TB_OPENOCD_SCRIPTS = 1536 };
typedef struct {
    char argv[TB_OPENOCD_ARGS][TB_PROFILE_PATH]; size_t count;
    char endpoint[256]; unsigned port; bool start_server;
} TbOpenOcdPlan;
typedef struct { char path[TB_PROFILE_PATH]; int kind; } TbOpenOcdScript;
typedef struct { TbOpenOcdScript scripts[TB_OPENOCD_SCRIPTS]; size_t count; bool truncated; } TbOpenOcdCatalog;
/* Catalog only: never evaluates Tcl or infers a board's script dependencies. */
void tb_openocd_catalog(TbOpenOcdCatalog *catalog, const TbProfileWords *search);
bool tb_openocd_plan(const TbProfile *profile, TbOpenOcdPlan *plan, char *error, size_t size);
typedef void (*TbOpenOcdLog)(void *context, const char *line);
typedef struct TbOpenOcd TbOpenOcd;
TbOpenOcd *tb_openocd_create(TbOpenOcdLog log, void *context);
bool tb_openocd_destroy(TbOpenOcd *server);
bool tb_openocd_start(TbOpenOcd *server, const TbOpenOcdPlan *plan);
void tb_openocd_pump(TbOpenOcd *server);
void tb_openocd_stop(TbOpenOcd *server);
bool tb_openocd_active(const TbOpenOcd *server);
bool tb_openocd_ready(const TbOpenOcd *server);
bool tb_openocd_failed(const TbOpenOcd *server);
const char *tb_openocd_message(const TbOpenOcd *server);
int tb_openocd_fd(const TbOpenOcd *server);
#endif
