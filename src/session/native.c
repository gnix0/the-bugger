#define _GNU_SOURCE /* NOLINT(bugprone-reserved-identifier) -- Linux metadata. */
#include "native.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

/* Linux metadata belongs to this adapter. No target bytes are probed here. */
/* Strict maps fields: conversion overflow or a malformed/truncated record must
 * never turn unknown storage into automatic-read permission. */
static bool map_number(const char **cursor, char delimiter, uint64_t *value)
{
    const char *start = *cursor;
    if (!*start || *start == '-' || *start == '+' || *start == ' ' || *start == '\t') { return false; }
    char *end = NULL; errno = 0;
    unsigned long long parsed = strtoull(start, &end, 16);
    if (errno || end == start || *end != delimiter || parsed > UINT64_MAX) { return false; }
    *value = (uint64_t)parsed; *cursor = end + 1; return true;
}

bool tb_native_memory_region(const char *row, TbMemoryRegion *region)
{
    if (!row || !region || !strchr(row, '\n')) { return false; }
    uint64_t begin, end, unused, inode;
    const char *cursor = row;
    if (!map_number(&cursor, '-', &begin) || !map_number(&cursor, ' ', &end) || end <= begin) { return false; }
    if (strlen(cursor) < 5 || cursor[0] != 'r' ||
        (cursor[1] != 'w' && cursor[1] != '-') || (cursor[2] != 'x' && cursor[2] != '-') ||
        (cursor[3] != 'p' && cursor[3] != 's') || cursor[4] != ' ') { return false; }
    cursor += 5;
    if (!map_number(&cursor, ' ', &unused) || !map_number(&cursor, ':', &unused) ||
        !map_number(&cursor, ' ', &unused)) { return false; }
    /* The inode field can end immediately before a newline or have padding. */
    const char *start = cursor; char *tail = NULL; errno = 0;
    if (*start < '0' || *start > '9') { return false; }
    unsigned long long parsed = strtoull(start, &tail, 10);
    if (errno || tail == start || parsed > UINT64_MAX || (*tail != ' ' && *tail != '\n')) { return false; }
    inode = (uint64_t)parsed; cursor = tail;
    while (*cursor == ' ') { ++cursor; }
    const char *newline = strchr(cursor, '\n');
    if (!newline || newline[1] != 0) { return false; }
    size_t length = (size_t)(newline - cursor);
    if (length >= PATH_MAX) { return false; }
    char name[PATH_MAX]; memcpy(name, cursor, length); name[length] = 0;
    bool ordinary = inode == 0 && (!*name || strcmp(name, "[heap]") == 0 || strcmp(name, "[stack]") == 0);
    struct stat info;
    if (*name == '/' && strncmp(name, "/dev/", 5) != 0 && strncmp(name, "/sys/", 5) != 0 &&
        strncmp(name, "/proc/", 6) != 0 && stat(name, &info) == 0 && S_ISREG(info.st_mode)) { ordinary = true; }
    if (!ordinary) { return false; }
    *region = (TbMemoryRegion){begin, end}; return true;
}
