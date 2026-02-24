/**
 * Ark userspace shell (init.bin)
 */

#include "ark/init_api.h"
#include "ark/uid.h"

#define LINE_MAX  256
#define NAME_MAX  64
#define ARG_MAX   128
#define PATH_MAX  256

static const ark_kernel_api_t *g_api;
static char g_cwd[PATH_MAX] = "/usr/root";  /* current working directory */

/* ------------------------------------------------------------------ */
/*  String utils                                                       */
/* ------------------------------------------------------------------ */

static void trim(char *s) {
    char *base = s;
    while (*s == ' ' || *s == '\t') s++;
    char *out = base;
    while (*s) *out++ = *s++;
    *out = '\0';
    char *end = out;
    while (end > base && (end[-1] == ' ' || end[-1] == '\t')) { end--; *end = '\0'; }
}

static int str_eq(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a == *b;
}

static void str_copy(char *dst, const char *src, unsigned max) {
    unsigned i = 0;
    while (src[i] && i + 1 < max) { dst[i] = src[i]; i++; }
    dst[i] = '\0';
}

static unsigned str_len(const char *s) {
    unsigned i = 0;
    while (s[i]) i++;
    return i;
}

/* build an absolute path from cwd + arg */
static void resolve_path(const char *arg, char *out, unsigned max) {
    if (arg[0] == '/') {
        str_copy(out, arg, max);
        return;
    }
    /* relative: append to cwd */
    str_copy(out, g_cwd, max);
    unsigned len = str_len(out);
    if (len > 0 && out[len - 1] != '/' && len + 1 < max) {
        out[len++] = '/';
        out[len]   = '\0';
    }
    unsigned i = 0;
    while (arg[i] && len + 1 < max) {
        out[len++] = arg[i++];
    }
    out[len] = '\0';
}

/* ------------------------------------------------------------------ */
/*  Parser                                                             */
/* ------------------------------------------------------------------ */

static void parse(char *line, char *cmd, unsigned cmd_max, char *args, unsigned args_max) {
    trim(line);
    unsigned i = 0;
    while (line[i] && line[i] != ' ' && line[i] != '\t' && i + 1 < cmd_max) {
        cmd[i] = line[i];
        i++;
    }
    cmd[i] = '\0';
    while (line[i] == ' ' || line[i] == '\t') i++;
    str_copy(args, line + i, args_max);
    trim(args);
}
//User folder
static void usr_init(u32 sid) {
    // Create /usr directory
    if (g_api->vfs_mkdir("/usr") < 0) {
        g_api->tty_debug(sid, "usr_init: /usr already exists or failed");
    } else {
        g_api->tty_debug(sid, "usr_init: /usr created");
    }

    // Create /usr/root (Assuming ARK_DEV_CHR or similar if it's a node, 
    // or just use mkdir if it's a home folder)
    if (g_api->vfs_mkdir("/usr/root") < 0) {
        g_api->tty_debug(sid, "usr_init: /usr/root mkdir failed");
    } else {
        g_api->tty_debug(sid, "usr_init: /usr/root created");
    }
}
/* ------------------------------------------------------------------ */
/*  /dev population                                                    */
/* ------------------------------------------------------------------ */

