/* Actual installed OpenOCD, intentional Tcl failure before adapter init; no hardware. */
#define _POSIX_C_SOURCE 200809L
#include "openocd.h"
#include "controller.h"
#include "../support/check.h"
#include <arpa/inet.h>
#include <poll.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>
static bool ordered;
static void log_line(void *context, const char *line)
{ (void)context; ordered = ordered || strstr(line, "M16 literal argument order verified") != NULL; }
static void write_file(const char *path, const char *text)
{ FILE *f = fopen(path, "w"); CHECK(f && fputs(text, f) >= 0 && fclose(f) == 0); }
int main(int argc, char **argv)
{
    CHECK(argc == 2);
    char root[] = "/tmp/tb m16-real-XXXXXX"; CHECK(mkdtemp(root));
    char first[512], second[512]; (void)snprintf(first, sizeof first, "%s/first config.cfg", root);
    (void)snprintf(second, sizeof second, "%s/second.cfg", root);
    write_file(first, "set tb_m16 42\n");
    write_file(second, "if {![info exists tb_m16]} {error {M16 wrong script order}}\nif {$tb_m16 != 42} {error {M16 wrong variable}}\nerror {M16 literal argument order verified}\n");
    int fd = socket(AF_INET, SOCK_STREAM, 0); CHECK(fd >= 0);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    CHECK(bind(fd, (struct sockaddr *)&addr, sizeof addr) == 0); socklen_t length = sizeof addr;
    CHECK(getsockname(fd, (struct sockaddr *)&addr, &length) == 0 && close(fd) == 0);
    TbProfile *p = calloc(1, sizeof *p); TbOpenOcdPlan *plan = malloc(sizeof *plan); CHECK(p && plan);
    p->start_server = true; (void)snprintf(p->openocd, sizeof p->openocd, "%s", argv[1]);
    (void)snprintf(p->endpoint, sizeof p->endpoint, "localhost:%u", ntohs(addr.sin_port));
    p->search.count = 1; (void)snprintf(p->search.values[0], TB_PROFILE_WORD, "%s", root);
    p->scripts.count = 2; (void)snprintf(p->scripts.values[0], TB_PROFILE_WORD, "first config.cfg"); (void)snprintf(p->scripts.values[1], TB_PROFILE_WORD, "second.cfg");
    char error[256]; CHECK(tb_openocd_plan(p, plan, error, sizeof error));
    TbOpenOcd *server = tb_openocd_create(log_line, NULL); CHECK(server && tb_openocd_start(server, plan));
    uint64_t until = tb_gdb_now_ms() + 2000;
    while (tb_openocd_active(server) && tb_gdb_now_ms() < until) { tb_openocd_pump(server); (void)poll(NULL, 0, 1); }
    CHECK(!tb_openocd_active(server) && tb_openocd_failed(server) && !tb_openocd_ready(server) && ordered);
    CHECK(tb_openocd_destroy(server)); free(p); free(plan);
    CHECK(unlink(first) == 0 && unlink(second) == 0 && rmdir(root) == 0);
    puts("PASS M16 actual OpenOCD ordered scripts/search paths with spaces, intentional pre-init Tcl error, bounded logs and owned child cleanup; no hardware exercised");
    return 0;
}
