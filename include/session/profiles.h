#ifndef THE_BUGGER_PROFILES_H
#define THE_BUGGER_PROFILES_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "session/setup.h"

enum {
    TB_PROFILES        = 16,
    TB_PROFILE_NAME    = 64,
    TB_PROFILE_PATH    = 1024,
    TB_PROFILE_WORDS   = 32,
    TB_PROFILE_WORD    = 512,
    TB_PROFILE_SOURCES = 8,
    TB_PROFILE_RECENT  = 8
};

typedef struct {
    size_t count;
    char   values[TB_PROFILE_WORDS][TB_PROFILE_WORD];
} TbProfileWords_t;

typedef struct {
    char from[TB_PROFILE_PATH];
    char to[TB_PROFILE_PATH];
} TbProfileRule_t;

typedef struct {
    char             name[TB_PROFILE_NAME];
    int              kind;      /* 0 native, 1 OpenOCD definition, 2 existing-server definition. */
    bool             attach;
    bool             start_server;
    bool             adapter_target;
    char             executable[TB_PROFILE_PATH];
    char             directory[TB_PROFILE_PATH];
    char             gdb[TB_PROFILE_PATH];
    char             openocd[TB_PROFILE_PATH];
    char             endpoint[256];
    char             board[TB_PROFILE_PATH];
    char             adapter[TB_PROFILE_PATH];
    char             target[TB_PROFILE_PATH];
    char             protocol[24];
    TbProfileWords_t arguments;
    TbProfileWords_t scripts;
    TbProfileWords_t search;
    char             directories[TB_PROFILE_SOURCES][TB_PROFILE_PATH];
    size_t           directory_count;
    TbProfileRule_t  rules[TB_PROFILE_SOURCES];
    size_t           rule_count;
} TbProfile_t;

typedef struct tb_profiles TbProfiles_t;

void tb_profile_defaults(TbProfile_t *profile, int kind);
bool tb_profile_valid(const TbProfile_t *profile);
/* Effective active host paths; publish only on success and leave raw fields intact. */
bool tb_profile_paths(const TbProfile_t *raw, TbProfile_t *effective, char *error, size_t error_size);

#define TB_PROFILE_MODE      UINT32_C(1)
#define TB_PROFILE_TOOLS     UINT32_C(2)
#define TB_PROFILE_ARGUMENTS UINT32_C(4)
#define TB_PROFILE_SCRIPTS   UINT32_C(8)
#define TB_PROFILE_SEARCH    UINT32_C(16)
#define TB_PROFILE_ENDPOINT  UINT32_C(32)
#define TB_PROFILE_ELF       UINT32_C(64)
#define TB_PROFILE_DIRECTORY UINT32_C(128)
#define TB_PROFILE_SOURCE    UINT32_C(256)

/* Parsed active fields only; names, inactive controls and unused array slots
 * are not connection differences. Paths and endpoint aliases stay literal. */
uint32_t tb_profile_differences(const TbProfile_t *session, const TbProfile_t *saved);

/* Local metadata for renewed restoration consent, never firmware identity. */
void tb_profile_image_stamp(const TbProfile_t *profile, const char *launch_directory, char stamp[256]);
bool tb_profile_relative_paths(const TbProfile_t *profile);
TbProfiles_t *tb_profiles_create(void);
void tb_profiles_destroy(TbProfiles_t *profiles);
bool tb_profiles_load(TbProfiles_t *profiles);
size_t tb_profiles_count(const TbProfiles_t *profiles);
const TbProfile_t *tb_profiles_at(const TbProfiles_t *profiles, size_t index);
const TbProfile_t *tb_profiles_find(const TbProfiles_t *profiles, const char *name);
const char *tb_profiles_recent(const TbProfiles_t *profiles, size_t index);
const char *tb_profiles_error(const TbProfiles_t *profiles);
const char *tb_profiles_path(const TbProfiles_t *profiles);
bool tb_profiles_read_only(const TbProfiles_t *profiles);
bool tb_profiles_has_setup(const TbProfiles_t *profiles, const char *name);

/* Returned definitions are independently owned; free after use. */
TbSetup_t *tb_profiles_setup(const TbProfiles_t *profiles, const char *name);
bool tb_profiles_setup_connection(const TbProfiles_t *profiles, const char *name, TbProfile_t *connection,
                                  char directory[TB_SETUP_PATH]);
bool tb_profiles_setup_stamp(const TbProfiles_t *profiles, const char *name, char stamp[256]);
bool tb_profiles_save_setup(TbProfiles_t *profiles, const char *name, const TbSetup_t *setup,
                            const TbProfile_t *connection, const char *launch_directory);

/* Copies only saved definitions, never a session draft or recency. */
bool tb_profiles_duplicate(TbProfiles_t *profiles, const char *name, const char *new_name);

/* Atomic whole-file transactions; failures retain the previous in-memory data.
 * Put requires explicit replacement consent for an existing name. */
bool tb_profiles_put(TbProfiles_t *profiles, const TbProfile_t *profile, bool replace);
bool tb_profiles_remove(TbProfiles_t *profiles, const char *name);
bool tb_profiles_touch(TbProfiles_t *profiles, const char *name);
#endif // THE_BUGGER_PROFILES_H
