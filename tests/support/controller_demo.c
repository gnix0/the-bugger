#define _POSIX_C_SOURCE 200809L
#include "controller.h"

#include <errno.h>
#include <inttypes.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* Diagnostic harness only: handlers request normal controller cleanup. */
static volatile sig_atomic_t interrupted;
static size_t streams;

static void stop_signal(int number) { interrupted = number; }

static const char *state_name(TbGdbState state)
{
    static const char *const names[] = {"disconnected", "starting", "ready (no target)",
        "FAKE running", "FAKE stopped", "FAKE target exited", "closing"};
    return names[state];
}

static void printable(const char *text, size_t length)
{
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (c >= 32 && c < 127) { (void)putchar(c); }
        else { (void)printf("\\%03o", (unsigned)c); }
    }
}

static void emit(void *context, const TbGdbEvent *event)
{
    TbGdb **gdb = context;
    if (event->kind == TB_GDB_STATE) {
        printf("state: %s\n", state_name(tb_gdb_state(*gdb)));
    } else if (event->kind == TB_GDB_STDERR) {
        if (++streams <= 4) {
            size_t shown = event->length < 160 ? event->length : 160;
            fputs("peer stderr: ", stdout); printable(event->text, shown);
            if (shown < event->length) { printf(" ... (%zu bytes total)", event->length); }
            putchar('\n');
        }
    } else if (event->kind == TB_GDB_DIAGNOSTIC) {
        fputs("diagnostic: ", stdout); printable(event->text, event->length);
        if (event->status != 0) { printf(" (%s)", strerror(event->status)); }
        putchar('\n');
    } else if (event->kind == TB_GDB_CHILD_EXIT) {
        printf("owned child reaped: wait status %d\n", event->status);
    } else if (event->kind == TB_GDB_REPLY || event->kind == TB_GDB_STALE ||
               event->kind == TB_GDB_TIMEOUT || event->kind == TB_GDB_CANCELLED) {
        const char *name = event->kind == TB_GDB_REPLY ? "reply" : event->kind == TB_GDB_STALE ? "stale" :
                           event->kind == TB_GDB_TIMEOUT ? "timeout" : "cancelled";
        printf("%s: token=%" PRIu64 " cookie=%" PRIu64 " session=%" PRIu64,
               name, event->token, event->cookie, event->generation.session);
        if (event->record != NULL && event->record->klass != NULL) { printf(" class=%s", event->record->klass); }
        putchar('\n');
    }
    (void)fflush(stdout);
}

static void command(TbGdb *gdb, const char *line, uint64_t *cookie)
{
    static const char *const verbs[] = {"stop", "run", "hold", "release", "flood", "error", "die",
                                      "hold-context", "hold-stop"};
    if (strcmp(line, "quit") == 0 || strcmp(line, "close") == 0) { tb_gdb_close(gdb); return; }
    if (strcmp(line, "context") == 0) {
        tb_gdb_context_changed(gdb); puts("selected context invalidated"); return;
    }
    for (size_t i = 0; i < sizeof verbs / sizeof verbs[0]; ++i) {
        if (strcmp(line, verbs[i]) == 0) {
            TbGdbScope scope = i == 7 ? TB_GDB_CONTEXT : i == 8 ? TB_GDB_STOP : TB_GDB_SESSION;
            char operation[32]; (void)snprintf(operation, sizeof operation, "-%s", i >= 7 ? "hold" : verbs[i]);
            if (!tb_gdb_request(gdb, operation, NULL, 0, scope, ++*cookie, i >= 7 ? 10000 : 2000, NULL)) {
                puts("request rejected: wait for ready or inspect diagnostics");
            }
            return;
        }
    }
    puts("Commands: stop run hold hold-context hold-stop release flood error die context close quit");
}

int main(int argc, char *argv[])
{
    if (argc < 2 || argc > 3) {
        fputs("Usage: controller-demo PATH-TO-FAKE-MI [MODE]\n", stderr); return 2;
    }
    if (setenv("TB_FAKE_MODE", argc == 3 ? argv[2] : "normal", 1) != 0) { perror("setenv"); return 2; }
    struct sigaction action = {.sa_handler = stop_signal};
    (void)sigemptyset(&action.sa_mask);
    if (sigaction(SIGINT, &action, NULL) != 0 || sigaction(SIGTERM, &action, NULL) != 0 ||
        sigaction(SIGHUP, &action, NULL) != 0) { perror("sigaction"); return 2; }
    puts("M04 FAKE MI controller demonstration: no debugger target or hardware.\n"
         "Wait for ready, then type commands and Enter. Ctrl+C closes and reaps.\n"
         "Commands: stop run hold hold-context hold-stop release flood error die context close quit");
    TbGdb *gdb = tb_gdb_create(emit, &gdb);
    if (gdb == NULL) { return 2; }
    if (!tb_gdb_start(gdb, argv[1], 1500)) { (void)tb_gdb_destroy(gdb); return 1; }
    char line[128]; size_t used = 0; uint64_t cookie = 0;
    bool input_open = true;
    while (tb_gdb_state(gdb) != TB_GDB_DISCONNECTED) {
        if (interrupted) { tb_gdb_close(gdb); }
        struct pollfd fds[4];
        bool accept_input = input_open && tb_gdb_state(gdb) != TB_GDB_STARTING;
        fds[0] = (struct pollfd){accept_input ? STDIN_FILENO : -1, POLLIN, 0};
        (void)tb_gdb_pollfds(gdb, fds + 1);
        int ready = poll(fds, 4, tb_gdb_timeout(gdb, 25));
        if (ready < 0 && errno != EINTR) { perror("poll"); tb_gdb_close(gdb); }
        tb_gdb_pump(gdb, 0);
        if (fds[0].revents != 0) {
            char bytes[256]; ssize_t count = read(STDIN_FILENO, bytes, sizeof bytes);
            if (count <= 0) { input_open = false; tb_gdb_close(gdb); }
            for (ssize_t i = 0; i < count; ++i) {
                if (bytes[i] == '\n') {
                    line[used] = '\0'; command(gdb, line, &cookie); used = 0;
                } else if (used + 1 < sizeof line) { line[used++] = bytes[i]; }
                else { used = 0; puts("input too long; use a listed command"); }
            }
        }
    }
    printf("cleanup: disconnected, no owned child; stderr chunks seen=%zu\n", streams);
    if (!tb_gdb_destroy(gdb)) { return 1; }
    return interrupted ? 128 + interrupted : 0;
}
