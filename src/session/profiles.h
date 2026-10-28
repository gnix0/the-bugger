#ifndef THE_BUGGER_PROFILES_H
#define THE_BUGGER_PROFILES_H
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "setup.h"

enum { TB_PROFILES = 16, TB_PROFILE_NAME = 64, TB_PROFILE_PATH = 1024,
       TB_PROFILE_WORDS = 32, TB_PROFILE_WORD = 512, TB_PROFILE_SOURCES = 8,
       TB_PROFILE_RECENT = 8 };
typedef struct { size_t count; char values[TB_PROFILE_WORDS][TB_PROFILE_WORD]; } TbProfileWords;
typedef struct { char from[TB_PROFILE_PATH], to[TB_PROFILE_PATH]; } TbProfileRule;
typedef struct {
    char name[TB_PROFILE_NAME];
    int kind; /* 0 native, 1 OpenOCD definition, 2 existing-server definition. */
    bool attach, start_server, adapter_target;
    char executable[TB_PROFILE_PATH], directory[TB_PROFILE_PATH], gdb[TB_PROFILE_PATH];
    char openocd[TB_PROFILE_PATH], endpoint[256], board[TB_PROFILE_PATH];
    char adapter[TB_PROFILE_PATH], target[TB_PROFILE_PATH], protocol[24];
    TbProfileWords arguments, scripts, search;
    char directories[TB_PROFILE_SOURCES][TB_PROFILE_PATH]; size_t directory_count;
    TbProfileRule rules[TB_PROFILE_SOURCES]; size_t rule_count;
} TbProfile;
typedef struct TbProfiles TbProfiles;
void tb_profile_defaults(TbProfile *profile, int kind);
bool tb_profile_valid(const TbProfile *profile);
enum { TB_PROFILE_MODE = 1, TB_PROFILE_TOOLS = 2, TB_PROFILE_ARGUMENTS = 4,
       TB_PROFILE_SCRIPTS = 8, TB_PROFILE_SEARCH = 16, TB_PROFILE_ENDPOINT = 32,
       TB_PROFILE_ELF = 64, TB_PROFILE_DIRECTORY = 128, TB_PROFILE_SOURCE = 256 };
/* Parsed active fields only; names, inactive controls and unused array slots
 * are not connection differences. Paths and endpoint aliases stay literal. */
uint32_t tb_profile_differences(const TbProfile *session, const TbProfile *saved);
/* Local metadata for renewed restoration consent, never firmware identity. */
void tb_profile_image_stamp(const TbProfile *profile, const char *launch_directory, char stamp[256]);
bool tb_profile_relative_paths(const TbProfile *profile);
TbProfiles *tb_profiles_create(void);
void tb_profiles_destroy(TbProfiles *profiles);
bool tb_profiles_load(TbProfiles *profiles);
size_t tb_profiles_count(const TbProfiles *profiles);
const TbProfile *tb_profiles_at(const TbProfiles *profiles, size_t index);
const TbProfile *tb_profiles_find(const TbProfiles *profiles, const char *name);
const char *tb_profiles_recent(const TbProfiles *profiles, size_t index);
const char *tb_profiles_error(const TbProfiles *profiles);
const char *tb_profiles_path(const TbProfiles *profiles);
bool tb_profiles_read_only(const TbProfiles *profiles);
bool tb_profiles_has_setup(const TbProfiles *profiles, const char *name);
/* Returned definitions are independently owned; free after use. */
TbSetup *tb_profiles_setup(const TbProfiles *profiles, const char *name);
bool tb_profiles_setup_connection(const TbProfiles *profiles, const char *name,
                                 TbProfile *connection, char directory[TB_SETUP_PATH]);
bool tb_profiles_setup_stamp(const TbProfiles *profiles, const char *name, char stamp[256]);
bool tb_profiles_save_setup(TbProfiles *profiles, const char *name, const TbSetup *setup,
                            const TbProfile *connection, const char *launch_directory);
/* Copies only saved definitions, never a session draft or recency. */
bool tb_profiles_duplicate(TbProfiles *profiles, const char *name, const char *new_name);
/* Atomic whole-file transactions; failures retain the previous in-memory data.
 * Put requires explicit replacement consent for an existing name. */
bool tb_profiles_put(TbProfiles *profiles, const TbProfile *profile, bool replace);
bool tb_profiles_remove(TbProfiles *profiles, const char *name);
bool tb_profiles_touch(TbProfiles *profiles, const char *name);
#endif
