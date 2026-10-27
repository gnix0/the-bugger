#include "monitor.h"
#include <string.h>

static bool identifier(const char *text, bool target)
{
    size_t n = text ? strlen(text) : 0;
    if (!n || n >= 64) { return false; }
    for (size_t i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)text[i];
        if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
              c == '_' || c == '-' || (target && c == '.'))) { return false; }
    }
    return true;
}
bool tb_monitor_start(TbMonitorFrame *f, const char *id, const char *target,
                      uint64_t address, unsigned width)
{
    memset(f, 0, sizeof *f); f->invalid = true;
    if (!identifier(id, false) || !identifier(target, true) ||
        (width != 8 && width != 16 && width != 32 && width != 64) || address % (width / 8)) { return false; }
    memcpy(f->id, id, strlen(id) + 1); memcpy(f->target, target, strlen(target) + 1);
    f->address = address; f->width = width; f->invalid = false; return true;
}
static bool number(const char *s, unsigned base, uint64_t *out)
{
    if (!*s) { return false; } uint64_t n = 0;
    for (size_t i = 0; s[i]; ++i) {
        unsigned char c = (unsigned char)s[i];
        unsigned d = c >= '0' && c <= '9' ? (unsigned)(c - '0') :
                     c >= 'a' && c <= 'f' ? (unsigned)(c - 'a') + 10 :
                     c >= 'A' && c <= 'F' ? (unsigned)(c - 'A') + 10 : 16;
        if (d >= base || n > (UINT64_MAX - d) / base) { return false; } n = n * base + d;
    }
    *out = n; return true;
}
static void line(TbMonitorFrame *f)
{
    f->line[f->used] = 0;
    if (strncmp(f->line, "TBM1|", 5) != 0) { return; }
    size_t id_length = strlen(f->id);
    if (f->used < 5 + id_length || strncmp(f->line + 5, f->id, id_length) != 0 ||
        (f->used > 5 + id_length && f->line[5 + id_length] != '|')) { return; }
    char *parts[8]; size_t count = 1; parts[0] = f->line;
    for (size_t i = 0; i < f->used; ++i) {
        if (f->line[i] == '|') {
            f->line[i] = 0;
            if (count == 8) { f->invalid = true; return; } parts[count++] = &f->line[i + 1];
        }
    }
    if (count < 2 || strcmp(parts[1], f->id) != 0) { return; } /* Unrelated / late request. */
    uint64_t address = 0, width = 0, value = 0;
    if (count != 8 || f->found || strcmp(parts[2], f->target) != 0 ||
        !number(parts[3], 16, &address) || address != f->address || !number(parts[4], 10, &width) || width != f->width ||
        strcmp(parts[5], "1") != 0 || !number(parts[6], 16, &value) ||
        (f->width < 64 && value >= (UINT64_C(1) << f->width)) || strcmp(parts[7], "END") != 0) {
        f->invalid = true; return;
    }
    f->value = value; f->found = true;
}
void tb_monitor_feed(TbMonitorFrame *f, const char *bytes, size_t length)
{
    if (f->invalid) { return; }
    if (length > 16384 - f->received) { f->invalid = true; return; } f->received += length;
    for (size_t i = 0; i < length; ++i) {
        unsigned char c = (unsigned char)bytes[i];
        if (c == '\n' && f->cr) { f->cr = false; continue; }
        f->cr = c == '\r';
        if (c == '\r' || c == '\n') {
            if (!f->discard) { line(f); } f->used = 0; f->discard = false;
            if (f->invalid) { return; } continue;
        }
        if (c < 32 || c > 126) { f->invalid = true; return; }
        if (f->discard) { continue; }
        if (f->used == sizeof f->line - 1) {
            /* Never accept a frame hidden in an overlong line. */
            if (strncmp(f->line, "TBM1|", 5) == 0) { f->invalid = true; return; }
            f->discard = true; f->used = 0; continue;
        }
        f->line[f->used++] = (char)c;
    }
}
bool tb_monitor_finish(const TbMonitorFrame *f, bool done, uint64_t *value)
{
    if (!done || f->invalid || !f->found || f->used || f->discard || !value) { return false; }
    *value = f->value; return true;
}
