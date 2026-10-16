/* Deterministic test peer, never a debugger. No target is launched. */
#define _POSIX_C_SOURCE 200809L
#include "mi.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

static void delay(unsigned milliseconds)
{
    struct timespec wait = {(time_t)(milliseconds / 1000), (long)(milliseconds % 1000) * 1000000};
    while (nanosleep(&wait, &wait) != 0 && errno == EINTR) { }
}

static void reply(const char *token, const char *body)
{
    (void)printf("%s%s\n", token, body);
    (void)fflush(stdout);
}

int main(int argc, char *argv[])
{
    if (argc != 4 || strcmp(argv[1], "--quiet") != 0 || strcmp(argv[2], "--nx") != 0 ||
        (strcmp(argv[3], "--interpreter=mi3") != 0 && strcmp(argv[3], "--interpreter=mi2") != 0)) {
        return 2;
    }
    const char *mode = getenv("TB_FAKE_MODE");
    if (mode == NULL) { mode = "normal"; }
    if ((strcmp(mode, "fallback") == 0 || strcmp(mode, "fallback-fail") == 0) &&
        strcmp(argv[3], "--interpreter=mi3") == 0) {
        fputs("Interpreter `mi3' unrecognized\n", stderr); return 0;
    }
    if (strcmp(mode, "fallback-fail") == 0) {
        fputs("Interpreter mi2 unrecognized\n", stderr); return 0;
    }
    if (strcmp(mode, "exit") == 0) { fputs("FAKE startup failure\n", stderr); return 7; }
    if (strcmp(mode, "signal") == 0) { (void)raise(SIGTERM); return 8; }
    if (strcmp(mode, "hang") == 0 || strcmp(mode, "stubborn") == 0) {
        if (strcmp(mode, "stubborn") == 0) { (void)signal(SIGTERM, SIG_IGN); }
        for (;;) { pause(); }
    }
    if (strcmp(mode, "inherited-fd") == 0) {
        if (getpgrp() != getpid()) { return 9; }
        for (int fd = 3; fd < 256; ++fd) {
            if (fcntl(fd, F_GETFD) >= 0) { fputs("inherited descriptor\n", stderr); return 9; }
        }
    }
    if (strcmp(mode, "fragment") == 0) {
        const char *prompt = "(gdb) \r\n";
        for (size_t i = 0; prompt[i] != '\0'; ++i) { (void)putchar(prompt[i]); (void)fflush(stdout); delay(1); }
    } else { puts("(gdb)"); (void)fflush(stdout); }
    char *line = NULL;
    size_t capacity = 0;
    char held[32] = {0};
    unsigned setup = 0;
    while (getline(&line, &capacity, stdin) >= 0) {
        size_t digits = strspn(line, "0123456789");
        if (digits == 0 || digits >= sizeof held) { free(line); return 3; }
        char token[32];
        memcpy(token, line, digits); token[digits] = '\0';
        char *command = line + digits;
        if (strncmp(command, "-gdb-set ", 9) == 0) {
            if (strcmp(mode, "setup-error") == 0) { reply(token, "^error,msg=\"FAKE unsupported setting\""); }
            else { reply(token, "^done"); }
            if (++setup == 2 && strcmp(mode, "no-read") == 0) { delay(400); }
            if (setup == 2 && strcmp(mode, "late-fallback") == 0) {
                fputs("Interpreter mi3 unrecognized\n", stderr); free(line); return 7;
            }
        } else if (strcmp(command, "-hold\n") == 0) {
            (void)snprintf(held, sizeof held, "%s", token);
        } else if (strcmp(command, "-release\n") == 0) {
            reply(token, "^done,order=\"second-first\"");
            if (held[0] != '\0') { reply(held, "^done,order=\"held\""); held[0] = '\0'; }
        } else if (strcmp(command, "-stop\n") == 0) {
            reply(token, "^done"); puts("*stopped,reason=\"breakpoint-hit\",thread-id=\"1\"");
        } else if (strcmp(command, "-run\n") == 0) {
            reply(token, "^running"); puts("*running,thread-id=\"all\"");
            puts("*running,thread-id=\"all\"");
        } else if (strcmp(command, "-reordered-run\n") == 0) {
            puts("*running,thread-id=\"all\"\n*stopped,reason=\"end-stepping-range\"");
            reply(token, "^running");
        } else if (strcmp(command, "-target-exit\n") == 0) {
            reply(token, "^done"); puts("*stopped,reason=\"exited-normally\"");
        } else if (strcmp(command, "-error\n") == 0) {
            reply(token, "^error,msg=\"FAKE command error\"");
        } else if (strcmp(command, "-unknown\n") == 0) {
            puts("999999^done"); reply(token, "^done");
        } else if (strcmp(command, "-flood\n") == 0) {
            char output[1024];
            memset(output, 'x', sizeof output); output[0] = '~'; output[1] = '"';
            output[sizeof output - 2] = '"'; output[sizeof output - 1] = '\n';
            for (int i = 0; i < 2048; ++i) {
                if (fwrite(output, 1, sizeof output, stdout) != sizeof output ||
                    fwrite(output, 1, sizeof output, stderr) != sizeof output) { break; }
            }
            reply(token, "^done");
        } else if (strcmp(command, "-close-input\n") == 0) {
            (void)close(STDIN_FILENO); delay(500); break;
        } else if (strcmp(command, "-eof\n") == 0) {
            (void)close(STDOUT_FILENO); delay(500); break;
        } else if (strcmp(command, "-truncated\n") == 0) {
            fputs("*stopped,reason=\"unfinished", stdout); (void)fflush(stdout); break;
        } else if (strcmp(command, "-malformed\n") == 0) {
            puts("^done,broken=[");
        } else if (strcmp(command, "-die\n") == 0) {
            free(line); return 13;
        } else if (strcmp(command, "-gdb-exit\n") == 0) {
            reply(token, "^exit"); free(line); return 0;
        } else if (strncmp(command, "-echo ", 6) == 0) {
            /* Reuse MI grammar only to decode generated argument bytes. */
            size_t length = strlen(command + 6);
            char *record_text = malloc(length + 9);
            if (record_text == NULL) { free(line); return 4; }
            memcpy(record_text, "^done,v=", 8);
            memcpy(record_text + 8, command + 6, length);
            TbMiRecord *record = NULL;
            TbMiError error;
            TbMiStatus status = tb_mi_parse(record_text, length + 7, &record, &error);
            free(record_text);
            if (status != TB_MI_OK) { free(line); return 5; }
            size_t bytes = record->results->string.length;
            tb_mi_record_free(record);
            (void)printf("%s^done,bytes=\"%zu\"\n", token, bytes);
        } else { reply(token, "^done"); }
        (void)fflush(stdout);
    }
    free(line);
    return 0;
}
