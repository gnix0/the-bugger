#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Linux spawn/pipe interfaces. */
#include "openocd.h"
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>
extern char **environ;

typedef struct { char path[48][TB_PROFILE_PATH]; size_t count; } Roots;
static uint64_t now(void)
{
    struct timespec t; (void)clock_gettime(CLOCK_MONOTONIC, &t);
    return (uint64_t)t.tv_sec * 1000 + (uint64_t)t.tv_nsec / 1000000;
}
static bool fail(char *error, size_t size, const char *text)
{ (void)snprintf(error, size, "%s", text); return false; }
static bool clean(const char *text, size_t size)
{
    size_t n = strnlen(text, size); if (!n || n == size || strchr(text, '#')) { return false; }
    for (size_t i = 0; i < n; ++i) { if ((unsigned char)text[i] < 32 || (unsigned char)text[i] == 127) { return false; } }
    return true;
}
static void root_add(Roots *roots, const char *path)
{
    if (!path || !clean(path, TB_PROFILE_PATH) || roots->count == 48) { return; }
    for (size_t i = 0; i < roots->count; ++i) { if (strcmp(path, roots->path[i]) == 0) { return; } }
    (void)snprintf(roots->path[roots->count++], TB_PROFILE_PATH, "%s", path);
}
static void roots_get(Roots *roots, const TbProfileWords *search)
{
    memset(roots, 0, sizeof *roots); root_add(roots, ".");
    for (size_t i = 0; search && i < search->count && i < TB_PROFILE_WORDS; ++i) { if (clean(search->values[i], TB_PROFILE_WORD)) { root_add(roots, search->values[i]); } }
    root_add(roots, getenv("OPENOCD_SCRIPTS"));
    char path[TB_PROFILE_PATH]; const char *xdg = getenv("XDG_CONFIG_HOME"), *home = getenv("HOME");
    if (xdg && *xdg) { if (snprintf(path, sizeof path, "%s/openocd", xdg) < (int)sizeof path) { root_add(roots, path); } }
    else if (home && snprintf(path, sizeof path, "%s/.config/openocd", home) < (int)sizeof path) { root_add(roots, path); }
    if (home && snprintf(path, sizeof path, "%s/.openocd", home) < (int)sizeof path) { root_add(roots, path); }
    root_add(roots, "/usr/local/share/openocd/site"); root_add(roots, "/usr/local/share/openocd/scripts");
    root_add(roots, "/usr/share/openocd/site"); root_add(roots, "/usr/share/openocd/scripts");
}
static bool readable(const char *path)
{ struct stat st; return stat(path, &st) == 0 && S_ISREG(st.st_mode) && access(path, R_OK) == 0; }
static bool script_exists(const Roots *roots, const char *script)
{
    if (!clean(script, TB_PROFILE_PATH)) { return false; }
    if (*script == '/') { return readable(script); }
    for (size_t i = 0; i < roots->count; ++i) {
        char path[TB_PROFILE_PATH * 2];
        if (snprintf(path, sizeof path, "%s/%s", roots->path[i], script) < (int)sizeof path && readable(path)) { return true; }
    }
    return false;
}
static void scan(TbOpenOcdCatalog *c, const char *directory, int kind, unsigned depth, size_t *budget)
{
    if (depth > 8 || !*budget || c->count == TB_OPENOCD_SCRIPTS) { c->truncated = true; return; }
    DIR *dir = opendir(directory); if (!dir) { return; }
    struct dirent *entry;
    while ((entry = readdir(dir))) {
        if (!strcmp(entry->d_name, ".") || !strcmp(entry->d_name, "..")) { continue; }
        if (!*budget || c->count == TB_OPENOCD_SCRIPTS) { c->truncated = true; break; } --*budget;
        char path[TB_PROFILE_PATH];
        if (snprintf(path, sizeof path, "%s/%s", directory, entry->d_name) >= (int)sizeof path) { c->truncated = true; continue; }
        struct stat st; if (lstat(path, &st) != 0) { continue; }
        if (S_ISDIR(st.st_mode)) { scan(c, path, kind, depth + 1, budget); }
        else if ((S_ISREG(st.st_mode) || S_ISLNK(st.st_mode)) && clean(path, sizeof path) && readable(path)) {
            size_t n = strlen(path); if (n < 4 || strcmp(path + n - 4, ".cfg") != 0) { continue; }
            bool duplicate = false;
            for (size_t i = 0; i < c->count; ++i) { duplicate = duplicate || strcmp(c->scripts[i].path, path) == 0; }
            if (!duplicate) { c->scripts[c->count].kind = kind; (void)snprintf(c->scripts[c->count++].path, TB_PROFILE_PATH, "%s", path); }
        }
    }
    (void)closedir(dir);
}
static int script_compare(const void *a, const void *b)
{ return strcmp(((const TbOpenOcdScript *)a)->path, ((const TbOpenOcdScript *)b)->path); }
void tb_openocd_catalog(TbOpenOcdCatalog *c, const TbProfileWords *search)
{
    memset(c, 0, sizeof *c); Roots roots; roots_get(&roots, search); size_t budget = 4096;
    static const char *const kinds[] = {"board", "interface", "target"};
    for (size_t i = 0; i < roots.count; ++i) {
        for (int k = 0; k < 3; ++k) {
            char directory[TB_PROFILE_PATH];
            if (snprintf(directory, sizeof directory, "%s/%s", roots.path[i], kinds[k]) < (int)sizeof directory) { scan(c, directory, k, 0, &budget); }
        }
    }
    qsort(c->scripts, c->count, sizeof c->scripts[0], script_compare);
}
static bool add(TbOpenOcdPlan *p, const char *word)
{
    if (p->count == TB_OPENOCD_ARGS || !clean(word, TB_PROFILE_PATH)) { return false; }
    (void)snprintf(p->argv[p->count++], TB_PROFILE_PATH, "%s", word); return true;
}
bool tb_openocd_plan(const TbProfile *p, TbOpenOcdPlan *plan, char *error, size_t size)
{
    memset(plan, 0, sizeof *plan); error[0] = 0;
    if (!clean(p->endpoint, sizeof p->endpoint)) { return fail(error, size, "Enter a valid GDB endpoint."); }
    (void)snprintf(plan->endpoint, sizeof plan->endpoint, "%s", p->endpoint); plan->start_server = p->start_server;
    if (!p->start_server) { return true; }
    const char *port;
    if (!strncmp(p->endpoint, "localhost:", 10) || !strncmp(p->endpoint, "127.0.0.1:", 10)) { port = p->endpoint + 10; }
    else { return fail(error, size, "Managed OpenOCD needs a local localhost:port or 127.0.0.1:port endpoint."); }
    if (!*port) { return fail(error, size, "Enter a TCP port from 1 to 65535."); }
    unsigned value = 0;
    for (; *port; ++port) {
        if (*port < '0' || *port > '9' || value > (65535U - (unsigned)(*port - '0')) / 10U) { return fail(error, size, "Enter a TCP port from 1 to 65535."); }
        value = value * 10U + (unsigned)(*port - '0');
    }
    if (!value) { return fail(error, size, "Enter a TCP port from 1 to 65535."); } plan->port = value;
    (void)snprintf(plan->endpoint, sizeof plan->endpoint, "127.0.0.1:%u", value);
    if (!add(plan, p->openocd) || !add(plan, "-c") || !add(plan, "bindto 127.0.0.1") ||
        !add(plan, "-c") || !add(plan, "tcl_port disabled") || !add(plan, "-c") || !add(plan, "telnet_port disabled")) { return fail(error, size, "Invalid OpenOCD executable or argument limit."); }
    if (p->search.count > TB_PROFILE_WORDS || p->scripts.count > TB_PROFILE_WORDS) { return fail(error, size, "Too many scripts/search paths."); }
    for (size_t i = 0; i < p->search.count; ++i) {
        struct stat st;
        if (!clean(p->search.values[i], TB_PROFILE_WORD) || stat(p->search.values[i], &st) || !S_ISDIR(st.st_mode) ||
            !add(plan, "-s") || !add(plan, p->search.values[i])) { return fail(error, size, "Search directory is absent, invalid or exceeds the argument limit."); }
    }
    Roots roots; roots_get(&roots, &p->search);
    const char *scripts[TB_PROFILE_WORDS]; size_t count = p->scripts.count;
    for (size_t i = 0; i < count; ++i) {
        if (!clean(p->scripts.values[i], TB_PROFILE_WORD)) { return fail(error, size, "Invalid script path."); }
        scripts[i] = p->scripts.values[i];
    }
    if (!count) {
        scripts[count++] = p->adapter_target ? p->adapter : p->board;
        if (p->adapter_target) { scripts[count++] = p->target; }
    }
    for (size_t i = 0; i < count; ++i) {
        if (!script_exists(&roots, scripts[i])) { return fail(error, size, "Script not found/readable. Choose an installed script or add its search directory; # and control bytes are unsupported."); }
        for (size_t j = 0; j < i; ++j) { if (!strcmp(scripts[i], scripts[j])) { return fail(error, size, "Duplicate explicit script. Board dependencies are never appended automatically."); } }
        if (!add(plan, "-f") || !add(plan, scripts[i])) { return fail(error, size, "OpenOCD argument limit exceeded."); }
    }
    char command[48]; (void)snprintf(command, sizeof command, "gdb_port %u", value);
    if (!add(plan, "-c") || !add(plan, command)) { return fail(error, size, "OpenOCD argument limit exceeded."); }
    return true;
}

