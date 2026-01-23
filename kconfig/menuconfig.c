// Simple menuconfig-style tool for Ark, using ncurses.
// Builds and runs on the host, writes a .kconfig file in the project root.

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int arch_index;      // 0: x86, 1: x86_64, 2: arm, 3: riscv
    int fb_enabled;      // 0/1
    int fb_width;
    int fb_height;
    int fb_bpp;
    char init_path[256];
} ark_config_t;

static const char *arch_options[] = {
    "x86",
    "x86_64",
    "arm",
    "riscv",
};
static const int arch_option_count = 4;

typedef enum {
    ITEM_ARCH = 0,
    ITEM_INIT_PATH,
    ITEM_FB_ENABLE,
    ITEM_FB_WIDTH,
    ITEM_FB_HEIGHT,
    ITEM_FB_BPP,
    ITEM_SAVE_AND_EXIT,
    ITEM_QUIT_DISCARD,
    ITEM_COUNT
} menu_item_t;

static void draw_menu(WINDOW *win, int highlight, const ark_config_t *cfg) {
    int row = 1;
    box(win, 0, 0);

    /* Top title, similar to Linux menuconfig. */
    mvwprintw(win, 0, 2, " Ark Kernel Configuration  (menuconfig) ");

    /* Header line with navigation help. */
    mvwprintw(win, row++, 2,
              "Arrow keys navigate, <Enter> selects, <S> Save, <Q> Quit");
    mvwprintw(win, row++, 2,
              "----------------------------------------------------------------------------");

    for (int i = 0; i < ITEM_COUNT; ++i) {
        if (i == highlight) {
            wattron(win, A_REVERSE);
        }

        switch (i) {
        case ITEM_ARCH:
            /* Linux-style choice line: (x86) Processor type and features */
            mvwprintw(win, row, 2, "(%s)  Processor type and architecture",
                      arch_options[cfg->arch_index]);
            break;
        case ITEM_INIT_PATH:
            /* String option: (path) Init binary path */
            mvwprintw(win, row, 2, "(%s)  Init binary path",
                      cfg->init_path[0] ? cfg->init_path : "none");
            break;
        case ITEM_FB_ENABLE:
            /* Boolean: [*] Framebuffer support */
            mvwprintw(win, row, 2, "[%c]  Framebuffer support",
                      cfg->fb_enabled ? 'X' : ' ');
            break;
        case ITEM_FB_WIDTH:
            mvwprintw(win, row, 2, "     ( %4d )  Framebuffer width", cfg->fb_width);
            break;
        case ITEM_FB_HEIGHT:
            mvwprintw(win, row, 2, "     ( %4d )  Framebuffer height", cfg->fb_height);
            break;
        case ITEM_FB_BPP:
            mvwprintw(win, row, 2, "     ( %2d  )  Framebuffer bits-per-pixel", cfg->fb_bpp);
            break;
        case ITEM_SAVE_AND_EXIT:
            mvwprintw(win, row, 2, "< Save >  Write .kconfig and exit");
            break;
        case ITEM_QUIT_DISCARD:
            mvwprintw(win, row, 2, "< Exit >  Quit without saving");
            break;
        }

        if (i == highlight) {
            wattroff(win, A_REVERSE);
        }
        row++;
    }

    wrefresh(win);
}

static void edit_string(WINDOW *win, const char *prompt, char *buf, size_t buflen) {
    echo();
    curs_set(1);
    int h, w;
    getmaxyx(win, h, w);
    mvwprintw(win, h - 2, 2, "%-*s", w - 4, ""); // clear line
    mvwprintw(win, h - 2, 2, "%s: ", prompt);
    wgetnstr(win, buf, (int)buflen - 1);
    noecho();
    curs_set(0);
}

static void edit_int(WINDOW *win, const char *prompt, int *value) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%d", *value);
    edit_string(win, prompt, tmp, sizeof(tmp));
    *value = atoi(tmp);
}

static void save_config(const ark_config_t *cfg, const char *path) {
    FILE *f = fopen(path, "w");
    if (!f) {
        return;
    }
    fprintf(f, "ARCH=%s\n", arch_options[cfg->arch_index]);
    fprintf(f, "INIT_BIN=%s\n", cfg->init_path);
    fprintf(f, "FB_ENABLE=%d\n", cfg->fb_enabled);
    fprintf(f, "FB_WIDTH=%d\n", cfg->fb_width);
    fprintf(f, "FB_HEIGHT=%d\n", cfg->fb_height);
    fprintf(f, "FB_BPP=%d\n", cfg->fb_bpp);
    fclose(f);
}

int main(void) {
    ark_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.arch_index = 0;   // default x86
    cfg.fb_enabled = 1;
    cfg.fb_width = 1024;
    cfg.fb_height = 768;
    cfg.fb_bpp = 32;
    strcpy(cfg.init_path, "/init.bin");

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    int height = ITEM_COUNT + 6;
    int width = 60;
    int starty = (LINES - height) / 2;
    int startx = (COLS - width) / 2;
    WINDOW *win = newwin(height, width, starty, startx);
    keypad(win, TRUE);

    int highlight = 0;
    int ch;
    int done = 0;
    int save = 0;

    while (!done) {
        draw_menu(win, highlight, &cfg);
        ch = wgetch(win);
        switch (ch) {
        case KEY_UP:
            highlight = (highlight - 1 + ITEM_COUNT) % ITEM_COUNT;
            break;
        case KEY_DOWN:
            highlight = (highlight + 1) % ITEM_COUNT;
            break;
        case '\n':
        case KEY_RIGHT:
            switch (highlight) {
            case ITEM_ARCH:
                cfg.arch_index = (cfg.arch_index + 1) % arch_option_count;
                break;
            case ITEM_INIT_PATH:
                edit_string(win, "Path to init.bin", cfg.init_path, sizeof(cfg.init_path));
                break;
            case ITEM_FB_ENABLE:
                cfg.fb_enabled = !cfg.fb_enabled;
                break;
            case ITEM_FB_WIDTH:
                edit_int(win, "Framebuffer width", &cfg.fb_width);
                break;
            case ITEM_FB_HEIGHT:
                edit_int(win, "Framebuffer height", &cfg.fb_height);
                break;
            case ITEM_FB_BPP:
                edit_int(win, "Framebuffer bpp", &cfg.fb_bpp);
                break;
            case ITEM_SAVE_AND_EXIT:
                save = 1;
                done = 1;
                break;
            case ITEM_QUIT_DISCARD:
                save = 0;
                done = 1;
                break;
            }
            break;
        case 's':
        case 'S':
            save = 1;
            done = 1;
            break;
        case 'q':
        case 'Q':
            save = 0;
            done = 1;
            break;
        default:
            break;
        }
    }

    if (save) {
        // Write .kconfig in current working directory.
        save_config(&cfg, ".kconfig");
    }

    delwin(win);
    endwin();
    return 0;
}

