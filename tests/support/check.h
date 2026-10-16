#ifndef THE_BUGGER_TEST_CHECK_H
#define THE_BUGGER_TEST_CHECK_H

#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>

#define CHECK(condition) do { \
    if (!(condition)) { \
        fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); \
        exit(EXIT_FAILURE); \
    } \
} while (0)

#endif