static void dev_init(u32 sid) {
    if (g_api->vfs_mkdir("/dev") < 0)
        g_api->tty_debug(sid, "dev_init: /dev mkdir failed or already exists");

    if (g_api->vfs_mknod("/dev/sda", ARK_DEV_BLK, ARK_MAJ_SDA, 0) < 0)
        g_api->tty_debug(sid, "dev_init: mknod sda failed");
    else
        g_api->tty_debug(sid, "dev_init: /dev/sda ok (blk %d:0)", ARK_MAJ_SDA);

    if (g_api->vfs_mknod("/dev/ps2-kbd", ARK_DEV_CHR, ARK_MAJ_PS2KBD, 0) < 0)
        g_api->tty_debug(sid, "dev_init: mknod ps2-kbd failed");
    else
        g_api->tty_debug(sid, "dev_init: /dev/ps2-kbd ok (chr %d:0)", ARK_MAJ_PS2KBD);

    if (g_api->has_usb_kbd) {
        if (g_api->vfs_mknod("/dev/usb-kbd", ARK_DEV_CHR, ARK_MAJ_USBKBD, 0) < 0)
            g_api->tty_debug(sid, "dev_init: mknod usb-kbd failed");
        else
            g_api->tty_debug(sid, "dev_init: /dev/usb-kbd ok (chr %d:0)", ARK_MAJ_USBKBD);
    } else {
        g_api->tty_debug(sid, "dev_init: no USB keyboard, skipping /dev/usb-kbd");
    }

    if (g_api->has_e1000) {
        if (g_api->vfs_mknod("/dev/e1000", ARK_DEV_CHR, ARK_MAJ_E1000, 0) < 0)
            g_api->tty_debug(sid, "dev_init: mknod e1000 failed");
        else
            g_api->tty_debug(sid, "dev_init: /dev/e1000 ok (chr %d:0)", ARK_MAJ_E1000);
    } else {
        g_api->tty_debug(sid, "dev_init: no e1000 NIC, skipping /dev/e1000");
    }
}

/* ------------------------------------------------------------------ */
/*  Commands                                                           */
/* ------------------------------------------------------------------ */

static void cmd_ls(u32 sid, char *args) {
    char path[PATH_MAX];
    if (args[0])
        resolve_path(args, path, sizeof(path));
    else
        str_copy(path, g_cwd, sizeof(path));

    u32 n = g_api->vfs_list_count(path);
    if (n == 0) {
        g_api->tty_debug(sid, "ls: no entries in %s", path);
        return;
    }
    g_api->tty_debug(sid, "ls: %u entries in %s", (unsigned)n, path);
    for (u32 i = 0; i < n; i++) {
        char name[NAME_MAX];
        if (g_api->vfs_list_at(path, i, name, sizeof(name)))
            g_api->printk("  %s\n", name);
    }
}

static void cmd_cd(u32 sid, char *args) {
    if (!args[0]) {
        /* cd with no args goes to / like a minimal shell */
        str_copy(g_cwd, "/", sizeof(g_cwd));
        return;
    }

    /* handle .. */
    if (str_eq(args, "..")) {
        if (str_eq(g_cwd, "/")) return; /* already root */
        /* strip last path component */
        int len = (int)str_len(g_cwd);
        if (len > 0 && g_cwd[len - 1] == '/') len--; /* trailing slash */
        while (len > 0 && g_cwd[len - 1] != '/') len--;
        if (len == 0) len = 1; /* keep at least "/" */
        g_cwd[len] = '\0';
        return;
    }

    char newpath[PATH_MAX];
    resolve_path(args, newpath, sizeof(newpath));

    /* verify it exists as a directory via vfs_list_count —
       if count returns 0 and file_exists also says no, it's invalid */
    if (!g_api->vfs_file_exists(newpath) && g_api->vfs_list_count(newpath) == 0) {
        g_api->printk("cd: no such directory: %s\n", newpath);
        return;
    }

    str_copy(g_cwd, newpath, sizeof(g_cwd));
    (void)sid;
}

static void cmd_pwd(u32 sid) {
    (void)sid;
    g_api->printk("%s\n", g_cwd);
}

static void cmd_cat(u32 sid, char *args) {
    if (!args[0]) {
        g_api->tty_debug(sid, "cat: usage cat <path>");
        return;
    }
    char path[PATH_MAX];
    resolve_path(args, path, sizeof(path));

    int fd = g_api->vfs_open(path);
    if (fd < 0) {
        g_api->tty_debug(sid, "cat: cannot open '%s'", path);
        return;
    }
    u32 sz = g_api->vfs_file_size(fd);
    if (sz > 4096) sz = 4096;
    char buf[256];
    u32 off = 0;
    while (off < sz) {
        u32 chunk = sizeof(buf);
        if (off + chunk > sz) chunk = sz - off;
        int nr = g_api->vfs_read(fd, buf, chunk);
        if (nr <= 0) break;
        for (int i = 0; i < nr; i++) {
            char c = buf[i];
            if (c >= 32 && c < 127)  g_api->printk("%c", c);
            else if (c == '\n')       g_api->printk("\n");
            else if (c == '\t')       g_api->printk("    ");
        }
        off += (u32)nr;
    }
    g_api->printk("\n");
    g_api->vfs_close(fd);
}

