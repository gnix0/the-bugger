#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "the_bugger/version.h"
#include "the_bugger/tui.h"

static void print_help(void)
{
    puts("Usage: the-bugger [--help | --version]\n"
         "\n"
         "Terminal-native C/C++ debugger frontend (under development).\n"
         "\n"
         "  --help       Show this help and exit\n"
         "  --version    Show the target version and exit\n"
         "\n"
         "Without arguments, open the demo launcher in a terminal.\n"
         "All workspace data is simulated; debugging is not implemented yet.");
}

int main(int argc, char *argv[])
{
    if (argc == 1) {
        return tb_tui_run();
    }

    if (argc == 2 && strcmp(argv[1], "--version") == 0) {
        puts("the-bugger " THE_BUGGER_VERSION);
        return EXIT_SUCCESS;
    }

    if (argc == 2 && strcmp(argv[1], "--help") == 0) {
        print_help();
        return EXIT_SUCCESS;
    }

    fputs("the-bugger: expected no arguments, --help, or --version.\n"
          "Run the-bugger --help for usage.\n", stderr);
    return EXIT_FAILURE;
}
