#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <time.h>
#include <unistd.h>

int main(int argc, char *argv[])
{
    (void)setvbuf(stdout, NULL, _IONBF, 0);
    (void)setvbuf(stderr, NULL, _IONBF, 0);
    if (argc > 1 && strcmp(argv[1], "--ignore-hup") == 0) { (void)signal(SIGHUP, SIG_IGN); }
    if (argc > 1 && strcmp(argv[1], "--allow-attach") == 0) {
        if (prctl(PR_SET_PTRACER, PR_SET_PTRACER_ANY, 0, 0, 0) != 0) { return 3; }
    } else if (argc > 1 && strcmp(argv[1], "--deny-attach") == 0) {
        if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0) { return 4; }
    }
    printf("FIXTURE PID=%ld\n", (long)getpid());
    char directory[4096];
    printf("CWD=<%s>\n", getcwd(directory, sizeof directory) ? directory : "unavailable");
    for (int i = 1; i < argc; ++i) { printf("arg[%d]=<%s>\n", i, argv[i]); }
    const char *environment = getenv("THE_BUGGER_TEST");
    printf("ENV=<%s>\n", environment ? environment : "unset");
    fprintf(stderr, "STDERR marker\n");
    for (int i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--control-bytes") == 0) {
            printf("CONTROL=\033[2J\033]52;c;not-a-clipboard\aEND\n");
        }
    }
    if (argc > 1 && strcmp(argv[1], "--exit") == 0) { return 0; }
    if (argc > 1 && strcmp(argv[1], "--flood") == 0) {
        for (int i = 0; i < 20000; ++i) { printf("flood %d abcdefghijklmnopqrstuvwxyz\n", i); }
        puts("FLOOD END");
    }
    for (unsigned tick = 0;; ++tick) {
        if (argc > 1 && strcmp(argv[1], "--render-stress") == 0) {
            for (unsigned line = 0; line < 64; ++line) {
                printf("RENDER-STRESS %u/%u abcdefghijklmnopqrstuvwxyz 0123456789 "
                       "abcdefghijklmnopqrstuvwxyz 0123456789 "
                       "abcdefghijklmnopqrstuvwxyz 0123456789\n", tick, line);
            }
            printf("CONTROL=\033[2J\033]52;c;not-a-clipboard\aEND\n");
        }
        printf("tick %u\n", tick);
        const struct timespec delay = {.tv_nsec = 100000000};
        (void)nanosleep(&delay, NULL);
    }
}
