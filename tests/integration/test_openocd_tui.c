#define _POSIX_C_SOURCE 200809L
#include "../support/check.h"
#include "../support/terminal.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
static TestTerminal term;
static void configure(const char *binary, const char *helper, const char *peer)
{
    char *const args[] = {(char *)binary, NULL}; test_terminal_start_argv(&term, args, "xterm-256color", false);
    test_terminal_resize(&term, 44, 160);
    test_terminal_click(&term, "Embedded target"); test_terminal_expect(&term, "Board script");
    test_terminal_click(&term, "Browse boards"); test_terminal_expect(&term, "Installed OpenOCD scripts");
    test_terminal_click(&term, "[ Filter ]"); test_terminal_expect(&term, "Filter installed scripts");
    test_terminal_send(&term, "\025selected-m16.cfg\r");
    test_terminal_expect(&term, "/board/selected-m16.cfg"); test_terminal_click(&term, "/board/selected-m16.cfg");
    test_terminal_expect(&term, "Configure session");
    /* Board mode: mode, board, symbols, GDB, endpoint, OpenOCD, ownership. */
    test_terminal_send(&term, "\t\t\025\t\025"); test_terminal_send(&term, peer);
    int fd = socket(AF_INET, SOCK_STREAM, 0); CHECK(fd >= 0);
    struct sockaddr_in addr = {.sin_family = AF_INET, .sin_addr.s_addr = htonl(INADDR_LOOPBACK)};
    CHECK(bind(fd, (struct sockaddr *)&addr, sizeof addr) == 0); socklen_t n = sizeof addr;
    CHECK(getsockname(fd, (struct sockaddr *)&addr, &n) == 0); CHECK(close(fd) == 0);
    char endpoint[64]; (void)snprintf(endpoint, sizeof endpoint, "127.0.0.1:%u", ntohs(addr.sin_port));
    test_terminal_send(&term, "\t\025"); test_terminal_send(&term, endpoint);
    test_terminal_send(&term, "\t\025"); test_terminal_send(&term, helper);
    test_terminal_click(&term, "Review session"); test_terminal_expect(&term, "Review OpenOCD session");
    test_terminal_expect(&term, "Start owned OpenOCD"); test_terminal_expect(&term, "argv[0]");
    test_terminal_expect(&term, "selected-m16.cfg");
}
int main(int argc, char **argv)
{
    CHECK(argc == 4);
    char root[] = "/tmp/tb-m16ui-XXXXXX"; CHECK(mkdtemp(root));
    char dir[256], script[256]; (void)snprintf(dir, sizeof dir, "%s/board", root); CHECK(mkdir(dir, 0700) == 0);
    (void)snprintf(script, sizeof script, "%s/board/selected-m16.cfg", root);
    int fd = open(script, O_CREAT | O_WRONLY | O_EXCL, 0600); CHECK(fd >= 0 && close(fd) == 0);
    CHECK(setenv("OPENOCD_SCRIPTS", root, 1) == 0 && setenv("XDG_CONFIG_HOME", root, 1) == 0);
    CHECK(setenv("TB_FAKE_MODE", "remote-openocd", 1) == 0 && setenv("TB_FAKE_OPENOCD", "ready", 1) == 0);
    configure(argv[1], argv[2], argv[3]);
    test_terminal_resize(&term, 18, 80); test_terminal_expect(&term, "Review OpenOCD session");
    test_terminal_click(&term, "Next argv"); test_terminal_click(&term, "Previous argv");
    test_terminal_resize(&term, 44, 160);
    test_terminal_click(&term, "Start session"); test_terminal_expect(&term, "LIVE OPENOCD");
    test_terminal_expect(&term, "LIVE | Stopped"); test_terminal_expect(&term, "Halt");
    test_terminal_click(&term, "Reset"); test_terminal_expect(&term, "Reset and halt target?");
    test_terminal_click(&term, "Cancel"); test_terminal_expect(&term, "LIVE OPENOCD");
    test_terminal_click(&term, "Reset"); test_terminal_click(&term, "Reset and halt ]");
    test_terminal_expect(&term, "OpenOCD command completed");
    test_terminal_click(&term, "Halt"); test_terminal_expect(&term, "M16 halt requested");
    test_terminal_resize(&term, 28, 110); test_terminal_expect(&term, "LIVE OPENOCD");
    test_terminal_send(&term, "\021"); test_terminal_expect(&term, "Target on disconnect: Leave halted");
    test_terminal_click(&term, "Target on disconnect"); test_terminal_expect(&term, "Target on disconnect: Resume");
    test_terminal_click(&term, "Close session"); test_terminal_stop(&term, 0);
    /* Signal cleanup during a server that never announces a listener. */
    CHECK(setenv("TB_FAKE_OPENOCD", "hang", 1) == 0);
    configure(argv[1], argv[2], argv[3]); test_terminal_click(&term, "Start session");
    test_terminal_expect(&term, "LIVE OPENOCD"); test_terminal_stop(&term, SIGTERM);
    CHECK(unlink(script) == 0 && rmdir(dir) == 0 && rmdir(root) == 0);
    puts("PASS M16 real TUI installed-script/filter/review, authored OpenOCD startup, reset confirmation, Halt, disconnect policy, resize and signal cleanup");
    return 0;
}
