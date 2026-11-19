#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- pidfd and spawn checks. */
#include "symbol/process.h"
#include "../support/check.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <limits.h>
#include <signal.h>
#include <stdarg.h>
#include <sys/syscall.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

static bool fail_probe;
static bool fail_isolation;
static bool block_exec;
static bool fail_child_identity;
static bool fail_signal;
static bool interrupt_wait;
static unsigned signal_calls;

int __real_execve(const char *path, char *const arguments[], char *const environment[]);
int __wrap_execve(const char *path, char *const arguments[], char *const environment[]);
long __real_syscall(long number, ...);
long __wrap_syscall(long number, ...);
pid_t __real_waitpid(pid_t pid, int *status, int options);
pid_t __wrap_waitpid(pid_t pid, int *status, int options);

long __wrap_syscall(long number, ...)
{
    va_list arguments;
    va_start(arguments, number);
    long result;
    if (number == SYS_pidfd_open) {
        pid_t pid = va_arg(arguments, pid_t);
        unsigned flags = va_arg(arguments, unsigned);
        if ((fail_probe && pid == getpid()) || (fail_child_identity && pid != getpid())) {
            errno = EMFILE;
            result = -1;
        } else {
            result = __real_syscall(number, pid, flags);
        }
    } else if (number == SYS_close_range) {
        unsigned first = va_arg(arguments, unsigned);
        unsigned last = va_arg(arguments, unsigned);
        unsigned flags = va_arg(arguments, unsigned);
        if (fail_isolation) {
            errno = ENOSYS;
            result = -1;
        } else {
            result = __real_syscall(number, first, last, flags);
        }
    } else {
        CHECK(number == SYS_pidfd_send_signal);
        int fd = va_arg(arguments, int);
        int signal_number = va_arg(arguments, int);
        void *info = va_arg(arguments, void *);
        unsigned flags = va_arg(arguments, unsigned);
        ++signal_calls;
        if (fail_signal) {
            errno = EPERM;
            result = -1;
        } else {
            result = __real_syscall(number, fd, signal_number, info, flags);
        }
    }
    va_end(arguments);
    return result;
}


pid_t __wrap_waitpid(pid_t pid, int *status, int options)
{
    if (interrupt_wait) {
        interrupt_wait = false;
        errno = EINTR;
        return -1;
    }
    return __real_waitpid(pid, status, options);
}


int __wrap_execve(const char *path, char *const arguments[], char *const environment[])
{
    if (block_exec) {
        if (write(3, "X", 1) != 1)
            _exit(90);
        struct pollfd fd = {
            .fd     = 3,
            .events = POLLIN
        };
        (void)poll(&fd, 1, 3000);
        errno = ETIMEDOUT;
        return -1;
    }
    return __real_execve(path, arguments, environment);
}


static int worker(void)
{
    const char *mode = getenv("TB_TEST_SYMBOL_PROCESS");
    if (!mode)
        return 20;
    for (int fd = 0; fd < 3; ++fd) {
        struct stat info;
        if (fstat(fd, &info) || !S_ISCHR(info.st_mode))
            return 21;
    }
    if (fcntl(64, F_GETFD) >= 0 || errno != EBADF || getpgrp() != getpid())
        return 22;
    if (strcmp(mode, "ignore") == 0) {
        struct sigaction ignore = {0};
        ignore.sa_handler = SIG_IGN;
        (void)sigemptyset(&ignore.sa_mask);
        if (sigaction(SIGTERM, &ignore, NULL))
            return 23;
    }
    if (write(3, "R", 1) != 1)
        return 24;
    if (strcmp(mode, "exit") == 0)
        return 0;

    for (;;) {
        struct pollfd fd = {
            .fd     = 3,
            .events = POLLIN
        };
        if (poll(&fd, 1, 1000) < 0 && errno != EINTR)
            return 25;
        char byte;
        ssize_t count = read(3, &byte, 1);
        if (!count)
            return 0;
        if (count > 0)
            return byte == 'Q' ? 0 : 26;
        if (errno != EAGAIN && errno != EINTR)
            return 27;
    }
}


static void ready(int socket_fd)
{
    struct pollfd fd = {
        .fd     = socket_fd,
        .events = POLLIN
    };
    CHECK(poll(&fd, 1, 3000) == 1);
    char byte;
    CHECK(read(socket_fd, &byte, 1) == 1 && byte == 'R');
}


static void reap(TbSymbolProcess_t *process, uint64_t now)
{
    for (unsigned i = 0; i < 300 && process->wait_owned; ++i) {
        tb_symbol_process_pump(process, now);
        if (!process->wait_owned)
            break;
        struct pollfd fd = {
            .fd     = process->pidfd,
            .events = POLLIN
        };
        CHECK(poll(&fd, 1, 10) >= 0);
    }
    CHECK(!process->wait_owned && tb_symbol_process_released(process));
}


