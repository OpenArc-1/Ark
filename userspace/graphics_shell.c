/**
 * Ark Graphical Shell (userspace/graphics_shell.c)
 *
 * This shell demonstrates using the graphics rendering system from userspace.
 * Requires kernel API to be extended with graphics functions.
 *
 * To enable this:
 * 1. Extend ark_kernel_api_t in gen/init_api.h to include graphics functions
 * 2. Implement graphics function wrappers in gen/init_api.c
 * 3. Replace "shell.c" with "graphics_shell.c" in Makefile
 *
 * Graphics API Extensions Needed:
 *   gfx_context_t *(*gfx_get_context)(void);
 *   void (*gfx_clear)(u32 color);
 *   void (*gfx_fill_rect)(gfx_context_t *ctx, u32 x, u32 y, u32 w, u32 h, u32 color);
 *   void (*gfx_draw_rect)(gfx_context_t *ctx, u32 x, u32 y, u32 w, u32 h, u32 color);
 *   void (*gfx_present)(void);
 *   void (*gfx_draw_pixel)(gfx_context_t *ctx, u32 x, u32 y, u32 color);
 *   void (*text_puts)(text_context_t *ctx, const char *str);
 *   void (*image_draw_bmp)(gfx_context_t *ctx, u32 x, u32 y, const char *path);
 */

#include "ark/init_api.h"

#define LINE_MAX     256
#define NAME_MAX     64
#define ARG_MAX      256

static const ark_kernel_api_t *g_api;

/* Color definitions (ARGB 32-bit) */
#define COLOR_BLACK      0xFF000000
#define COLOR_WHITE      0xFFFFFFFF
#define COLOR_RED        0xFFFF0000
#define COLOR_GREEN      0xFF00FF00
#define COLOR_BLUE       0xFF0000FF
#define COLOR_GRAY       0xFF808080
#define COLOR_DARK_BLUE  0xFF001166
#define COLOR_LIGHT_BLUE 0xFFAACCFF

/* ============ String Utilities ============ */

static void trim(char *s) {
    char *base = s;
    while (*s == ' ' || *s == '\t') s++;
    char *out = base;
    while (*s) *out++ = *s++;
    *out = '\0';
    
    char *end = out;
    while (end > base && (end[-1] == ' ' || end[-1] == '\t')) {
        end--;
        *end = '\0';
    }
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) {
        a++;
        b++;
    }
    return *a == *b;
}

static void str_copy(char *dst, const char *src, unsigned max) {
    unsigned i = 0;
    while (src[i] && i + 1 < max) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = '\0';
}

/* ============ Graphics Utilities ============ */

/**
 * Draw a filled box with border (card/window frame)
 * Requires: gfx_fill_rect, gfx_draw_rect
 */
static void draw_panel(void *gfx_ctx, u32 x, u32 y, u32 w, u32 h, 
                       u32 bg_color, u32 border_color) {
    /* Fill background */
    /* g_api->gfx_fill_rect(gfx_ctx, x, y, w, h, bg_color); */
    
    /* Draw border */
    /* g_api->gfx_draw_rect(gfx_ctx, x, y, w, h, border_color); */
    
    /* Note: Actual implementation requires extended kernel API */
}

/**
 * Draw a button (filled rectangle with text)
 */
static void draw_button(void *gfx_ctx, u32 x, u32 y, u32 w, u32 h,
                       const char *label) {
    /* Button background */
    /* g_api->gfx_fill_rect(gfx_ctx, x, y, w, h, COLOR_BLUE); */
    
    /* Button border */
    /* g_api->gfx_draw_rect(gfx_ctx, x, y, w, h, COLOR_WHITE); */
    
    /* Button text (would require text_renderer integration) */
    /* g_api->text_puts(context, label); */
}

/* ============ UI Rendering Functions ============ */

static void render_welcome_screen(void) {
    /* Draw background */
    /* 
    gfx_context_t *ctx = g_api->gfx_get_context();
    g_api->gfx_clear(COLOR_DARK_BLUE);
    
    // Draw title bar
    g_api->gfx_fill_rect(ctx, 0, 0, 800, 60, COLOR_BLUE);
    g_api->gfx_draw_rect(ctx, 0, 0, 800, 60, COLOR_WHITE);
    
    // Draw title text
    // g_api->text_puts(title_ctx, "Ark Shell v1.0");
    
    // Draw content area
    draw_panel(ctx, 20, 80, 760, 300, COLOR_DARK_BLUE, COLOR_WHITE);
    
    // Draw command buttons
    draw_button(ctx, 30, 400, 100, 40, "Help");
    draw_button(ctx, 140, 400, 100, 40, "Files");
    draw_button(ctx, 250, 400, 100, 40, "Time");
    
    g_api->gfx_present();
    */
}

