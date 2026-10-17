#include "../support/check.h"

#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static void run(const char *binary, char *const arguments[], int expected,
                 const char *output, bool error)
{
    int out[2], err[2];
    CHECK(pipe(out) == 0 && pipe(err) == 0);
    pid_t child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        (void)close(out[0]); (void)close(err[0]);
        CHECK(dup2(out[1], STDOUT_FILENO) >= 0 && dup2(err[1], STDERR_FILENO) >= 0);
        (void)close(out[1]); (void)close(err[1]);
        (void)unsetenv("TERM");
        execv(binary, arguments);
        _exit(127);
    }
    (void)close(out[1]); (void)close(err[1]);
    int status;
    CHECK(waitpid(child, &status, 0) == child);
    char stdout_text[4096] = {0}, stderr_text[4096] = {0};
    CHECK(read(out[0], stdout_text, sizeof stdout_text - 1) >= 0);
    CHECK(read(err[0], stderr_text, sizeof stderr_text - 1) >= 0);
    (void)close(out[0]); (void)close(err[0]);
    if (!WIFEXITED(status) || WEXITSTATUS(status) != expected) {
        fprintf(stderr, "%s\n%s\n", stdout_text, stderr_text);
        CHECK(false);
    }
    CHECK(strstr(error ? stderr_text : stdout_text, output) != NULL);
    CHECK((error ? stdout_text : stderr_text)[0] == '\0');
    CHECK(strstr(stdout_text, "\033") == NULL && strstr(stderr_text, "\033") == NULL);
}

int main(int argc, char *argv[])
{
    CHECK(argc == 2);
    char *const version[] = {argv[1], "--version", NULL};
    char *const help[] = {argv[1], "--help", NULL};
    char *const bare[] = {argv[1], NULL};
    char *const invalid[] = {argv[1], "--unknown", NULL};
    char *const extra[] = {argv[1], "--version", "--help", NULL};
    char *const hostile[] = {argv[1], "$(false);\033[2J", NULL};
    run(argv[1], version, 0, "the-bugger 0.1.0\n", false);
    run(argv[1], help, 0, "native launch/attach", false);
    run(argv[1], bare, 1, "interactive terminal", true);
    run(argv[1], invalid, 1, "--help", true);
    run(argv[1], extra, 1, "--help", true);
    run(argv[1], hostile, 1, "--help", true);
    puts("PASS CLI help/version, non-TTY diagnostics and invalid/literal arguments");
    return EXIT_SUCCESS;
}
