#ifndef THE_BUGGER_HOST_PATHS_H
#define THE_BUGGER_HOST_PATHS_H

#include <stdbool.h>
#include <stddef.h>

/* Expand only the current user's leading ~ shorthand; other text stays literal. */
bool tb_path_expand(char *output, size_t capacity, const char *path, char *error, size_t error_size);

#endif // THE_BUGGER_HOST_PATHS_H