static void render_command_prompt(void) {
    /*
    gfx_context_t *ctx = g_api->gfx_get_context();
    
    // Draw command input area at bottom
    g_api->gfx_fill_rect(ctx, 0, 700, 800, 100, COLOR_GRAY);
    g_api->gfx_draw_rect(ctx, 0, 700, 800, 100, COLOR_WHITE);
    
    // Draw prompt text
    // g_api->text_puts(prompt_ctx, "ark@localhost:~$ ");
    
    g_api->gfx_present();
    */
}

/* ============ Command Implementations ============ */

static void cmd_help(void) {
    g_api->printk("\n╔════════════════════════════════════════════╗\n");
    g_api->printk("║           SHELL COMMANDS                   ║\n");
    g_api->printk("╠════════════════════════════════════════════╣\n");
    g_api->printk("║  ls         - List files in /              ║\n");
    g_api->printk("║  cat <path> - Display file contents        ║\n");
    g_api->printk("║  time       - Show current date/time       ║\n");
    g_api->printk("║  cpu        - Display CPU information      ║\n");
    g_api->printk("║  clear      - Clear the screen             ║\n");
    g_api->printk("║  help       - Show this help message       ║\n");
    g_api->printk("║  exit       - Exit the shell               ║\n");
    g_api->printk("╚════════════════════════════════════════════╝\n\n");
}

static void cmd_ls(char *args) {
    const char *path = (args && args[0]) ? args : "/";
    
    if (!str_eq(path, "/")) {
        g_api->printk("  ✗ Error: only \"/\" supported\n\n");
        return;
    }
    
    u32 count = g_api->vfs_list_count("/");
    g_api->printk("  Files in %s:\n", path);
    
    for (u32 i = 0; i < count && i < 32; i++) {
        char name[NAME_MAX];
        if (g_api->vfs_list_at("/", i, name, sizeof(name))) {
            g_api->printk("    • %s\n", name);
        }
    }
    if (count > 32) {
        g_api->printk("    ... (%u more)\n", count - 32);
    }
    g_api->printk("\n");
}

static void cmd_cat(char *args) {
    if (!args || !args[0]) {
        g_api->printk("  Usage: cat <path>\n\n");
        return;
    }
    
    int fd = g_api->vfs_open(args);
    if (fd < 0) {
        g_api->printk("  Error: cannot open '%s'\n\n", args);
        return;
    }
    
    u32 size = g_api->vfs_file_size(fd);
    g_api->printk("  Displaying '%s' (%u bytes):\n", args, (unsigned)size);
    g_api->printk("  ─────────────────────────────────────────\n");
    
    char buf[256];
    u32 offset = 0;
    u32 max_read = (size > 2048) ? 2048 : size;
    
    while (offset < max_read) {
        u32 chunk = sizeof(buf);
        if (offset + chunk > max_read) {
            chunk = max_read - offset;
        }
        
        int bytes_read = g_api->vfs_read(fd, buf, chunk);
        if (bytes_read <= 0) break;
        
        for (int i = 0; i < bytes_read; i++) {
            char c = buf[i];
            if (c >= 32 && c < 127) {
                g_api->printk("%c", c);
            } else if (c == '\n') {
                g_api->printk("\n");
            } else if (c == '\t') {
                g_api->printk("    ");
            }
        }
        offset += (u32)bytes_read;
    }
    
    if (size > max_read) {
        g_api->printk("\n  ... (%u more bytes)\n", size - max_read);
    }
    g_api->printk("\n");
    g_api->vfs_close(fd);
}

