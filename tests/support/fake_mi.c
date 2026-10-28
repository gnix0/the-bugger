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
    unsigned register_value = 16; bool register_error = false, register_malformed = false;
    bool register_stop_held = false;
    unsigned memory_mode = 0;
    unsigned mmio_mode = strcmp(mode, "remote-mmio-slow") == 0 ? 1 : 0, mmio_reads = 0;
    bool mmio_peer = strcmp(mode, "mmio") == 0 || strcmp(mode, "context-mmio") == 0 || strncmp(mode, "remote-mmio", 11) == 0;
    bool code_page_error = false, code_duplicate_source = false, code_limited_sources = false;
    unsigned code_bad_reply = 0;
    char variable_name[64] = {0}; unsigned variable_reads = 0, variable_children = 0, variable_deletes = 0;
    unsigned variable_mode = 0;
    unsigned variable_value = 17;
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
    bool context_held = false, thread_exited = false, arguments_held = false;
    int held_argument_level = -1;
    bool bp_temporary = false; int bp_table = 0, bp_clean_attempts = 0;
    unsigned remote_resets = 0;
    unsigned data_mode = 0; bool data_watch = false;
    char data_expression[512] = {0}, data_type[64] = "hw watchpoint";
    while (getline(&line, &capacity, stdin) >= 0) {
        size_t digits = strspn(line, "0123456789");
        if (digits == 0 || digits >= sizeof held) { free(line); return 3; }
        char token[32];
        memcpy(token, line, digits); token[digits] = '\0';
        char *command = line + digits;
        if (mmio_peer && strncmp(command, "-mmio-mode ", 11) == 0) {
            mmio_mode = (unsigned)strtoul(command + 11, NULL, 10); reply(token, "^done");
        } else if (mmio_peer && strcmp(command, "-mmio-stats\n") == 0) {
            (void)printf("%s^done,reads=\"%u\"\n", token, mmio_reads);
        } else if (mmio_peer && strstr(command, "monitor apply ")) {
            char id[64], target[64]; unsigned width; unsigned long long address;
            const char *tail = strstr(command, "}} ");
            if (!tail || !strstr(command, "$t read_memory $a $w 1") ||
                sscanf(tail + 3, "%63s %63s %llx %u", id, target, &address, &width) != 4) { free(line); return 81; }
            ++mmio_reads;
            if (mmio_mode == 1) { delay(3500); }
            if (mmio_mode == 9) { reply(token, "^error,msg=\"Authored monitor failure\""); }
            else {
                if (mmio_mode == 10) { puts("*running,thread-id=\"all\""); }
                char frame[256];
                const char *value = mmio_mode == 7 ? "100000000" : width == 8 ? "5a" : width == 16 ? "125a" : "1234125a";
                (void)snprintf(frame, sizeof frame, "TBM1|%s|%s|%llx|%u|%u|%s|END\\n",
                    mmio_mode == 11 ? "old-request" : id, mmio_mode == 4 ? "other.cpu" : target,
                    address + (mmio_mode == 3 ? 4 : 0), mmio_mode == 5 ? 16 : width, mmio_mode == 6 ? 2 : 1, value);
                if (mmio_mode != 8) { (void)printf("@\"%s\"\n", frame); }
                if (mmio_mode == 2) { (void)printf("@\"%s\"\n", frame); }
                reply(token, "^done");
            }
        } else if (strncmp(mode, "remote-", 7) == 0) {
            if (strstr(command, "disable-randomization") || strstr(command, "startup-with-shell") ||
                strstr(command, "inferior-tty-set") || strstr(command, "exec-arguments") ||
                strstr(command, "-target-attach") || strstr(command, "kill")) { free(line); return 80; }
            if (strncmp(command, "-target-select ", 15) == 0) {
                if (strcmp(mode, "remote-timeout") == 0 || strcmp(mode, "remote-cancel") == 0) { continue; }
                if (strcmp(mode, "remote-drop") == 0) { free(line); return 0; }
                if (strcmp(mode, "remote-refused") == 0) { reply(token, "^error,msg=\"Connection refused\""); }
                else {
                    puts("=thread-group-started,id=\"i1\",pid=\"987654321\"");
                    puts("*stopped,reason=\"signal-received\",thread-id=\"1\",frame={level=\"0\",addr=\"0x8000100\",func=\"remote_main\",arch=\"arm\"}");
                    reply(token, "^connected");
                }
            } else if (strncmp(command, "-data-evaluate-expression ", 26) == 0) {
                if (strstr(command, "unsigned char")) { reply(token, strcmp(mode, "remote-wide") == 0 ? "^done,value=\"256\"" : "^done,value=\"0\""); }
                else if (strstr(command, "sizeof(void*)")) { reply(token, "^done,value=\"4\""); }
                else { reply(token, "^error,msg=\"Unexpected automatic value evaluation\""); }
            } else if (strcmp(command, "-gdb-show endian\n") == 0) {
                reply(token, strcmp(mode, "remote-unknown") == 0 ? "^done,value=\"auto\"" : "^done,value=\"big\"");
            } else if (strcmp(command, "-stack-info-frame\n") == 0) {
                reply(token, remote_resets ? "^done,frame={level=\"0\",addr=\"0x8000200\",func=\"remote_main\",arch=\"arm\"}" : "^done,frame={level=\"0\",addr=\"0x8000100\",func=\"remote_main\",arch=\"arm\"}");
            } else if (strcmp(command, "-thread-info\n") == 0 && !strcmp(mode, "remote-openocd-verify-error")) {
                reply(token, "^error,msg=\"Target state unavailable after monitor operation\"");
            } else if (strcmp(command, "-thread-info\n") == 0 && remote_resets && !strcmp(mode, "remote-openocd-current-error")) {
                reply(token, "^done,threads=[{id=\"1\",state=\"stopped\",frame={addr=\"0x8000200\"}}],current-thread-id=\"3\"");
            } else if (strcmp(command, "-thread-info\n") == 0 && remote_resets && !strcmp(mode, "remote-openocd-frame-error")) {
                reply(token, "^done,threads=[{id=\"1\",state=\"stopped\"}],current-thread-id=\"1\"");
            } else if (strcmp(command, "-thread-info\n") == 0 && remote_resets && !strcmp(mode, "remote-openocd-address-error")) {
                reply(token, "^done,threads=[{id=\"1\",state=\"stopped\",frame={addr=\"<unavailable>\"}}],current-thread-id=\"1\"");
            } else if (strcmp(command, "-thread-info\n") == 0) {
                reply(token, remote_resets ? "^done,threads=[{id=\"1\",state=\"stopped\",frame={level=\"0\",addr=\"0x8000200\",arch=\"arm\"}}],current-thread-id=\"1\"" : "^done,threads=[{id=\"1\",state=\"stopped\"}],current-thread-id=\"1\"");
            } else if (strstr(command, "monitor reset halt") && !strncmp(mode, "remote-openocd", 14)) {
                ++remote_resets; puts("~\"M16 reset requested\\n\"");
                reply(token, !strcmp(mode, "remote-openocd-error") ? "^error,msg=\"Reset unsupported by this target\"" : "^done");
            } else if (strstr(command, "maintenance flush register-cache") && !strncmp(mode, "remote-openocd", 14)) {
                puts("~\"M16 flush requested\\n\"");
                reply(token, !strcmp(mode, "remote-openocd-flush-error") ? "^error,msg=\"Register cache flush unsupported\"" : "^done");
            } else if (strstr(command, "monitor resume") && !strncmp(mode, "remote-openocd", 14)) {
                puts("~\"M16 resume requested\\n\"");
                reply(token, !strcmp(mode, "remote-openocd-resume-error") ? "^error,msg=\"Resume unsupported by this target\"" : "^done");
            } else if (strstr(command, "monitor halt") && !strncmp(mode, "remote-openocd", 14)) {
                puts("~\"M16 halt requested\\n\""); reply(token, "^done");
            } else if (strcmp(command, "-target-detach\n") == 0 && !strncmp(mode, "remote-openocd", 14)) {
                puts("~\"M16 detach requested\\n\""); reply(token, "^done");
            } else if (strcmp(command, "-target-disconnect\n") == 0) {
                reply(token, strcmp(mode, "remote-disconnect-error") == 0 ? "^error,msg=\"Disconnect not confirmed\"" : "^done");
            } else if (strstr(command, "-data-read-memory-bytes") && strcmp(mode, "remote-regions") == 0) {
                const char *address = strstr(command, "0x");
                if (!address) { free(line); return 82; }
                char start[32]; size_t n = strspn(address, "0x0123456789abcdefABCDEF");
                if (!n || n >= sizeof start) { free(line); return 82; }
                memcpy(start, address, n); start[n] = 0;
                printf("%s^done,memory=[{begin=\"%s\",end=\"0x%llx\",offset=\"0x0\",contents=\"000102030405060708090a0b0c0d0e0f\"}]\n",
                       token, start, strtoull(start, NULL, 16) + 16); fflush(stdout);
            } else if (strncmp(command, "-var-", 5) == 0 || strncmp(command, "-data-read-memory", 17) == 0) {
                free(line); return 81; /* No automatic target value reads. */
            } else { reply(token, "^done"); }
        } else if (strcmp(mode, "profile-source") == 0 && strncmp(command, "-environment-directory ", 23) == 0 && strstr(command, "rejected")) {
            reply(token, "^error,msg=\"Source directory rejected by test backend\"");
        } else if (strcmp(mode, "data-watch") == 0 && strncmp(command, "-data-mode ", 11) == 0) {
            data_mode = (unsigned)strtoul(command + 11, NULL, 10); reply(token, "^done");
        } else if (strcmp(mode, "data-watch") == 0 && strcmp(command, "-thread-info\n") == 0) {
            reply(token, "^done,threads=[{id=\"1\",state=\"stopped\"}],current-thread-id=\"1\"");
        } else if (strcmp(mode, "data-watch") == 0 && strncmp(command, "-break-watch ", 13) == 0) {
            if (data_mode == 2) { reply(token, "^error,msg=\"Target watch width/alignment/slots unsupported\""); continue; }
            const char *type = strstr(command, "-r ") ? "read watchpoint" : strstr(command, "-a ") ? "acc watchpoint" : "hw watchpoint";
            if (data_mode == 1 || data_mode == 3) { type = "watchpoint"; }
            (void)snprintf(data_type, sizeof data_type, "%s", type);
            char *expression = command + 13;
            if (strncmp(expression, "--language c ", 13) != 0) { free(line); return 32; }
            expression += 13;
            if (strncmp(expression, "-r ", 3) == 0 || strncmp(expression, "-a ", 3) == 0) { expression += 3; }
            /* These tests use literal safe ASCII expressions; MI adds quotes
             * only for whitespace. Reject escapes rather than fake a parser. */
            size_t length = strcspn(expression, "\r\n");
            if (length && expression[0] == '"' && expression[length - 1] == '"') { ++expression; length -= 2; }
            if (!length || length >= sizeof data_expression || memchr(expression, '\\', length)) { free(line); return 30; }
            memcpy(data_expression, expression, length); data_expression[length] = 0; data_watch = true;
            if (data_mode == 4) { puts("*stopped,reason=\"signal-received\",thread-id=\"1\""); }
            if (data_mode == 12) { (void)snprintf(held, sizeof held, "%s", token); (void)fflush(stdout); continue; }
            if (data_mode == 8) { reply(token, "^done,wpt={number=\"1\"}"); }
            else if (data_mode == 5 || data_mode == 6) { reply(token, "^done"); }
            else { reply(token, "^done,wpt={number=\"2\"}"); }
        } else if (strcmp(mode, "data-watch") == 0 && strcmp(command, "-break-list\n") == 0) {
            if (data_mode == 7) { reply(token, "^done,BreakpointTable={body=\"malformed\"}"); continue; }
            (void)printf("%s^done,BreakpointTable={body=[bkpt={number=\"1\",type=\"breakpoint\",enabled=\"y\",times=\"9\"}", token);
            if (data_watch) {
                fputs(",bkpt={number=\"2\",enabled=\"y\",type=", stdout); quoted(data_type);
                fputs(",what=", stdout); quoted(data_mode == 6 ? "another_expression" : data_expression); putchar('}');
            }
            puts("]}");
        } else if (strcmp(mode, "data-watch") == 0 && strncmp(command, "-break-delete ", 14) == 0) {
            if (strcmp(command, "-break-delete 2\n") != 0) { free(line); return 31; }
            if (data_mode == 3) { reply(token, "^error,msg=\"cleanup rejected\""); }
            else { data_watch = false; puts("=breakpoint-deleted,id=\"2\""); reply(token, "^done"); }
        } else if (strcmp(mode, "data-watch") == 0 && strcmp(command, "-data-scope\n") == 0) {
            data_watch = false; puts("=breakpoint-deleted,id=\"2\"");
            puts("*stopped,reason=\"watchpoint-scope\",wpnum=\"2\",thread-id=\"1\""); reply(token, "^done");
        } else if (strcmp(mode, "data-watch") == 0 && strcmp(command, "-data-hit\n") == 0) {
            puts("*stopped,reason=\"watchpoint-trigger\",wpt={number=\"2\",exp=\"watched\"},value={old=\"17\",new=\"20\"},thread-id=\"1\""); reply(token, "^done");
        } else if (strcmp(mode, "data-watch") == 0 && strcmp(command, "-data-downgrade\n") == 0) {
            (void)snprintf(data_type, sizeof data_type, "watchpoint");
            puts("=breakpoint-modified,bkpt={number=\"2\",type=\"watchpoint\"}"); reply(token, "^done");
        } else if (strcmp(mode, "data-watch") == 0 && strcmp(command, "-data-release\n") == 0) {
            reply(token, "^done"); if (*held) { reply(held, "^done,wpt={number=\"2\"}"); *held = 0; }
        } else if (strncmp(mode, "bp-", 3) == 0 && strcmp(command, "-thread-info\n") == 0) {
            if (strcmp(mode, "bp-late-error-running") == 0) { reply(token, "^done,threads=[{id=\"1\",state=\"running\"}],current-thread-id=\"1\""); }
            else if (strcmp(mode, "bp-late-error-unknown") == 0) { reply(token, "^done"); }
            else { reply(token, "^done,threads=[{id=\"1\",state=\"stopped\",frame={level=\"0\",addr=\"0x1000\",func=\"main\"}}],current-thread-id=\"1\""); }
        } else if (strncmp(mode, "bp-", 3) == 0 && strcmp(command, "-break-list\n") == 0) {
            if (bp_table == 2) { reply(token, "^done,BreakpointTable={body=\"malformed\"}"); continue; }
            (void)printf("%s^done,BreakpointTable={body=[bkpt={number=\"1\",type=\"breakpoint\",enabled=\"y\",addr=\"0x1000\",fullname=\"/source.c\",line=\"7\",times=\"9\"}", token);
            if (bp_table == 1) {
                for (int i = 2; i < 302; ++i) { (void)printf(",bkpt={number=\"%d\",type=\"breakpoint\",enabled=\"y\",addr=\"<PENDING>\"}", i); }
            } else {
                fputs(",bkpt={number=\"2\",type=\"hw breakpoint\",enabled=\"y\",addr=\"<MULTIPLE>\",locations=[{number=\"2.1\",enabled=\"y\",addr=\"0x2000\"},{number=\"2.2\",enabled=\"n\",addr=\"0x3000\"}]},bkpt={number=\"3\",enabled=\"y\",type=\"breakpoint\",addr=\"<PENDING>\",original-location=\"future\"}", stdout);
            }
            if (bp_temporary) {
                fputs(",bkpt={number=\"5\",type=\"breakpoint\",disp=\"del\",enabled=\"y\",addr=\"0x4000\",original-location=", stdout);
                fputs(strcmp(mode, "bp-timeout-ambiguous") == 0 ? "\"different spelling\"}" : "\"main\"}", stdout);
            }
            puts("]}");
        } else if (strncmp(mode, "bp-", 3) == 0 && strncmp(command, "-bp-table ", 10) == 0) {
            bp_table = atoi(command + 10); reply(token, "^done");
        } else if (strncmp(mode, "bp-", 3) == 0 && strncmp(command, "-break-insert ", 14) == 0) {
            if (!strstr(command, "-t ")) { reply(token, "^error,msg=\"FAKE hardware slots exhausted\""); continue; }
            bp_temporary = true;
            if (strcmp(mode, "bp-timeout-silent") != 0 && strcmp(mode, "bp-timeout-ambiguous") != 0) { puts("=breakpoint-created,bkpt={number=\"5\",type=\"breakpoint\",disp=\"del\",original-location=\"main\"}"); }
            if (strcmp(mode, "bp-race") == 0) { puts("*stopped,reason=\"signal-received\",thread-id=\"1\""); }
            if (strncmp(mode, "bp-timeout", 10) == 0) { (void)fflush(stdout); continue; }
            reply(token, "^done,bkpt={number=\"5\",disp=\"del\"}");
        } else if (strncmp(mode, "bp-", 3) == 0 && strncmp(command, "-break-delete ", 14) == 0) {
            if (strcmp(command, "-break-delete 5\n")) { reply(token, "^error,msg=\"FAKE attempted user breakpoint deletion\""); }
            else if (strcmp(mode, "bp-clean-error") == 0 && !bp_clean_attempts++) { reply(token, "^error,msg=\"FAKE cleanup failure\""); }
            else { bp_temporary = false; puts("=breakpoint-deleted,id=\"5\""); reply(token, "^done"); }
        } else if (strncmp(mode, "bp-", 3) == 0 && strncmp(command, "-exec-continue", 14) == 0) {
            if (strncmp(mode, "bp-late-error", 13) == 0) { reply(token, "^running"); puts("*running,thread-id=\"all\""); reply(token, "^error,msg=\"FAKE command aborted\""); }
            else if (strcmp(mode, "bp-resume-error") == 0) { reply(token, "^error,msg=\"FAKE cannot insert breakpoint\""); }
            else if (strcmp(mode, "bp-race") == 0) { reply(token, "^error,msg=\"FAKE race incorrectly resumed\""); }
            else { reply(token, "^running"); puts("*running,thread-id=\"all\""); puts("*stopped,reason=\"breakpoint-hit\",bkptno=\"1\",thread-id=\"1\""); }
        } else if (strncmp(command, "-gdb-set ", 9) == 0) {
            if (strcmp(mode, "setup-error") == 0) { reply(token, "^error,msg=\"FAKE unsupported setting\""); }
            else { reply(token, "^done"); }
            if (++setup == 2 && strcmp(mode, "no-read") == 0) { delay(400); }
            if (setup == 2 && strcmp(mode, "late-fallback") == 0) {
                fputs("Interpreter mi3 unrecognized\n", stderr); free(line); return 7;
            }
        } else if (strcmp(mode, "context-variables") == 0 && strncmp(command, "-variable-mode ", 15) == 0) {
            variable_mode = (unsigned)strtoul(command + 15, NULL, 10); reply(token, "^done");
        } else if (strcmp(mode, "context-variables") == 0 && strncmp(command, "-variable-value ", 16) == 0) {
            variable_value = (unsigned)strtoul(command + 16, NULL, 10); reply(token, "^done");
        } else if (strcmp(mode, "context-variables") == 0 && strcmp(command, "-variable-release\n") == 0) {
            reply(token, "^done");
            if (*held) { (void)printf("%s^done,name=\"%s\",numchild=\"0\",type=\"int\",value=\"OBSOLETE\"\n", held, variable_name); *held = 0; }
        } else if (strcmp(mode, "context-variables") == 0 && strncmp(command, "-stack-list-variables ", 22) == 0) {
            if (!strstr(command, "--no-values")) { free(line); return 25; }
            if (variable_mode == 4 || variable_mode == 5) {
                (void)printf("%s^done,variables=[", token);
                for (unsigned i = 0; i < 1000; ++i) { (void)printf("%s{name=\"%s%u\"}", i ? "," : "", variable_mode == 4 ? "old" : "new", i); }
                puts("]");
            } else { reply(token, "^done,variables=[{name=\"scalar\"},{name=\"aggregate\"},{name=\"pointer\"},{name=\"unavailable\"}]"); }
        } else if (strcmp(mode, "context-variables") == 0 && strncmp(command, "-var-create ", 12) == 0) {
            char *name = strstr(command, "tbv");
            if (!name || sscanf(name, "%63s", variable_name) != 1) { free(line); return 26; }
            ++variable_reads;
            if (variable_mode == 3) { (void)snprintf(held, sizeof held, "%s", token); }
            else if (variable_mode == 1) { reply(token, "^done,type=\"malformed\""); }
            else if (strstr(command, "invalid")) { reply(token, "^error,msg=\"Unknown symbol\""); }
            else {
                char value[32]; (void)snprintf(value, sizeof value, "%u", variable_value);
                (void)printf("%s^done,name=\"%s\",numchild=\"%u\",type=\"%s\",value=\"%s\"\n", token, variable_name,
                    strstr(command, "aggregate") ? (variable_mode == 6 ? 1U : 100U) : strstr(command, "pointer") ? 1U : 0U,
                    strstr(command, "aggregate") ? (variable_mode == 6 ? "Sample" : "int [100]") : strstr(command, "pointer") ? "int *" : "int",
                    strstr(command, "unavailable") ? "<optimized out>" : strstr(command, "pointer") ? "0x40000000" : value);
            }
        } else if (strcmp(mode, "context-variables") == 0 && strncmp(command, "-var-list-children ", 19) == 0) {
            unsigned first, end; char object[256];
            if (sscanf(command, "-var-list-children --all-values %255s %u %u", object, &first, &end) != 3 || end < first || end - first > 32) { free(line); return 27; }
            if (variable_mode == 6) {
                ++variable_children;
                if (!strstr(object, ".public")) {
                    (void)printf("%s^done,numchild=\"1\",children=[child={name=\"%s.public\",exp=\"public\",numchild=\"4\",value=\"\"}]\n", token, object);
                } else {
                    static const char *const names[] = {"count", "flags", "text", "values"};
                    (void)printf("%s^done,numchild=\"4\",children=[", token);
                    for (unsigned i = first; i < end && i < 4; ++i) {
                        (void)printf("%schild={name=\"%s.%s\",exp=\"%s\",numchild=\"0\",type=\"int\",value=\"%u\"}", i == first ? "" : ",", object, names[i], names[i], variable_value);
                    }
                    puts("]");
                }
                (void)fflush(stdout); continue;
            }
            ++variable_children; (void)printf("%s^done,numchild=\"100\",children=[", token);
            for (unsigned i = first; i < end; ++i) {
                (void)printf("%schild={name=\"%s.%u\",exp=\"%u\",numchild=\"0\",type=\"int\",value=\"%u\"}", i == first ? "" : ",", object, i, i, i);
            }
            puts("]");
        } else if (strcmp(mode, "context-variables") == 0 && strncmp(command, "-var-evaluate-expression ", 25) == 0) {
            ++variable_reads; reply(token, "^done,value=\"0x11\"");
        } else if (strcmp(mode, "context-variables") == 0 && strncmp(command, "-var-delete ", 12) == 0) {
            ++variable_deletes; reply(token, "^done,ndeleted=\"1\"");
        } else if (strcmp(mode, "context-variables") == 0 && strcmp(command, "-variable-stats\n") == 0) {
            (void)printf("%s^done,reads=\"%u\",children=\"%u\",deletes=\"%u\"\n", token, variable_reads, variable_children, variable_deletes);
        } else if (strcmp(mode, "context-memory") == 0 && strncmp(command, "-memory-mode ", 13) == 0) {
            memory_mode = (unsigned)strtoul(command + 13, NULL, 10); reply(token, "^done");
        } else if (strcmp(mode, "context-memory") == 0 && strncmp(command, "-data-evaluate-expression ", 26) == 0) {
            if (!strstr(command, "--language c") || !strstr(command, "(unsigned long long)(")) { free(line); return 19; }
            reply(token, "^done,value=\"0x1000 <buffer>\"");
        } else if (strcmp(mode, "context-memory") == 0 && strncmp(command, "-data-read-memory-bytes ", 24) == 0) {
            unsigned long long address; unsigned length;
            /* MI command name has 23 characters, followed by a space. */
            if (sscanf(command + 24, "%llx %u", &address, &length) != 2 || !length || length > 1024) { free(line); return 20; }
            if (memory_mode == 2) { reply(token, "^error,msg=\"FAKE unreadable memory\\nregion\""); }
            else if (memory_mode == 3) { reply(token, "^done,memory=\"bad\""); }
            else if (memory_mode == 4) { reply(token, "^done,memory=[{begin=\"0x1000\",end=\"0x1001\",contents=\"00\"},{begin=\"0x1000\",end=\"0x1001\",contents=\"00\"}]"); }
            else if (memory_mode == 5) { reply(token, "^done,memory=[{begin=\"0x0\",end=\"0xffffffffffffffff\",contents=\"00\"}]"); }
            else if (memory_mode == 6) { reply(token, "^done,memory=[]"); }
            else if (memory_mode == 7) { reply(token, "^done,memory=[{begin=\"0x1000\",end=\"0x1001\",contents=\"zz\"}]"); }
            else {
                unsigned count = memory_mode == 1 ? length / 2 : length;
                if (memory_mode == 8) { puts("*running,thread-id=\"all\""); }
                (void)printf("%s^done,memory=[{begin=\"0x%llx\",end=\"0x%llx\",offset=\"0x0\",contents=\"", token, address, address + count);
                for (unsigned i = 0; i < count; ++i) { (void)printf("%02x", (unsigned)((address + i) & 255)); }
                puts("\"}]");
                if (memory_mode == 8) { puts("*stopped,reason=\"end-stepping-range\",thread-id=\"7\",frame={level=\"0\",addr=\"0x1234\",func=\"main\"}"); }
            }
        } else if (strncmp(mode, "context-registers", 17) == 0 && strcmp(command, "-register-change\n") == 0) {
            register_value += 16; reply(token, "^done");
        } else if (strncmp(mode, "context-registers", 17) == 0 && strcmp(command, "-register-error\n") == 0) {
            register_error = !register_error; reply(token, "^done");
        } else if (strncmp(mode, "context-registers", 17) == 0 && strcmp(command, "-register-malformed\n") == 0) {
            register_malformed = !register_malformed; reply(token, "^done");
        } else if (strncmp(mode, "context-registers", 17) == 0 && strcmp(command, "-data-list-register-names\n") == 0) {
            if (strstr(mode, "bounds")) {
                (void)printf("%s^done,register-names=[", token);
                for (int i = 0; i < 600; ++i) { (void)printf("%s\"target%d\"", i ? "," : "", i); } puts("]");
            } else { reply(token, strstr(mode, "arm") ? "^done,register-names=[\"r0\",\"\",\"pc\",\"q0\",\"unreadable\"]" :
                                                      "^done,register-names=[\"rax\",\"\",\"rip\",\"xmm0\",\"unreadable\"]"); }
        } else if (strncmp(mode, "context-registers", 17) == 0 && strncmp(command, "-data-list-register-values ", 27) == 0) {
            if (register_error) { reply(token, "^error,msg=\"FAKE register read error\""); }
            else if (register_malformed) { reply(token, "^done,register-values=\"bad\""); }
            else {
                char *at = strstr(command, "--frame ");
                if (!at) { free(line); return 18; }
                char *end; long frame_value = strtol(at + 8, &end, 10); at = end;
                while (*at == ' ') { ++at; } char format = *at++;
                (void)printf("%s^done,register-values=[", token); bool comma = false;
                while (*at == ' ') {
                    unsigned long id = strtoul(at + 1, &end, 10); if (end == at + 1) { break; } at = end;
                    if (id == 4) { continue; }
                    (void)printf("%s{number=\"%lu\",value=\"", comma ? "," : "", id); comma = true;
                    if (id == 3) { for (int i = 0; i < 1200; ++i) { (void)putchar('a'); } }
                    else if (format == 'x') { (void)printf("0x%x", register_value + (unsigned)frame_value); }
                    else { (void)printf("%u", register_value + (unsigned)frame_value); }
                    fputs("\"}", stdout);
                }
                puts("]");
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
            fputs("},{file=\"duplicate.c\",fullname=\"/not/here/duplicate.c\"},{file=\"relative-unresolved.c\"}", stdout);
            if (code_duplicate_source) { fputs(",{file=\"duplicate.c\",fullname=", stdout); quoted(getenv("TB_FAKE_SOURCE2")); fputs("}", stdout); }
            if (code_limited_sources) { for (unsigned i = 0; i < 257; ++i) { fputs(",{file=\"filler.c\",fullname=\"/not/real.c\"}", stdout); } }
            puts("]");
        } else if (strncmp(mode, "context-code", 12) == 0 && strcmp(command, "-code-limited-sources\n") == 0) {
            code_limited_sources = true; reply(token, "^done");
        } else if (strncmp(mode, "context-code", 12) == 0 && strcmp(command, "-code-duplicate-source\n") == 0) {
            code_duplicate_source = true; reply(token, "^done");
        } else if (strncmp(mode, "context-code", 12) == 0 && strcmp(command, "-code-page-error\n") == 0) {
            code_page_error = true; reply(token, "^done");
        } else if (strncmp(mode, "context-code", 12) == 0 && strncmp(command, "-code-bad-reply ", 16) == 0) {
            code_bad_reply = (unsigned)atoi(command + 16); reply(token, "^done");
        } else if (strncmp(mode, "context-code", 12) == 0 && strncmp(command, "-data-disassemble ", 18) == 0) {
            if (code_bad_reply) {
                if (code_bad_reply == 1) { reply(token, "^done,asm_insns=\"invalid\""); }
                else if (code_bad_reply == 2) { reply(token, "^done,asm_insns=[]"); }
                else { reply(token, "^done,asm_insns=[{address=\"0x6000\",inst=\"nop\"},{address=\"invalid\",inst=\"nop\"}]"); }
                code_bad_reply = 0;
            } else if (code_page_error) {
                code_page_error = false;
                reply(token, "^error,msg=\"Cannot access memory at requested page\"");
            } else if (strcmp(mode, "context-code-stale") == 0 && !context_held) {
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
                    if (mixed && strcmp(mode, "context-code-multifile") == 0 && i > 0 && i <= 4) {
                        fputs("]},src_and_asm_line={line=", stdout);
                        quoted(i == 2 ? "3" : "2"); fputs(",fullname=", stdout);
                        quoted(i == 3 ? "/missing/mixed-source.c" : i == 4 ? getenv("TB_FAKE_SOURCE") : getenv("TB_FAKE_SOURCE2"));
                        fputs(",line_asm_insn=[", stdout);
                    }
                    unsigned long long base = strcmp(mode, "context-code-short") == 0 ? 0x122f : 0x1228;
                    unsigned long long offset = address + i >= base && address + i < 0x1328 ? address + i - base : 0;
                    bool new_group = mixed && strcmp(mode, "context-code-multifile") == 0 && i > 0 && i <= 4;
                    (void)printf("%s{address=\"0x%llx\",offset=\"%llu\",opcodes=\"90\",inst=\"mov %%eax,%%ebx\"}", i && !new_group ? "," : "", address + i, offset);
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
            bool values = sscanf(command + 22, "--no-frame-filters --simple-values %d %d", &low, &high) == 2;
            if (!values && sscanf(command + 22, "--no-frame-filters --no-values %d %d", &low, &high) != 2) { free(line); return 12; }
            /* Reject a value request for any frame other than the explicit
             * selected frame; automatic page-wide evaluation is a test failure. */
            if (values && (low != high || low != selected_frame)) { free(line); return 13; }
            if (values && strcmp(mode, "context-args-missing") == 0) {
                reply(token, "^done,stack-args=[]");
            } else if (values && strcmp(mode, "context-args-error") == 0) {
                reply(token, "^error,msg=\"Argument storage unavailable\"");
            } else if (values && (strcmp(mode, "context-args-stale") == 0 || strcmp(mode, "context-args-resume") == 0) && !arguments_held) {
                arguments_held = true; held_argument_level = low;
                (void)snprintf(held, sizeof held, "%s", token);
            } else {
                (void)printf("%s^done,stack-args=[", token);
                for (int level = low; level <= high && level < 40; ++level) {
                    (void)printf("%sframe={level=\"%d\",args=[", level == low ? "" : ",", level);
                    if (values) { (void)printf("{name=\"depth\",value=\"%s\"}", level == 1 ? "<optimized out>" : "4"); }
                    else if (level % 2) { (void)printf("{name=\"depth\",value=\"UNSOLICITED\"}"); }
                    else { (void)printf("name=\"depth\""); }
                    (void)printf("]}");
                }
                puts("]");
            }
        } else if (strcmp(command, "-ctx-release\n") == 0) {
            reply(token, "^done");
            if (*held && held_argument_level >= 0) {
                (void)printf("%s^done,stack-args=[frame={level=\"%d\",args=[{name=\"depth\",value=\"OBSOLETE\"}]}]\n", held, held_argument_level);
                *held = 0; held_argument_level = -1;
            }
            if (*held) { reply(held, "^done,stack=[frame={level=\"0\",func=\"OBSOLETE\"}]"); *held = '\0'; }
        } else if (strcmp(command, "-ctx-exit\n") == 0) {
            thread_exited = true; selected_thread = 7; selected_frame = 0;
            puts("=thread-exited,id=\"42\",group-id=\"i1\""); reply(token, "^done");
        } else if (strcmp(command, "-hold\n") == 0) {
            (void)snprintf(held, sizeof held, "%s", token);
        } else if (strcmp(command, "-release\n") == 0) {
            reply(token, "^done,order=\"second-first\"");
            if (held[0] != '\0') { reply(held, "^done,order=\"held\""); held[0] = '\0'; }
        } else if (strncmp(mode, "context-registers", 17) == 0 && strcmp(command, "-stop-held\n") == 0) {
            register_stop_held = true; reply(token, "^done");
        } else if (strncmp(mode, "context-registers", 17) == 0 && strcmp(command, "-release-stop\n") == 0 && register_stop_held) {
            register_stop_held = false; reply(token, "^done");
            puts("*stopped,reason=\"breakpoint-hit\",thread-id=\"1\"");
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
