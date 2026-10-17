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

static void quoted(const char *text)
{
    (void)putchar('"');
    for (size_t i = 0; text[i]; ++i) {
        if (text[i] == '\\' || text[i] == '"') { (void)putchar('\\'); }
        (void)putchar(text[i]);
    }
    (void)putchar('"');
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
    int selected_thread = 7, selected_frame = 0;
    bool context_held = false, thread_exited = false;
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
        } else if (strncmp(mode, "context", 7) == 0 && strcmp(command, "-thread-info\n") == 0) {
            (void)printf("%s^done,current-thread-id=\"%d\",threads=[{id=\"7\",name=\"main\",target-id=\"FAKE worker\",state=\"stopped\",frame={level=\"0\",func=\"leaf\",addr=\"0x1000\"}}", token, selected_thread);
            if (!thread_exited) { fputs(",{id=\"42\",name=\"worker\",state=\"stopped\",frame={level=\"0\",func=\"worker_leaf\",addr=\"0x2000\"}}", stdout); }
            if (strcmp(mode, "context-many") == 0 && !thread_exited) {
                for (int i = 100; i < 240; ++i) { (void)printf(",{id=\"%d\",state=\"stopped\"}", i); }
            }
            puts("]");
        } else if (strncmp(mode, "context", 7) == 0 && strncmp(command, "-thread-select ", 15) == 0) {
            selected_thread = atoi(command + 15); selected_frame = 0;
            if (strcmp(mode, "context-selection-stale") == 0 && !context_held) {
                (void)snprintf(held, sizeof held, "%s", token); context_held = true;
            } else { reply(token, "^done"); }
        } else if (strncmp(mode, "context", 7) == 0 && strncmp(command, "-stack-select-frame ", 20) == 0) {
            selected_frame = atoi(command + 20); reply(token, "^done");
        } else if (strncmp(mode, "context", 7) == 0 && strcmp(command, "-stack-info-frame\n") == 0) {
            (void)printf("%s^done,frame={level=\"%d\",func=\"%s\",addr=\"0x1234\",arch=\"test:neutral\"", token, selected_frame, selected_thread == 7 ? "leaf" : "worker_leaf");
            if (strncmp(mode, "context-code", 12) == 0) {
                fputs(",fullname=", stdout); quoted(getenv("TB_FAKE_SOURCE") ? getenv("TB_FAKE_SOURCE") : "/missing.c");
                fputs(",line=\"2\"", stdout);
            }
            puts("}");
        } else if (strncmp(mode, "context-code", 12) == 0 && strcmp(command, "-file-list-exec-source-files\n") == 0) {
            (void)printf("%s^done,files=[{file=\"unit.c\",fullname=", token);
            quoted(getenv("TB_FAKE_SOURCE") ? getenv("TB_FAKE_SOURCE") : "/missing.c");
            puts("},{file=\"duplicate.c\",fullname=\"/not/here/duplicate.c\"},{file=\"relative-unresolved.c\"}]");
        } else if (strncmp(mode, "context-code", 12) == 0 && strncmp(command, "-data-disassemble ", 18) == 0) {
            if (strcmp(mode, "context-code-stale") == 0 && !context_held) {
                (void)snprintf(held, sizeof held, "%s", token); context_held = true;
            } else if (strcmp(mode, "context-code-fallback") == 0 && strstr(command, "-- 5")) {
                reply(token, "^error,msg=\"FAKE mixed mode unavailable\"");
            } else {
                unsigned long long address = 0, end = 0;
                if (sscanf(command + 18, "-s %llx -e %llx", &address, &end) != 2) { free(line); return 14; }
                bool mixed = strstr(command, "-- 5") != NULL;
                (void)printf("%s^done,asm_insns=[", token);
                if (mixed) {
                    fputs("src_and_asm_line={line=\"2\",fullname=", stdout);
                    quoted(getenv("TB_FAKE_SOURCE") ? getenv("TB_FAKE_SOURCE") : "/missing.c");
                    fputs(",line_asm_insn=[", stdout);
                }
                for (unsigned i = 0; i < 140 && address + i < end; ++i) {
                    unsigned long long base = strcmp(mode, "context-code-short") == 0 ? 0x122f : 0x1228;
                    unsigned long long offset = address + i >= base && address + i < 0x1328 ? address + i - base : 0;
                    (void)printf("%s{address=\"0x%llx\",offset=\"%llu\",opcodes=\"90\",inst=\"mov %%eax,%%ebx\"}", i ? "," : "", address + i, offset);
                }
                if (mixed) { fputs("]}", stdout); }
                puts("]");
            }
        } else if (strncmp(mode, "context-code", 12) == 0 && strncmp(command, "-data-evaluate-expression ", 26) == 0) {
            reply(token, "^done,value=\"0x2000 <main>\"");
        } else if (strncmp(mode, "context", 7) == 0 && strncmp(command, "-stack-list-frames ", 19) == 0) {
            if (strcmp(mode, "context-stale") == 0 && !context_held) {
                (void)snprintf(held, sizeof held, "%s", token); context_held = true;
            } else if (strcmp(mode, "context-unwind") == 0) { reply(token, "^error,msg=\"FAKE cannot unwind\""); }
            else if (strcmp(mode, "context-malformed") == 0) { reply(token, "^done,stack=\"unavailable\""); }
            else {
                int low = 0, high = 0;
                if (sscanf(command + 19, "--no-frame-filters %d %d", &low, &high) != 2) { free(line); return 11; }
                (void)printf("%s^done,stack=[", token);
                for (int level = low; level <= high && level < 40; ++level) {
                    (void)printf("%sframe={level=\"%d\",func=\"%s_%d\",addr=\"0x1234\",file=\"path with spaces.c\",line=\"12\"}", level == low ? "" : ",", level, selected_thread == 7 ? "main" : "worker", level);
                }
                puts("]");
            }
        } else if (strncmp(mode, "context", 7) == 0 && strncmp(command, "-stack-list-arguments ", 22) == 0) {
            int low = 0, high = 0;
            if (sscanf(command + 22, "--no-frame-filters --simple-values %d %d", &low, &high) != 2) { free(line); return 12; }
            (void)printf("%s^done,stack-args=[", token);
            for (int level = low; level <= high && level < 40; ++level) {
                (void)printf("%sframe={level=\"%d\",args=[{name=\"depth\",value=\"%s\"}]}", level == low ? "" : ",", level, level == 1 ? "<optimized out>" : "4");
            }
            puts("]");
        } else if (strcmp(command, "-ctx-release\n") == 0) {
            reply(token, "^done");
            if (*held) { reply(held, "^done,stack=[frame={level=\"0\",func=\"OBSOLETE\"}]"); *held = '\0'; }
        } else if (strcmp(command, "-ctx-exit\n") == 0) {
            thread_exited = true; selected_thread = 7; selected_frame = 0;
            puts("=thread-exited,id=\"42\",group-id=\"i1\""); reply(token, "^done");
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
            if (command[6] != '"') {
                (void)printf("%s^done,bytes=\"%zu\"\n", token, strlen(command + 6) - 1);
                (void)fflush(stdout);
                continue;
            }
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
