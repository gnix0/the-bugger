#ifndef THE_BUGGER_PROFILES_H
#define THE_BUGGER_PROFILES_H
#include <stdbool.h>
#include <stddef.h>

enum { TB_PROFILES = 16, TB_PROFILE_NAME = 64, TB_PROFILE_PATH = 1024,
       TB_PROFILE_WORDS = 32, TB_PROFILE_WORD = 512, TB_PROFILE_SOURCES = 8,
       TB_PROFILE_ACTIONS = 7, TB_PROFILE_RECENT = 8 };
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
    /* Positive = F1..F24; negative = Ctrl+A..Z; zero = unbound. */
    int keys[TB_PROFILE_ACTIONS];
} TbProfile;
typedef struct TbProfiles TbProfiles;
void tb_profile_defaults(TbProfile *profile, int kind);
bool tb_profile_valid(const TbProfile *profile);
const char *tb_profile_action(size_t action);
bool tb_profile_key_valid(int key);
void tb_profile_key_name(int key, char *text, size_t size);
bool tb_profile_key_parse(const char *text, int *key);
TbProfiles *tb_profiles_create(void);
void tb_profiles_destroy(TbProfiles *profiles);
bool tb_profiles_load(TbProfiles *profiles);
size_t tb_profiles_count(const TbProfiles *profiles);
const TbProfile *tb_profiles_at(const TbProfiles *profiles, size_t index);
const TbProfile *tb_profiles_find(const TbProfiles *profiles, const char *name);
const char *tb_profiles_recent(const TbProfiles *profiles, size_t index);
const char *tb_profiles_error(const TbProfiles *profiles);
const char *tb_profiles_path(const TbProfiles *profiles);
/* Atomic whole-file transactions; failures retain the previous in-memory data.
 * Put requires explicit replacement consent for an existing name. */
bool tb_profiles_put(TbProfiles *profiles, const TbProfile *profile, bool replace);
bool tb_profiles_remove(TbProfiles *profiles, const char *name);
bool tb_profiles_touch(TbProfiles *profiles, const char *name);
#endif
