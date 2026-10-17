#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "the_bugger/version.h"
#include "the_bugger/tui.h"

static void print_help(void)
{
    puts("Usage: the-bugger [--help | --version | --demo]\n"
         "\n"
         "Terminal-native C/C++ debugger frontend (under development).\n"
         "\n"
         "  --help       Show this help and exit\n"
         "  --version    Show the target version and exit\n"
         "  --demo       Open the explicitly simulated visual workspace\n"
         "\n"
         "Without arguments, open the native launch/attach launcher.\n"
         "Native execution, code/stack/thread inspection and Console are live.\n"
         "Breakpoints and other inspection remain under development.\n"
         "Embedded/remote configuration remains an explicit preview.");
}

int main(int argc, char *argv[])
{
    if (argc == 1) {
        return tb_tui_run(false);
    }
    if (argc == 2 && strcmp(argv[1], "--demo") == 0) { return tb_tui_run(true); }

    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("the-bugger " THE_BUGGER_VERSION);
        return EXIT_SUCCESS;
    }

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_help();
        return EXIT_SUCCESS;
    }

    fputs("the-bugger: expected no arguments, --demo, --help, or --version.\n"
          "Run the-bugger --help for usage.\n", stderr);
    return EXIT_FAILURE;
}