struct TbOpenOcd {
    pid_t pid; int fd; bool ready, failed, stopping, killed;
    unsigned port; uint64_t deadline;
    char partial[512], message[192]; size_t used;
    TbOpenOcdLog log; void *context;
};
TbOpenOcd *tb_openocd_create(TbOpenOcdLog log, void *context)
{
    TbOpenOcd *s = calloc(1, sizeof *s); if (s) { s->fd = -1; s->log = log; s->context = context; } return s;
}
bool tb_openocd_active(const TbOpenOcd *s) { return s && s->pid > 0; }
bool tb_openocd_ready(const TbOpenOcd *s) { return s && s->ready && !s->failed && !s->stopping; }
bool tb_openocd_failed(const TbOpenOcd *s) { return s && s->failed; }
const char *tb_openocd_message(const TbOpenOcd *s) { return s->message; }
int tb_openocd_fd(const TbOpenOcd *s) { return s ? s->fd : -1; }
bool tb_openocd_destroy(TbOpenOcd *s)
{ if (tb_openocd_active(s)) { return false; } if (s) { if (s->fd >= 0) { (void)close(s->fd); } free(s); } return true; }
static void report(TbOpenOcd *s, const char *message)
{ (void)snprintf(s->message, sizeof s->message, "%s", message); if (s->log) { s->log(s->context, s->message); } }
static void reap(TbOpenOcd *s)
{
    if (s->pid <= 0) { return; }
    int status = 0; pid_t result = waitpid(s->pid, &status, WNOHANG);
    if (result == s->pid || (result < 0 && errno == ECHILD)) {
        s->pid = 0; s->ready = false;
        if (!s->stopping) { s->failed = true; report(s, "OpenOCD exited unexpectedly; inspect server logs for script, USB/probe, target or port errors."); }
    }
}
void tb_openocd_stop(TbOpenOcd *s)
{
    if (!s || s->stopping) { return; } reap(s); s->stopping = true; s->ready = false;
    if (s->pid > 0) { (void)kill(s->pid, SIGTERM); s->deadline = now() + 300; }
}
bool tb_openocd_start(TbOpenOcd *s, const TbOpenOcdPlan *plan)
{
    if (!s || tb_openocd_active(s) || !plan->start_server || !plan->count || plan->count > TB_OPENOCD_ARGS || !plan->port || plan->port > 65535) { return false; }
    for (size_t i = 0; i < plan->count; ++i) { if (!clean(plan->argv[i], TB_PROFILE_PATH)) { return false; } }
    s->ready = s->failed = s->stopping = s->killed = false; s->used = 0; s->port = plan->port;
    if (s->fd >= 0) { (void)close(s->fd); s->fd = -1; }
    int probe = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    struct sockaddr_in address = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK), .sin_port = htons((uint16_t)plan->port)};
    if (probe < 0 || bind(probe, (struct sockaddr *)&address, sizeof address) != 0) {
        if (probe >= 0) { (void)close(probe); } s->failed = true; report(s, "GDB port unavailable. Choose a free port or connect to the existing server explicitly."); return false;
    }
    int output[2];
    if (pipe2(output, O_CLOEXEC) != 0) { (void)close(probe); s->failed = true; report(s, "Cannot create OpenOCD log pipe."); return false; }
    int flags = fcntl(output[0], F_GETFL);
    if (flags < 0 || fcntl(output[0], F_SETFL, (int)((unsigned)flags | (unsigned)O_NONBLOCK)) != 0) {
        (void)close(probe); (void)close(output[0]); (void)close(output[1]); s->failed = true; report(s, "Cannot configure the nonblocking OpenOCD log pipe."); return false;
    }
    char *argv[TB_OPENOCD_ARGS + 1];
    for (size_t i = 0; i < plan->count; ++i) { argv[i] = (char *)plan->argv[i]; } argv[plan->count] = NULL;
    posix_spawn_file_actions_t actions; posix_spawnattr_t attr;
    int error = posix_spawn_file_actions_init(&actions);
    if (error) { (void)close(probe); (void)close(output[0]); (void)close(output[1]); return false; }
    error = posix_spawnattr_init(&attr);
    if (error) { (void)posix_spawn_file_actions_destroy(&actions); (void)close(probe); (void)close(output[0]); (void)close(output[1]); return false; }
    sigset_t mask, defaults; (void)sigemptyset(&mask); (void)sigemptyset(&defaults);
    (void)sigaddset(&defaults, SIGPIPE); (void)sigaddset(&defaults, SIGINT); (void)sigaddset(&defaults, SIGTERM);
    if (!error) { error = posix_spawnattr_setsigmask(&attr, &mask); }
    if (!error) { error = posix_spawnattr_setsigdefault(&attr, &defaults); }
    if (!error) { error = posix_spawnattr_setflags(&attr, POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF); }
    if (!error) { error = posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0); }
    if (!error) { error = posix_spawn_file_actions_adddup2(&actions, output[1], STDOUT_FILENO); }
    if (!error) { error = posix_spawn_file_actions_adddup2(&actions, output[1], STDERR_FILENO); }
    if (!error) { error = posix_spawn_file_actions_addclosefrom_np(&actions, 3); }
    (void)close(probe); /* A competing bind remains possible; own-child readiness is required too. */
    if (!error) { error = posix_spawnp(&s->pid, argv[0], &actions, &attr, argv, environ); }
    (void)posix_spawnattr_destroy(&attr); (void)posix_spawn_file_actions_destroy(&actions); (void)close(output[1]);
    if (error) {
        (void)close(output[0]); s->pid = 0; s->failed = true;
        char text[192]; (void)snprintf(text, sizeof text, "Cannot start OpenOCD: %s", strerror(error)); report(s, text); return false;
    }
    s->fd = output[0]; s->deadline = now() + 5000; report(s, "Starting owned OpenOCD. Script actions are defined by the reviewed configuration."); return true;
}
static void output_line(TbOpenOcd *s)
{
    s->partial[s->used] = 0;
    const char *listen = strstr(s->partial, "Listening on port ");
    if (listen && !s->stopping) {
        char *end; unsigned long port = strtoul(listen + 18, &end, 10);
        if (port == s->port && !strncmp(end, " for gdb connections", 20)) { s->ready = true; }
    }
    char safe[2048]; size_t at = 0;
    for (size_t i = 0; i < s->used && at + 5 < sizeof safe; ++i) {
        unsigned char c = (unsigned char)s->partial[i];
        if (c >= 32 && c < 127) { safe[at++] = (char)c; }
        else { int n = snprintf(safe + at, sizeof safe - at, "\\x%02x", c); if (n > 0) { at += (size_t)n; } }
    }
    safe[at] = 0; if (s->log && at) { s->log(s->context, safe); } s->used = 0;
}
void tb_openocd_pump(TbOpenOcd *s)
{
    if (!s) { return; }
    char bytes[1024]; size_t budget = 8192;
    while (s->fd >= 0 && budget) {
        ssize_t n = read(s->fd, bytes, budget < sizeof bytes ? budget : sizeof bytes);
        if (n < 0) { if (errno == EINTR) { break; } if (errno != EAGAIN && errno != EWOULDBLOCK) { (void)close(s->fd); s->fd = -1; } break; }
        if (!n) { (void)close(s->fd); s->fd = -1; if (s->used) { output_line(s); } break; }
        budget -= (size_t)n;
        for (ssize_t i = 0; i < n; ++i) {
            if (bytes[i] == '\n' || s->used == sizeof s->partial - 1) { output_line(s); }
            if (bytes[i] != '\n') { s->partial[s->used++] = bytes[i]; }
        }
    }
    reap(s);
    if (s->pid > 0 && !s->ready && !s->stopping && now() >= s->deadline) { s->failed = true; report(s, "OpenOCD startup timed out before this child's GDB listener was confirmed."); tb_openocd_stop(s); }
    if (s->pid > 0 && s->stopping && now() >= s->deadline && !s->killed) { (void)kill(s->pid, SIGKILL); s->killed = true; }
}
