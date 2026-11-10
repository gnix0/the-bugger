#define _POSIX_C_SOURCE 200809L
#include "tui/preferences.h"
#include "../support/check.h"
#include "../support/terminal.h"

#include <errno.h>
#include <signal.h>
#include <string.h>
#include <sys/wait.h>
#include <unistd.h>

static char tmux_socket[80];
static char tmux_config[] = "/tmp/tb-f05-tmux-XXXXXX";
static char config_home[] = "/tmp/tb-f05-config-XXXXXX";
static TbPreferences_t preferences;

static void cleanup(void)
{
    if (*tmux_socket) {
        pid_t child = fork();
        if (child == 0) {
            if (!freopen("/dev/null", "w", stderr))
                _exit(127);

            execlp("tmux", "tmux", "-L", tmux_socket, "kill-server", (char *)NULL);
            _exit(127);
        }
        if (child > 0) {
            while (waitpid(child, NULL, 0) < 0 && errno == EINTR) { }
        }
        (void)unlink(tmux_config);
    }
    (void)unlink(preferences.path);
    (void)rmdir(preferences.directory);
    (void)rmdir(config_home);
}


static void start(TestTerminal *term, const char *binary)
{
    if (!getenv("TB_REGISTER_TEST_TMUX")) {
        char *const args[] = {(char *)binary, NULL};
        test_terminal_start_argv(term, args, "xterm-256color", false);
        return;
    }

    (void)snprintf(tmux_socket, sizeof(tmux_socket), "tb-f05-%ld", (long)getpid());
    int fd = mkstemp(tmux_config);
    CHECK(fd >= 0);
    const char config[] = "set -g mouse on\nset -g status off\nset -g default-terminal tmux-256color\n";
    CHECK(write(fd, config, sizeof(config) - 1) == (ssize_t)(sizeof(config) - 1));
    CHECK(close(fd) == 0);
    CHECK(unsetenv("TMUX") == 0);

    char *args[] = {
        "tmux", "-L", tmux_socket, "-f", tmux_config,
        "new-session", "-s", "register-edit", (char *)binary, NULL
    };
    test_terminal_start_argv(term, args, "xterm-256color", false);
}


static void prepared(TestTerminal *term)
{
    static bool keyboard_checked;
    if (!keyboard_checked) {
        test_terminal_click(term, "rax");
        test_terminal_send(term, "\033OB\033OA\r");
        keyboard_checked = true;
    } else {
        test_terminal_click(term, "Edit");
    }
    test_terminal_expect(term, "CPU register edit");
    test_terminal_expect(term, "GDB reports an editable register object");
}


static void input(TestTerminal *term, const char *value, bool vim)
{
    test_terminal_send(term, vim ? "i\025" : "\025");
    test_terminal_send(term, value);
    test_terminal_send(term, "\r");
    test_terminal_expect(term, "Review CPU register");
}


static void apply(TestTerminal *term)
{
    for (int page = 0; !test_terminal_find(term, "[ Apply ]", NULL, NULL); ++page) {
        CHECK(page < 30);
        test_terminal_expect(term, "[ Next ]");
        test_terminal_click(term, "[ Next ]");
    }
    test_terminal_click(term, "[ Apply ]");
    test_terminal_expect(term, "Write acknowledged");
    test_terminal_pump(term, 250);
    for (int page = 0; !test_terminal_find(term, "[ Verify ]", NULL, NULL); ++page) {
        CHECK(page < 30);
        test_terminal_expect(term, "[ Next ]");
        test_terminal_click(term, "[ Next ]");
    }
    test_terminal_click(term, "[ Verify ]");
    test_terminal_expect(term, "registers");
    test_terminal_pump(term, 300);
}


int main(int argc, char **argv)
{
    CHECK(argc == 3);
    CHECK(mkdtemp(config_home));
    CHECK(setenv("XDG_CONFIG_HOME", config_home, 1) == 0);
    CHECK(setenv("TB_TEST_CONFIG_HOME", config_home, 1) == 0);
    CHECK(tb_preferences_load(&preferences));
    bool vim = getenv("TB_REGISTER_TEST_VIM") != NULL;
    CHECK(tb_preferences_save_input(&preferences, preferences.keys, vim));
    CHECK(atexit(cleanup) == 0);

    TestTerminal term;
    start(&term, argv[1]);
    test_terminal_expect(&term, "Native Linux");
    test_terminal_send(&term, "\r");
    test_terminal_expect(&term, "Executable / ELF");
    test_terminal_send(&term, "\t");
    test_terminal_send(&term, vim ? "i\025" : "\025");
    test_terminal_send(&term, argv[2]);
    test_terminal_click(&term, "Review session");
    test_terminal_click(&term, "Start native");
    test_terminal_expect(&term, "LIVE | Loaded");
    test_terminal_lower(&term, 2, true);
    test_terminal_click(&term, "Command");
    test_terminal_send(&term, vim ? "istart\r" : "start\r");
    test_terminal_lower(&term, 2, false);
    test_terminal_expect(&term, "LIVE | registers");
    test_terminal_pump(&term, 400);

    prepared(&term);
    int x, y;
    CHECK(test_terminal_find(&term, "Old: ", &x, &y));
    char original[19];
    CHECK(sscanf(term.cells[y] + x + 5, "%18s", original) == 1);
    input(&term, "0x2\b1", vim);
    test_terminal_send(&term, "\033");
    test_terminal_expect(&term, "LIVE | registers");
    test_terminal_pump(&term, 300);

    prepared(&term);
    CHECK(test_terminal_find(&term, "Old: ", &x, &y));
    CHECK(strncmp(term.cells[y] + x + 5, original, strlen(original)) == 0);
    input(&term, "0x1", vim);
    test_terminal_send(&term, "\021");
    test_terminal_expect(&term, "Terminate the launched program");
    test_terminal_click(&term, "[ Cancel ]");
    test_terminal_expect(&term, "LIVE | registers");
    test_terminal_pump(&term, 300);

    prepared(&term);
    test_terminal_resize(&term, 20, 80);
    input(&term, "0x1", vim);
    CHECK(!test_terminal_find(&term, "[ Apply ]", NULL, NULL));
    test_terminal_click(&term, "[ Next ]");
    test_terminal_resize(&term, 20, 96);
    test_terminal_expect(&term, "Target architecture:");
    test_terminal_resize(&term, 20, 80);
    test_terminal_expect(&term, "Target architecture:");
    CHECK(!test_terminal_find(&term, "[ Apply ]", NULL, NULL));
    apply(&term);
    test_terminal_resize(&term, 44, 140);
    test_terminal_expect(&term, "* rax");

    prepared(&term);
    CHECK(test_terminal_find(&term, "Old: 0x0000000000000001", NULL, NULL));
    input(&term, original, vim);
    apply(&term);
    test_terminal_send(&term, "\021");
    test_terminal_expect(&term, "Terminate the launched program");
    test_terminal_click(&term, "Close session");
    test_terminal_stop(&term, 0);
    puts("PASS F05 native PTY register Review/Cancel/Apply, literal editing, complete compact review, explicit Verify, restoration and terminal cleanup");
    return EXIT_SUCCESS;
}