static void start(TbSymbolProcess_t *process, int sockets[2], const char *mode)
{
    CHECK(setenv("TB_TEST_SYMBOL_PROCESS", mode, 1) == 0);
    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sockets) == 0);
    CHECK(tb_symbol_process_start(process, "/proc/self/exe", sockets[1]));
    CHECK(close(sockets[1]) == 0);
    CHECK(process->state == TB_SYMBOL_PROCESS_RUNNING && process->pidfd >= 0);
    CHECK((fcntl(process->pidfd, F_GETFD) & FD_CLOEXEC) != 0);
    ready(sockets[0]);
}


static void lifecycle(void)
{
    int leaked = open("/dev/null", O_RDONLY);
    CHECK(leaked >= 0 && dup2(leaked, 64) == 64 && close(leaked) == 0);
    TbSymbolProcess_t process;
    CHECK(!tb_symbol_process_init(NULL, 100, 100));
    CHECK(!tb_symbol_process_init(&process, 0, 100));
    CHECK(tb_symbol_process_init(&process, 100, 100));
    CHECK(tb_symbol_process_released(&process));
    CHECK(tb_symbol_process_start(&process, "/does/not/exist", 64));
    reap(&process, 0);
    CHECK(WIFEXITED(process.wait_status) && WEXITSTATUS(process.wait_status) == 127);
    CHECK(tb_symbol_process_released(&process));

    int sockets[2];
    start(&process, sockets, "exit");
    reap(&process, 0);
    CHECK(process.state == TB_SYMBOL_PROCESS_REAPED);
    CHECK(WIFEXITED(process.wait_status) && WEXITSTATUS(process.wait_status) == 0);
    CHECK(close(sockets[0]) == 0);

    start(&process, sockets, "graceful");
    CHECK(!tb_symbol_process_start(&process, "/proc/self/exe", sockets[0]));
    tb_symbol_process_stop(&process, 1000);
    CHECK(process.state == TB_SYMBOL_PROCESS_STOPPING && process.deadline_ns == 1100);
    CHECK(write(sockets[0], "Q", 1) == 1);
    reap(&process, 1000);
    CHECK(WIFEXITED(process.wait_status) && WEXITSTATUS(process.wait_status) == 0);
    CHECK(close(sockets[0]) == 0);

    start(&process, sockets, "ignore");
    tb_symbol_process_stop(&process, 2000);
    tb_symbol_process_pump(&process, 2099);
    CHECK(process.state == TB_SYMBOL_PROCESS_STOPPING);
    tb_symbol_process_pump(&process, 2100);
    CHECK(process.state == TB_SYMBOL_PROCESS_TERM && process.deadline_ns == 2200);
    tb_symbol_process_pump(&process, 2199);
    CHECK(process.state == TB_SYMBOL_PROCESS_TERM);
    tb_symbol_process_pump(&process, 2200);
    CHECK(process.state == TB_SYMBOL_PROCESS_KILL);
    CHECK(!tb_symbol_process_start(&process, "/proc/self/exe", sockets[0]));
    reap(&process, 2200);
    CHECK(WIFSIGNALED(process.wait_status) && WTERMSIG(process.wait_status) == SIGKILL);
    CHECK(close(sockets[0]) == 0 && close(64) == 0);
}


static void lost_reap(void)
{
    TbSymbolProcess_t process;
    CHECK(tb_symbol_process_init(&process, 100, 100));
    /* This test deliberately violates the exclusive-reap contract. */
    int extra = open("/dev/null", O_RDONLY);
    CHECK(extra >= 0 && dup2(extra, 64) == 64 && close(extra) == 0);
    int sockets[2];
    start(&process, sockets, "exit");
    int status;
    CHECK(waitpid(process.pid, &status, 0) == process.pid);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
    tb_symbol_process_pump(&process, 100);
    CHECK(process.state == TB_SYMBOL_PROCESS_UNCERTAIN && process.error == ECHILD);
    CHECK(tb_symbol_process_released(&process));
    CHECK(!tb_symbol_process_start(&process, "/proc/self/exe", sockets[0]));
    tb_symbol_process_pump(&process, 200);
    CHECK(process.state == TB_SYMBOL_PROCESS_UNCERTAIN);
    CHECK(close(sockets[0]) == 0 && close(64) == 0);
}


static void unsafe_reaping(void)
{
    TbSymbolProcess_t process;
    CHECK(tb_symbol_process_init(&process, 100, 100));
    struct sigaction ignore = {0};
    struct sigaction saved;
    ignore.sa_handler = SIG_IGN;
    (void)sigemptyset(&ignore.sa_mask);
    CHECK(sigaction(SIGCHLD, &ignore, &saved) == 0);
    int socket_fd = open("/dev/null", O_RDONLY);
    CHECK(socket_fd >= 0);
    CHECK(!tb_symbol_process_start(&process, "/proc/self/exe", socket_fd));
    CHECK(process.error == ENOTSUP && !process.pid && process.pidfd < 0);
    CHECK(sigaction(SIGCHLD, &saved, NULL) == 0 && close(socket_fd) == 0);
}