static void cmd_cfetch(u32 sid) {
    (void)sid;
    g_api->printk("^__^\n");
    g_api->printk("(- -)\n");
    g_api->printk("----- >HAI!!\n");
}

static void cmd_echo(u32 sid, char *args) {
    (void)sid;
    if (args[0])
        g_api->printk("%s\n", args);
    else
        g_api->printk("Too few arguments: echo {arg}\n");
}

static void cmd_clear(u32 sid) {
    (void)sid;
    for (int i = 0; i < 36; i++)
        g_api->printk("\n");
}

/* ═══════════════════════════════════════════════════════════════
   Text Editor Command
   ═══════════════════════════════════════════════════════════════ */

#define ED_MAX_LINES 1000
#define ED_MAX_LINE_LEN 256

typedef struct {
    int line_count;
    char lines[ED_MAX_LINES][ED_MAX_LINE_LEN];
    char filename[PATH_MAX];
    int modified;
} editor_ctx_t;

static editor_ctx_t g_ed = {0};

static void ed_read_file(const char *path) {
    int fd = g_api->vfs_open(path);
    g_ed.line_count = 0;
    g_ed.modified = 0;
    
    if (fd < 0) {
        g_api->printk("(new file)\n");
        return;
    }

    u32 sz = g_api->vfs_file_size(fd);
    if (sz > 65536) sz = 65536;
    
    char buf[256];
    char line_buf[ED_MAX_LINE_LEN];
    int line_pos = 0;
    
    u32 off = 0;
    while (off < sz && g_ed.line_count < ED_MAX_LINES) {
        int nr = g_api->vfs_read(fd, buf, sizeof(buf));
        if (nr <= 0) break;
        
        for (int i = 0; i < nr; i++) {
            char c = buf[i];
            if (c == '\n') {
                if (line_pos > 0) {
                    line_buf[line_pos] = '\0';
                    str_copy(g_ed.lines[g_ed.line_count], line_buf, ED_MAX_LINE_LEN);
                    g_ed.line_count++;
                }
                line_pos = 0;
            } else if (line_pos + 1 < ED_MAX_LINE_LEN) {
                line_buf[line_pos++] = c;
            }
        }
        off += (u32)nr;
    }
    
    if (line_pos > 0 && g_ed.line_count < ED_MAX_LINES) {
        line_buf[line_pos] = '\0';
        str_copy(g_ed.lines[g_ed.line_count], line_buf, ED_MAX_LINE_LEN);
        g_ed.line_count++;
    }
    
    g_api->vfs_close(fd);
    g_api->printk("Loaded %d lines\n", g_ed.line_count);
}

static void ed_write_file(const char *path) {
    /* Note: Kernel VFS API doesn't provide write() function yet.
       File writing support planned for future release.
       For now, edited content is stored in memory only. */
    (void)path;
    
    g_ed.modified = 0;
    g_api->printk("Note: File write not yet implemented in kernel VFS.\n");
    g_api->printk("Content buffered in memory (%d lines)\n", g_ed.line_count);
}

static void ed_list(void) {
    if (g_ed.line_count == 0) {
        g_api->printk("[empty file]\n");
        return;
    }
    for (int i = 0; i < g_ed.line_count; i++) {
        g_api->printk("%4d | %s\n", i+1, g_ed.lines[i]);
    }
}

