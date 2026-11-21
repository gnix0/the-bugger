#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Linux pidfd and fork/exec isolation. */
#include "symbol/process.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <limits.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <unistd.h>

extern char **environ;

static uint64_t after(uint64_t now, uint64_t delay)
{
    return delay > UINT64_MAX - now ? UINT64_MAX : now + delay;
}


bool tb_symbol_process_init(TbSymbolProcess_t *process, uint64_t cancel_grace_ns,
                            uint64_t term_grace_ns)
{
    if (!process || !cancel_grace_ns || !term_grace_ns)
        return false;

    *process = (TbSymbolProcess_t){
        .pidfd           = -1,
        .state           = TB_SYMBOL_PROCESS_IDLE,
        .cancel_grace_ns = cancel_grace_ns,
        .term_grace_ns   = term_grace_ns
    };
    return true;
}


bool tb_symbol_process_released(const TbSymbolProcess_t *process)
{
    return process && !process->wait_owned && process->pidfd < 0;
}


static void child_exec(const char *path, int socket_fd)
{
    /* Only async-signal-safe setup occurs between fork and exec. In particular,
     * opening/loading the executable cannot suspend the supervising parent. */
    struct sigaction defaults = {0};
    defaults.sa_handler = SIG_DFL;
    (void)sigemptyset(&defaults.sa_mask);
    const int signals[] = {SIGPIPE, SIGINT, SIGTERM, SIGHUP, SIGCHLD};
    for (size_t i = 0; i < sizeof(signals) / sizeof(signals[0]); ++i) {
        if (sigaction(signals[i], &defaults, NULL))
            _exit(127);
    }
    sigset_t empty;
    (void)sigemptyset(&empty);
    if (sigprocmask(SIG_SETMASK, &empty, NULL) || setpgid(0, 0))
        _exit(127);

    /* Protect sources before installing stdio, including when 0/1/2 were closed
     * at launch. Otherwise a dup2 could overwrite a later action's source. */
    int channel = fcntl(socket_fd, F_DUPFD_CLOEXEC, 4);
    if (channel < 0)
        _exit(127);
    int null_fd = open("/dev/null", O_RDWR | O_CLOEXEC);
    if (null_fd < 0)
        _exit(127);
    int null_source = fcntl(null_fd, F_DUPFD_CLOEXEC, 4);
    if (null_source < 0)
        _exit(127);
    for (int target = 0; target < 3; ++target) {
        if (dup2(null_source, target) < 0)
            _exit(127);
    }
    if (dup2(channel, 3) < 0 || syscall(SYS_close_range, 4U, UINT_MAX, 0U) < 0)
        _exit(127);
    char *const arguments[] = {(char *)path, "--symbol-worker", NULL};
    (void)execve(path, arguments, environ);
    _exit(127);
}


bool tb_symbol_process_start(TbSymbolProcess_t *process, const char *path, int socket_fd)
{
    if (!tb_symbol_process_released(process) ||
        (process->state != TB_SYMBOL_PROCESS_IDLE && process->state != TB_SYMBOL_PROCESS_REAPED) ||
        !path || !*path || socket_fd < 0)
        return false;

    struct sigaction disposition;
    if (sigaction(SIGCHLD, NULL, &disposition) < 0) {
        process->error = errno;
        return false;
    }
    if (disposition.sa_handler != SIG_DFL ||
        ((unsigned)disposition.sa_flags & (unsigned)SA_NOCLDWAIT)) {
        process->error = ENOTSUP;
        return false;
    }

    /* Check support before spawning and reserve descriptor capacity. Default
     * SIGCHLD and exclusive wait ownership keep an exited child unrecycled until
     * pidfd_open or our waitpid; no numeric-PID signalling fallback is used. */
    int probe = (int)syscall(SYS_pidfd_open, getpid(), 0U);
    if (probe < 0) {
        process->error = errno;
        return false;
    }
    /* No valid descriptor can have UINT_MAX as its signed-int descriptor
     * number. Probe isolation support without changing an existing descriptor. */
    if (syscall(SYS_close_range, UINT_MAX, UINT_MAX, 0U) < 0) {
        process->error = errno;
        (void)close(probe);
        return false;
    }
    pid_t child = fork();
    if (!child)
        child_exec(path, socket_fd);
    int fork_error = errno;
    (void)close(probe);
    if (child < 0) {
        process->error = fork_error;
        return false;
    }
    process->wait_owned  = true;
    process->pid         = child;
    process->pidfd       = (int)syscall(SYS_pidfd_open, child, 0U);
    process->state       = process->pidfd < 0 ? TB_SYMBOL_PROCESS_UNCERTAIN : TB_SYMBOL_PROCESS_RUNNING;
    process->error       = process->pidfd < 0 ? errno : 0;
    process->wait_status = 0;
    process->deadline_ns = 0;
    return process->state == TB_SYMBOL_PROCESS_RUNNING;
}


void tb_symbol_process_stop(TbSymbolProcess_t *process, uint64_t now_ns)
{
    if (process && process->state == TB_SYMBOL_PROCESS_RUNNING) {
        process->state       = TB_SYMBOL_PROCESS_STOPPING;
        process->deadline_ns = after(now_ns, process->cancel_grace_ns);
    }
}


void tb_symbol_process_pump(TbSymbolProcess_t *process, uint64_t now_ns)
{
    if (!process || !process->wait_owned)
        return;

    int   status;
    pid_t result = waitpid(process->pid, &status, WNOHANG);
    if (result == process->pid) {
        /* A failed initial identity acquisition is still unavailable even after
         * cleanup. Its uncertainty must not become implicit restart permission. */
        bool uncertain = process->state == TB_SYMBOL_PROCESS_UNCERTAIN;

        process->wait_status = status;
        process->pid         = 0;
        process->wait_owned  = false;
        if (process->pidfd >= 0)
            (void)close(process->pidfd);
        process->pidfd = -1;
        process->state = uncertain ? TB_SYMBOL_PROCESS_UNCERTAIN : TB_SYMBOL_PROCESS_REAPED;
        return;
    }
    if (result < 0) {
        if (errno != EINTR) {
            process->error = errno;
            process->state = TB_SYMBOL_PROCESS_UNCERTAIN;
            if (errno == ECHILD) {
                /* Ownership was lost. Never wait on a possibly recycled PID. */
                process->wait_owned = false;
                if (process->pidfd >= 0)
                    (void)close(process->pidfd);
                process->pidfd = -1;
            }
        }
        return;
    }
    if ((process->state != TB_SYMBOL_PROCESS_STOPPING && process->state != TB_SYMBOL_PROCESS_TERM) ||
        now_ns < process->deadline_ns) {
        return;
    }

    int signal_number = process->state == TB_SYMBOL_PROCESS_STOPPING ? SIGTERM : SIGKILL;
    if (process->pidfd < 0 || syscall(SYS_pidfd_send_signal, process->pidfd, signal_number, NULL, 0U) < 0) {
        if (process->pidfd < 0 || errno != ESRCH) {
            process->error = process->pidfd < 0 ? EBADF : errno;
            process->state = TB_SYMBOL_PROCESS_UNCERTAIN;
            return;
        }
    }
    process->state       = signal_number == SIGTERM ? TB_SYMBOL_PROCESS_TERM : TB_SYMBOL_PROCESS_KILL;
    process->deadline_ns = after(now_ns, process->term_grace_ns);
}