static void cmd_time(void) {
    rtc_time_t t = g_api->read_rtc();
    
    const char *months[] = { "", "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                             "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
    const char *month = (t.month > 0 && t.month < 13) ? months[t.month] : "???";
    
    g_api->printk("  System Time:\n");
    g_api->printk("    %02u:%02u:%02u  |  %u %s %u\n",
                  (unsigned)t.hour, (unsigned)t.min, (unsigned)t.sec,
                  (unsigned)t.day, month, (unsigned)t.year);
    g_api->printk("\n");
}

static void cmd_cpu(void) {
    char vendor[13];
    g_api->get_cpu_vendor(vendor);
    
    u32 eax, edx;
    g_api->cpuid(1, 0, &eax, 0, 0, &edx);
    
    u32 family = (eax >> 8) & 0x0F;
    u32 model = (eax >> 4) & 0x0F;
    u32 stepping = eax & 0x0F;
    
    g_api->printk("  CPU Information:\n");
    g_api->printk("    Vendor:   %s\n", vendor);
    g_api->printk("    Family:   %u\n", (unsigned)family);
    g_api->printk("    Model:    %u\n", (unsigned)model);
    g_api->printk("    Stepping: %u\n", (unsigned)stepping);
    g_api->printk("\n");
}

static void cmd_clear(void) {
    for (int i = 0; i < 40; i++) {
        g_api->printk("\n");
    }
}

/* ============ Command Parser & Executor ============ */

static void parse_command(char *line, char *cmd, unsigned cmd_max, char *args, unsigned args_max) {
    trim(line);
    
    unsigned i = 0;
    while (line[i] && line[i] != ' ' && line[i] != '\t' && i + 1 < cmd_max) {
        cmd[i] = line[i];
        i++;
    }
    cmd[i] = '\0';
    
    while (line[i] == ' ' || line[i] == '\t') {
        i++;
    }
    
    str_copy(args, line + i, args_max);
    trim(args);
}

static int execute_command(char *cmd, char *args) {
    if (str_eq(cmd, "help")) {
        cmd_help();
        return 0;
    }
    
    if (str_eq(cmd, "ls")) {
        cmd_ls(args);
        return 0;
    }
    
    if (str_eq(cmd, "cat")) {
        cmd_cat(args);
        return 0;
    }
    
    if (str_eq(cmd, "time")) {
        cmd_time();
        return 0;
    }
    
    if (str_eq(cmd, "cpu")) {
        cmd_cpu();
        return 0;
    }
    
    if (str_eq(cmd, "clear")) {
        cmd_clear();
        return 0;
    }
    
    if (str_eq(cmd, "exit")) {
        return 1;
    }
    
    if (cmd[0] == '\0') {
        return 0;
    }
    
    g_api->printk("  Unknown command: '%s' (try 'help')\n\n", cmd);
    return 0;
}

/* ============ Main Entry Point ============ */

int _start(const ark_kernel_api_t *api) {
    if (!api || api->version < 2) {
        if (api && api->printk) {
            api->printk("[shell] Error: need API v2\n");
        }
        return 1;
    }
    
    g_api = api;
    
    /* Allocate TTY session */
    u32 sid = api->tty_alloc();
    if (sid == (u32)-1) {
        api->printk("[shell] Error: tty_alloc failed\n");
        return 1;
    }
    
    api->tty_switch(sid);
    char ttyname[16];
    api->tty_get_name(sid, ttyname, sizeof(ttyname));
    
    /* ========== Render Welcome Screen ========== */
    
    /* If graphics extensions are available, use them */
    /* render_welcome_screen(); */
    
    /* Fallback: text mode welcome */
    api->printk("\n");
    api->printk("╔═══════════════════════════════════════════╗\n");
    api->printk("║      Ark Operating System Shell v1.0      ║\n");
    api->printk("║      Running in userspace (ring 3)        ║\n");
    api->printk("╚═══════════════════════════════════════════╝\n");
    api->printk("\n");
    api->printk("Session: %s (ID: %u)\n", ttyname, (unsigned)sid);
    api->printk("Type 'help' for commands, 'exit' to quit\n\n");
    
    /* ========== Main Command Loop ========== */
    
    for (;;) {
        api->printk("ark:~$ ");
        
        char line[LINE_MAX];
        api->input_read(line, sizeof(line), 0);
        
        trim(line);
        if (!line[0]) continue;
        
        char cmd[ARG_MAX];
        char args[ARG_MAX];
        parse_command(line, cmd, sizeof(cmd), args, sizeof(args));
        
        int should_exit = execute_command(cmd, args);
        if (should_exit) break;
    }
    
    /* ========== Cleanup & Exit ========== */
    
    api->printk("\nShutdown: cleaning up resources...\n");
    api->printk("Goodbye!\n\n");
    api->tty_debug(sid, "shell exit");
    api->tty_free(sid);
    
    return 0;
}