static void cmd_edit(u32 sid, char *args) {
    (void)sid;
    
    if (!args[0]) {
        g_api->printk("edit: usage: edit <filename>\n");
        return;
    }

    char path[PATH_MAX];
    resolve_path(args, path, sizeof(path));
    str_copy(g_ed.filename, path, sizeof(g_ed.filename));

    ed_read_file(path);
    
    g_api->printk("\n=== Text Editor ===\n");
    g_api->printk("Commands: i(insert) l(list) d(del) w(write) q(quit) wq(save+quit) h(help)\n\n");

    char line[LINE_MAX];
    while (1) {
        g_api->printk("ed> ");
        g_api->input_read(line, sizeof(line), 0);
        trim(line);

        if (!line[0]) continue;

        char cmd_char = line[0];
        char arg_str[64];
        str_copy(arg_str, line + 1, sizeof(arg_str));
        trim(arg_str);

        switch (cmd_char) {
            case 'i': {
                g_api->printk("Insert mode (empty line to exit):\n");
                while (1) {
                    g_api->printk("  > ");
                    g_api->input_read(line, sizeof(line), 0);
                    int len = str_len(line);
                    if (len > 0 && line[len-1] == '\n') line[len-1] = '\0';
                    if (!line[0]) break;
                    
                    if (g_ed.line_count < ED_MAX_LINES) {
                        str_copy(g_ed.lines[g_ed.line_count], line, ED_MAX_LINE_LEN);
                        g_ed.line_count++;
                        g_ed.modified = 1;
                    } else {
                        g_api->printk("File full\n");
                    }
                }
                break;
            }
            case 'l':
                ed_list();
                break;
            case 'd': {
                int lineno = 0;
                unsigned i = 0;
                while (arg_str[i] && arg_str[i] >= '0' && arg_str[i] <= '9') {
                    lineno = lineno * 10 + (arg_str[i] - '0');
                    i++;
                }
                if (lineno > 0 && lineno <= g_ed.line_count) {
                    for (int j = lineno - 1; j < g_ed.line_count - 1; j++) {
                        str_copy(g_ed.lines[j], g_ed.lines[j+1], ED_MAX_LINE_LEN);
                    }
                    g_ed.line_count--;
                    g_ed.modified = 1;
                    g_api->printk("Deleted line %d\n", lineno);
                } else {
                    g_api->printk("Invalid line\n");
                }
                break;
            }
            case 'w':
                ed_write_file(g_ed.filename);
                break;
            case 'q':
                if (g_ed.modified) {
                    g_api->printk("Modified. Save first? (y/n): ");
                    g_api->input_read(line, 10, 0);
                    if (line[0] == 'y' || line[0] == 'Y') {
                        ed_write_file(g_ed.filename);
                    }
                }
                g_api->printk("Editor closed\n");
                return;
            case 'h':
            case '?':
                g_api->printk("i - insert lines\nl - list lines\nd N - delete line N\nw - write file\nq - quit\nwq - write+quit\nh - help\n");
                break;
            default:
                if (str_eq(line, "wq")) {
                    ed_write_file(g_ed.filename);
                    g_api->printk("Editor closed\n");
                    return;
                }
                g_api->printk("Unknown command\n");
        }
    }
}

static void cmd_help(u32 sid) {
    g_api->tty_debug(sid, "commands: ls cd pwd cat echo edit clear cfetch help exit");
    g_api->printk("  ls [path]    list directory (default: cwd)\n");
    g_api->printk("  cd <path>    change directory (.. supported)\n");
    g_api->printk("  pwd          print current directory\n");
    g_api->printk("  cat <path>   print file contents\n");
    g_api->printk("  edit <file>  text editor\n");
    g_api->printk("  echo ...     echo args\n");
    g_api->printk("  clear        scroll screen\n");
    g_api->printk("  cfetch       just a demo\n");
    g_api->printk("  help         this message\n");
    g_api->printk("  exit         exit shell\n");
}

