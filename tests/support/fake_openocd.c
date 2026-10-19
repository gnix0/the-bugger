/* Authored lifecycle peer: never a real server or a physical-target emulator. */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
int main(int argc, char **argv)
{
    const char *mode = getenv("TB_FAKE_OPENOCD"); if (!mode) { mode = "ready"; }
    unsigned port = 0;
    for (int i = 1; i < argc; ++i) { if (sscanf(argv[i], "gdb_port %u", &port) == 1) { break; } }
    if (!port) { return 2; }
    if (!strcmp(mode, "exit")) { fputs("Error: probe unavailable or bad script\n", stderr); return 3; }
    if (!strcmp(mode, "ignore-term")) { (void)signal(SIGTERM, SIG_IGN); }
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    if (strcmp(mode, "hang")) { printf("Info : Listening on port %u for gdb connections\n", port); }
    if (!strcmp(mode, "crash")) { const struct timespec delay = {.tv_nsec = 200000000}; (void)nanosleep(&delay, NULL); return 4; }
    if (!strcmp(mode, "flood")) { for (;;) { puts("output flood \033[31m must be escaped"); } }
    for (;;) { const struct timespec delay = {.tv_nsec = 10000000}; (void)nanosleep(&delay, NULL); }
}
