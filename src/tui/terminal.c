#include "terminal.h"
#include <curses.h>
#include <term.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static bool advertised_rgb(const char *name)
{
    const char *color_term = getenv("COLORTERM");
    if (color_term && (strcmp(color_term, "truecolor") == 0 || strcmp(color_term, "24bit") == 0)) { return true; }
    int error = 0;
    if (setupterm(name, STDOUT_FILENO, &error) != OK) { return false; }
    char *foreground = tigetstr("setrgbf"), *background = tigetstr("setrgbb");
    bool rgb = tigetflag("RGB") == 1 || tigetflag("Tc") == 1 ||
               (foreground && foreground != (char *)-1 && background && background != (char *)-1);
    (void)del_curterm(cur_term);
    return rgb;
}

const char *tb_terminal_description(void)
{
    const char *name = getenv("TERM"), *tmux = getenv("TMUX");
    if (!name || getenv("NO_COLOR")) { return NULL; }
    bool inside_tmux = tmux && *tmux;
    bool matching_tmux = inside_tmux &&
        (strcmp(name, "tmux-256color") == 0 || strcmp(name, "tmux") == 0 ||
         strcmp(name, "screen-256color") == 0 || strcmp(name, "screen") == 0);
    bool matching_alacritty = !inside_tmux && strcmp(name, "alacritty") == 0;
    if ((!matching_tmux && !matching_alacritty) || !advertised_rgb(name)) { return NULL; }
    return matching_tmux ? "tmux-direct" : "alacritty-direct";
}
