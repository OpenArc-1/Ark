/**
 * Ark userspace shell (init.bin)
 *
 * Multi-command shell with real FS usage (VFS). Uses TTY for session IDs
 * and Linux-like debug output. Entry: int _start(const ark_kernel_api_t *api)
 */

#include "ark/init_api.h"
#include "ark/uid.h"

#define LINE_MAX  256
#define NAME_MAX  64
#define ARG_MAX   128

static const ark_kernel_api_t *g_api;

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

/* Parse "cmd args": first word -> cmd, rest -> args (trimmed). */
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

static void cmd_ls(u32 sid, char *args) {
    const char *path = args[0] ? args : "/";
    if (!str_eq(path, "/")) {
        g_api->tty_debug(sid, "ls: only \"/\" supported");
        return;
    }
    u32 n = g_api->vfs_list_count("/");
    g_api->tty_debug(sid, "ls: %u files", (unsigned)n);
    for (u32 i = 0; i < n; i++) {
        char name[NAME_MAX];
        if (g_api->vfs_list_at("/", i, name, sizeof(name)))
            g_api->printk("  %s\n", name);
    }
}

static void cmd_cat(u32 sid, char *args) {
    if (!args[0]) {
        g_api->tty_debug(sid, "cat: usage cat <path>");
        return;
    }
    int fd = g_api->vfs_open(args);
    if (fd < 0) {
        g_api->tty_debug(sid, "cat: cannot open '%s'", args);
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
            if (c >= 32 && c < 127)
                g_api->printk("%c", c);
            else if (c == '\n')
                g_api->printk("\n");
            else if (c == '\t')
                g_api->printk("    ");
        }
        off += (u32)nr;
    }
    g_api->printk("\n");
    g_api->vfs_close(fd);
}
static void cmd_cfetch(u32 sid){
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
        g_api->printk("Too few arguments echo {arg}\n");
}

static void cmd_clear(u32 sid) {
    (void)sid;
    for (int i = 0; i < 36; i++)
        g_api->printk("\n");
}

static void cmd_help(u32 sid) {
    g_api->tty_debug(sid, "commands: ls [path] cat <path> echo [args] clear help exit");
    g_api->printk("  ls [/]     list files at / (ramfs)\n");
    g_api->printk("  cat <path> print file contents\n");
    g_api->printk("  echo ...   echo args\n");
    g_api->printk("  clear      scroll screen\n");
    g_api->printk("  help       this message\n");
    g_api->printk("  exit       exit shell\n");
    g_api->printk("  cfetch     just a demo\n");
}

static int run_cmd(u32 sid, char *cmd, char *args) {
    if (str_eq(cmd, "exit"))
        return 1;
    if (str_eq(cmd, "ls"))   { cmd_ls(sid, args);   return 0; }
    if (str_eq(cmd, "cat"))  { cmd_cat(sid, args);  return 0; }
    if (str_eq(cmd, "echo")) { cmd_echo(sid, args); return 0; }
    if (str_eq(cmd, "clear")) { cmd_clear(sid);     return 0; }
    if (str_eq(cmd, "help")) { cmd_help(sid);       return 0; }
    if (str_eq(cmd, "cfetch")) { cmd_cfetch(sid);   return 0; }
    g_api->tty_debug(sid, "unknown command: %s (try help)", cmd);
    return 0;
}

int _start(const ark_kernel_api_t *api) {
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
    api->printk("test shell on %s. Type 'help' for commands.\n\n", ttyname);

    for (;;) {
	api->printk("$ "); //two sperate lines for prompting 
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