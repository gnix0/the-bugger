#include "tui/terminal.h"
#include <curses.h>
#include <term.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <stdint.h>

enum {
    TERM_ENTRY_LIMIT = 32768
};

typedef struct {
    unsigned char bytes[TERM_ENTRY_LIMIT];
    size_t        size;
    bool          valid;
} TermEntry_t;


static void entry_bytes(TermEntry_t *entry, const void *data, size_t length)
{
    if (length > sizeof(entry->bytes) - entry->size) {
        entry->valid = false;
        return;
    }

    memcpy(entry->bytes + entry->size, data, length);
    entry->size += length;
}


static void entry_u16(TermEntry_t *entry, uint32_t value)
{
    unsigned char bytes[] = {
        (unsigned char)value,
        (unsigned char)(value >> 8U)
    };

    entry_bytes(entry, bytes, sizeof(bytes));
}


static void entry_u32(TermEntry_t *entry, uint32_t value)
{
    unsigned char bytes[] = {
        (unsigned char)value,
        (unsigned char)(value >> 8U),
        (unsigned char)(value >> 16U),
        (unsigned char)(value >> 24U)
    };

    entry_bytes(entry, bytes, sizeof(bytes));
}


static void entry_align(TermEntry_t *entry)
{
    if (entry->size % 2)
        entry_bytes(entry, "\0", 1);
}


static void entry_string(TermEntry_t *entry, TermEntry_t *strings, const char *value, size_t base)
{
    if (!value || value == (char *)-1) {
        entry_u16(entry, value ? 0xfffeU : 0xffffU);
        return;
    }

    entry_u16(entry, (uint32_t)(strings->size - base));
    entry_bytes(strings, value, strlen(value) + 1);
}


/* Serialize the installed description using ncurses' documented extended-number
 * format. Reading the public TERMTYPE retains all keys and extended capabilities;
 * numeric queries preserve full values that its legacy short array cannot hold. */
static char *direct_entry(void)
{
    static const char rgb_foreground[] =
        "\033[38;2;%p1%{65536}%/%d;%p1%{256}%/%{255}%&%d;%p1%{255}%&%dm";
    static const char rgb_background[] =
        "\033[48;2;%p1%{65536}%/%d;%p1%{256}%/%{255}%&%d;%p1%{255}%&%dm";

    if (tigetnum("colors") < 16777216)
        return NULL;

    char *foreground = tigetstr("setaf");
    char *background = tigetstr("setab");
    if (!foreground || foreground == (char *)-1 || !background || background == (char *)-1)
        return NULL;

    char *sequence = tparm(foreground, 0L);
    if (!sequence || strcmp(sequence, "\033[30m") != 0)
        return NULL;
    sequence = tparm(background, 0L);
    if (!sequence || strcmp(sequence, "\033[40m") != 0)
        return NULL;

    TermEntry_t entry = {
        .valid = true
    };
    TermEntry_t strings = {
        .valid = true
    };
    const TERMTYPE *type  = &cur_term->type;
    size_t         names = strlen(type->term_names) + 1;
    if (names > 128)
        return NULL;

    entry_u16(&entry, 01036);
    entry_u16(&entry, (uint32_t)names);
    entry_u16(&entry, BOOLCOUNT);
    entry_u16(&entry, NUMCOUNT);
    entry_u16(&entry, STRCOUNT);
    entry_u16(&entry, 0);
    entry_bytes(&entry, type->term_names, names);
    entry_bytes(&entry, type->Booleans, BOOLCOUNT);
    entry_align(&entry);
    for (size_t i = 0; i < NUMCOUNT; ++i)
        entry_u32(&entry, (uint32_t)tigetnum(numnames[i]));
    for (size_t i = 0; i < STRCOUNT; ++i) {
        const char *value = strcmp(strnames[i], "setaf") == 0 ? rgb_foreground
            : strcmp(strnames[i], "setab") == 0 ? rgb_background
            : type->Strings[i];
        entry_string(&entry, &strings, value, 0);
    }
    entry.bytes[10] = (unsigned char)strings.size;
    entry.bytes[11] = (unsigned char)(strings.size >> 8U);
    entry_bytes(&entry, strings.bytes, strings.size);

#if NCURSES_XNAMES
    size_t booleans = type->ext_Booleans;
    size_t numbers  = type->ext_Numbers;
    size_t values   = type->ext_Strings;
    size_t count    = booleans + numbers + values;
    if (count) {
        entry_align(&entry);
        entry_u16(&entry, (uint32_t)booleans);
        entry_u16(&entry, (uint32_t)numbers);
        entry_u16(&entry, (uint32_t)values);
        entry_u16(&entry, (uint32_t)(count + values));
        size_t size_offset = entry.size;
        entry_u16(&entry, 0);
        entry_bytes(&entry, type->Booleans + BOOLCOUNT, booleans);
        entry_align(&entry);
        for (size_t i = 0; i < numbers; ++i)
            entry_u32(&entry, (uint32_t)tigetnum(type->ext_Names[booleans + i]));
        strings.size = 0;
        for (size_t i = 0; i < values; ++i)
            entry_string(&entry, &strings, type->Strings[STRCOUNT + i], 0);
        size_t name_base = strings.size;
        for (size_t i = 0; i < count; ++i)
            entry_string(&entry, &strings, type->ext_Names[i], name_base);
        if (!entry.valid || size_offset + 1 >= sizeof(entry.bytes))
            return NULL;
        entry.bytes[size_offset]     = (unsigned char)strings.size;
        entry.bytes[size_offset + 1] = (unsigned char)(strings.size >> 8U);
        entry_bytes(&entry, strings.bytes, strings.size);
    }
#endif // NCURSES_XNAMES

    if (!entry.valid || !strings.valid)
        return NULL;

    char *encoded = malloc(4 + entry.size * 2 + 1);
    if (!encoded)
        return NULL;

    static const char hex[] = "0123456789abcdef";
    memcpy(encoded, "hex:", 4);
    for (size_t i = 0; i < entry.size; ++i) {
        encoded[4 + i * 2]     = hex[entry.bytes[i] >> 4U];
        encoded[4 + i * 2 + 1] = hex[entry.bytes[i] & 15U];
    }
    encoded[4 + entry.size * 2] = '\0';

    return encoded;
}