static void closed_stdio(void)
{
    pid_t child = fork();
    CHECK(child >= 0);
    if (!child) {
        (void)close(0);
        (void)close(1);
        (void)close(2);
        TbSymbolProcess_t process;
        if (!tb_symbol_process_init(&process, 100, 100))
            _exit(30);
        int sockets[2];
        if (setenv("TB_TEST_SYMBOL_PROCESS", "exit", 1) ||
            socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sockets) ||
            !tb_symbol_process_start(&process, "/proc/self/exe", sockets[1])) {
            _exit(31);
        }
        (void)close(sockets[1]);
        ready(sockets[0]);
        reap(&process, 0);
        _exit(WIFEXITED(process.wait_status) ? WEXITSTATUS(process.wait_status) : 32);
    }
    int status;
    CHECK(waitpid(child, &status, 0) == child);
    CHECK(WIFEXITED(status) && WEXITSTATUS(status) == 0);
}


static void identity_failures(void)
{
    TbSymbolProcess_t process;
    CHECK(tb_symbol_process_init(&process, 100, 100));
    int sockets[2];
    CHECK(setenv("TB_TEST_SYMBOL_PROCESS", "graceful", 1) == 0);
    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sockets) == 0);
    fail_probe = true;
    CHECK(!tb_symbol_process_start(&process, "/proc/self/exe", sockets[1]));
    CHECK(process.error == EMFILE && tb_symbol_process_released(&process));
    fail_probe = false;
    fail_child_identity = true;
    CHECK(!tb_symbol_process_start(&process, "/proc/self/exe", sockets[1]));
    fail_child_identity = false;
    CHECK(close(sockets[1]) == 0);
    CHECK(process.wait_owned && process.pidfd < 0 && process.state == TB_SYMBOL_PROCESS_UNCERTAIN);
    ready(sockets[0]);
    unsigned previous_signals = signal_calls;
    tb_symbol_process_stop(&process, 0);
    tb_symbol_process_pump(&process, UINT64_MAX);
    CHECK(signal_calls == previous_signals && process.wait_owned);
    CHECK(write(sockets[0], "Q", 1) == 1);
    reap(&process, 0);
    CHECK(process.state == TB_SYMBOL_PROCESS_UNCERTAIN);
    CHECK(!tb_symbol_process_start(&process, "/proc/self/exe", sockets[0]));
    CHECK(close(sockets[0]) == 0);

    /* Use a new owner, representing a separately constructed engine after the
     * prior unavailable instance has been explicitly discarded. */
    CHECK(tb_symbol_process_init(&process, 100, 100));
    start(&process, sockets, "ignore");
    interrupt_wait = true;
    tb_symbol_process_pump(&process, 0);
    CHECK(process.wait_owned && process.state == TB_SYMBOL_PROCESS_RUNNING);
    fail_signal = true;
    tb_symbol_process_stop(&process, 0);
    tb_symbol_process_pump(&process, 100);
    fail_signal = false;
    CHECK(process.state == TB_SYMBOL_PROCESS_UNCERTAIN && process.error == EPERM);
    CHECK(write(sockets[0], "Q", 1) == 1);
    reap(&process, 100);
    CHECK(process.state == TB_SYMBOL_PROCESS_UNCERTAIN);
    CHECK(close(sockets[0]) == 0);
}


static void pre_exec_supervision(void)
{
    TbSymbolProcess_t process;
    CHECK(tb_symbol_process_init(&process, 100, 100));
    int sockets[2];
    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0, sockets) == 0);
    fail_isolation = true;
    CHECK(!tb_symbol_process_start(&process, "/proc/self/exe", sockets[1]));
    CHECK(process.error == ENOSYS && tb_symbol_process_released(&process));
    fail_isolation = false;
    block_exec = true;
    CHECK(tb_symbol_process_start(&process, "/proc/self/exe", sockets[1]));
    block_exec = false;
    CHECK(close(sockets[1]) == 0);
    struct pollfd fd = {
        .fd     = sockets[0],
        .events = POLLIN
    };
    CHECK(poll(&fd, 1, 3000) == 1);
    char byte;
    CHECK(read(sockets[0], &byte, 1) == 1 && byte == 'X');
    /* Ownership exists while exec is still blocked in the child. */
    CHECK(process.state == TB_SYMBOL_PROCESS_RUNNING && process.wait_owned && process.pidfd >= 0);
    tb_symbol_process_stop(&process, 0);
    tb_symbol_process_pump(&process, 100);
    reap(&process, 100);
    CHECK(WIFSIGNALED(process.wait_status) && WTERMSIG(process.wait_status) == SIGTERM);
    CHECK(close(sockets[0]) == 0);
}


int main(int argc, char *argv[])
{
    if (argc == 2 && strcmp(argv[1], "--symbol-worker") == 0)
        return worker();

    pre_exec_supervision();
    identity_failures();
    unsafe_reaping();
    closed_stdio();
    lifecycle();
    lost_reap();
    puts("PASS symbol process: isolated spawn, graceful exit, TERM/KILL, owned reap and restart fencing");
    return 0;
}