static int run_cmd(u32 sid, char *cmd, char *args) {
    if (str_eq(cmd, "exit"))    return 0;
    if (str_eq(cmd, "ls"))      { cmd_ls(sid, args);   return 0; }
    if (str_eq(cmd, "cd"))      { cmd_cd(sid, args);   return 0; }
    if (str_eq(cmd, "pwd"))     { cmd_pwd(sid);        return 0; }
    if (str_eq(cmd, "cat"))     { cmd_cat(sid, args);  return 0; }
    if (str_eq(cmd, "edit"))    { cmd_edit(sid, args); return 0; }
    if (str_eq(cmd, "echo"))    { cmd_echo(sid, args); return 0; }
    if (str_eq(cmd, "clear"))   { cmd_clear(sid);      return 0; }
    if (str_eq(cmd, "help"))    { cmd_help(sid);       return 0; }
    if (str_eq(cmd, "cfetch"))  { cmd_cfetch(sid);     return 0; }
    g_api->tty_debug(sid, "unknown command: %s (try help)", cmd);
    return 0;
}

/* ------------------------------------------------------------------ */
/*  Entry                                                              */
/* ------------------------------------------------------------------ */

int main(const ark_kernel_api_t *api) {
    if (!api || api->version < 2) {
        if (api && api->printk)
            api->printk("[init] need API v2 (VFS+TTY)\n");
        return 1;
    }
    g_api = api;

    u32 sid = api->tty_alloc();
    if (sid == (u32)-1) {
        api->printk("[init] tty_alloc failed\n");
        return 1;
    }
    api->tty_switch(sid);

    char ttyname[16];
    api->tty_get_name(sid, ttyname, sizeof(ttyname));
    api->tty_debug(sid, "shell started on %s (session id %u)", ttyname, (unsigned)sid);

    usr_init(sid);
    dev_init(sid);

    /* If kernel invoked us as interpreter for a script (e.g. /init with #!/bin/sh), run that script first */
    if (api->get_startup_script_path) {
        const char *script_path = api->get_startup_script_path();
        if (script_path && script_path[0]) {
            api->tty_debug(sid, "running script: %s", script_path);
            int fd = api->vfs_open(script_path);
            if (fd >= 0) {
                u32 sz = api->vfs_file_size(fd);
                if (sz > 8192) sz = 8192;
                char line[LINE_MAX];
                u32 off = 0;
                while (off < sz) {
                    u32 line_len = 0;
                    while (line_len + 1 < sizeof(line) && off < sz) {
                        int nr = api->vfs_read(fd, line + line_len, 1);
                        if (nr <= 0) break;
                        off++;
                        if (line[line_len] == '\n' || line[line_len] == '\r') break;
                        line_len++;
                    }
                    line[line_len] = '\0';
                    trim(line);
                    if (line[0] && line[0] != '#') {
                        char cmd[ARG_MAX], args[ARG_MAX];
                        parse(line, cmd, sizeof(cmd), args, sizeof(args));
                        if (run_cmd(sid, cmd, args))
                            break;
                    }
                }
                api->vfs_close(fd);
                api->tty_debug(sid, "script %s finished", script_path);
            } else {
                api->tty_debug(sid, "could not open script: %s", script_path);
            }
        }
    }

    api->printk("Ark shell on %s. Type 'help' for commands.\n", ttyname);
    api->printk("This is a demo shell. It doesn't support features like command history or job control.\n");
    api->printk("This is just for the user to test filesystem\n");
    for (;;) {
        #define H_ID "ark123"  /* hardcoded host ID for demo purposes */
        /* K_ID comes from ark/uid.h */
        api->printk("[" K_ID "@"H_ID":%s]$ ", g_cwd);
        char line[LINE_MAX];
        api->input_read(line, sizeof(line), 0);
        trim(line);
        if (!line[0]) continue;

        char cmd[ARG_MAX], args[ARG_MAX];
        parse(line, cmd, sizeof(cmd), args, sizeof(args));
        if (run_cmd(sid, cmd, args))
            break;
    }

    api->tty_debug(sid, "[INIT ENDED!!]");
    api->tty_free(sid);
    return 0;
}