#define _POSIX_C_SOURCE 200809L /* NOLINT(bugprone-reserved-identifier) -- POSIX account lookup. */
#include "session/host_paths.h"
#include "session/profiles.h"

#include <errno.h>
#include <pwd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


static bool path_error(char *error, size_t capacity, const char *text)
{
    if (capacity)
        (void)snprintf(error, capacity, "%s", text);

    return false;
}


bool tb_path_expand(char *output, size_t capacity, const char *path, char *error, size_t error_size)
{
    if (!path)
        path = "";

    const char *home    = "";
    const char *suffix  = path;
    char       *storage = NULL;

    if (path[0] == '~' && (!path[1] || path[1] == '/')) {
        home = getenv("HOME");
        if (home && *home && home[0] != '/')
            return path_error(error, error_size, "HOME must be an absolute path.");

        if (!home || !*home) {
            struct passwd  account;
            struct passwd *result = NULL;
            int            code   = ERANGE;

            /* Bound account lookup rather than trusting an unbounded NSS response. */
            for (size_t size = 1024; size <= (size_t)1024 * 1024; size *= 2) {
                free(storage);
                storage = malloc(size);
                if (!storage)
                    break;

                code = getpwuid_r(getuid(), &account, storage, size, &result);
                if (code != ERANGE)
                    break;
            }

            if (code || !result || !result->pw_dir || result->pw_dir[0] != '/') {
                free(storage);
                return path_error(error, error_size, "Cannot obtain an absolute account home directory.");
            }

            home = result->pw_dir;
        }

        suffix = path + 1;
    }

    size_t prefix = strlen(home);
    size_t length = strlen(suffix);
    if (prefix && home[prefix - 1] == '/' && suffix[0] == '/') {
        ++suffix;
        --length;
    }

    if (prefix >= capacity || length >= capacity - prefix) {
        free(storage);
        return path_error(error, error_size, "Expanded path is too long.");
    }

    memcpy(output, home, prefix);
    output[prefix] = '\0';
    memcpy(output + prefix, suffix, length + 1);
    free(storage);
    if (error_size)
        error[0] = '\0';

    return true;
}


static bool profile_path(char *path, size_t capacity, char *error, size_t error_size)
{
    char effective[TB_PROFILE_PATH];
    if (!memchr(path, '\0', capacity))
        return path_error(error, error_size, "Path is not terminated.");

    if (!tb_path_expand(effective, capacity, path, error, error_size))
        return false;

    for (size_t i = 0; effective[i]; ++i) {
        unsigned char byte = (unsigned char)effective[i];
        if (byte < 32 || byte == 127)
            return path_error(error, error_size, "Expanded profile path contains unsupported control bytes.");
    }

    memcpy(path, effective, strlen(effective) + 1);

    return true;
}


bool tb_profile_paths(const TbProfile_t *raw, TbProfile_t *effective, char *error, size_t error_size)
{
    if (raw->directory_count > TB_PROFILE_SOURCES || raw->rule_count > TB_PROFILE_SOURCES ||
        raw->scripts.count > TB_PROFILE_WORDS || raw->search.count > TB_PROFILE_WORDS)
        return path_error(error, error_size, "Too many path entries.");

    TbProfile_t next = *raw;
    if (!profile_path(next.executable, sizeof(next.executable), error, error_size) ||
        !profile_path(next.gdb, sizeof(next.gdb), error, error_size) ||
        (next.kind == 0 && !next.attach &&
         !profile_path(next.directory, sizeof(next.directory), error, error_size)))
        return false;

    if (next.kind == 1 && next.start_server) {
        if (!profile_path(next.openocd, sizeof(next.openocd), error, error_size))
            return false;

        for (size_t i = 0; i < next.search.count; ++i) {
            if (!profile_path(next.search.values[i], TB_PROFILE_WORD, error, error_size))
                return false;
        }

        for (size_t i = 0; i < next.scripts.count; ++i) {
            if (!profile_path(next.scripts.values[i], TB_PROFILE_WORD, error, error_size))
                return false;
        }

        if (!next.scripts.count &&
            (!profile_path(next.adapter_target ? next.adapter : next.board, TB_PROFILE_PATH,
                           error, error_size) ||
             (next.adapter_target && !profile_path(next.target, TB_PROFILE_PATH, error, error_size))))
            return false;
    }

    for (size_t i = 0; i < next.directory_count; ++i) {
        if (!profile_path(next.directories[i], TB_PROFILE_PATH, error, error_size))
            return false;
    }

    for (size_t i = 0; i < next.rule_count; ++i) {
        if (!profile_path(next.rules[i].to, TB_PROFILE_PATH, error, error_size))
            return false;
    }

    *effective = next;

    return true;
}