SCREEN *tb_terminal_screen(const char *description, FILE *output, FILE *input)
{
    char *entry = NULL;
    int   error = 0;
    if (!getenv("NO_COLOR") && setupterm(description, fileno(output), &error) == OK) {
        entry = direct_entry();
        (void)del_curterm(cur_term);
    }
    if (!entry)
        return newterm(description, output, input);

    const char *previous = getenv("TERMINFO");
    char       *saved    = previous ? strdup(previous) : NULL;
    if (previous && !saved) {
        free(entry);
        return NULL;
    }
    SCREEN *screen = NULL;
    if (setenv("TERMINFO", entry, 1) == 0) {
        screen = newterm(description, output, input);
        int restored = saved ? setenv("TERMINFO", saved, 1) : unsetenv("TERMINFO");
        if (restored < 0 && screen) {
            (void)endwin();
            delscreen(screen);
            screen = NULL;
        }
    }
    free(saved);
    free(entry);

    return screen;
}


static bool advertised_rgb(const char *name)
{
    const char *color_term = getenv("COLORTERM");
    if (color_term && (strcmp(color_term, "truecolor") == 0 || strcmp(color_term, "24bit") == 0))
        return true;

    int error = 0;
    if (setupterm(name, STDOUT_FILENO, &error) != OK)
        return false;

    char *foreground = tigetstr("setrgbf");
    char *background = tigetstr("setrgbb");
    bool  rgb        = tigetflag("RGB") == 1 || tigetflag("Tc") == 1 ||
                       (foreground && foreground != (char *)-1 && background && background != (char *)-1);

    (void)del_curterm(cur_term);

    return rgb;
}


const char *tb_terminal_description(void)
{
    const char *name = getenv("TERM");
    const char *tmux = getenv("TMUX");
    if (!name || getenv("NO_COLOR"))
        return NULL;

    bool inside_tmux = tmux && *tmux;
    bool matching_tmux = inside_tmux &&
        (strcmp(name, "tmux-256color") == 0 || strcmp(name, "tmux") == 0 ||
         strcmp(name, "screen-256color") == 0 || strcmp(name, "screen") == 0);
    bool matching_alacritty = !inside_tmux && strcmp(name, "alacritty") == 0;

    if ((!matching_tmux && !matching_alacritty) || !advertised_rgb(name))
        return NULL;

    return matching_tmux ? "tmux-direct" : "alacritty-direct";
}
