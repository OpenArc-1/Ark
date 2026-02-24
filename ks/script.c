/**
 * script.c - Ark .init script runner
 *
 * Handles the legacy #!init script format:
 *
 *   #!init
 *   # comment
 *   file:/some/elf        <- load and run an ELF from ramfs
 *   echo hello world      <- print a line
 *   printk some message   <- same as echo
 *   /bin/sh               <- load and run /bin/sh from ramfs
 *
 * Called by the universal init executor in gen/init.c when a
 * /init file starts with "#!init".  Also scanned for any .init
 * file in ramfs as a fallback.
 */

#include "ark/types.h"
#include "ark/printk.h"
#include "ark/ramfs.h"
#include "ark/elf_loader.h"
#include "ark/input.h"
#include "ark/init_api.h"

void kbd_poll(void);
bool kbd_is_initialized(void);

#define SCRIPT_MAX_LINE 512

/* ------------------------------------------------------------------ */
/* Helpers                                                             */
/* ------------------------------------------------------------------ */

static u32 read_line(const u8 *data, u32 size, u32 *pos,
                     char *out, u32 max) {
    if (*pos >= size) return 0;
    u32 i = 0;
    while (*pos < size && i + 1 < max) {
        char c = (char)data[(*pos)++];
        if (c == '\n' || c == '\r') {
            /* swallow companion \r or \n */
            if (*pos < size &&
                ((c == '\r' && data[*pos] == '\n') ||
                 (c == '\n' && data[*pos] == '\r')))
                (*pos)++;
            break;
        }
        out[i++] = c;
    }
    out[i] = '\0';
    return i;
}

/* trim leading whitespace in-place, return new start pointer */
static char *ltrim(char *s) {
    while (*s == ' ' || *s == '\t') s++;
    return s;
}

static u8 starts_with(const char *s, const char *prefix) {
    while (*prefix) {
        if (*s != *prefix) return 0;
        s++; prefix++;
    }
    return 1;
}

/* ------------------------------------------------------------------ */
/* Execute one script line                                             */
/* ------------------------------------------------------------------ */

static u8 exec_line(const char *line) {
    const ark_kernel_api_t *api = ark_kernel_api();

    /* file:/path  -> run ELF from ramfs */
    if (starts_with(line, "file:")) {
        const char *path = line + 5;
        while (*path == ' ' || *path == '\t') path++;
        if (!*path) { api->printk("script: file: missing path\n"); return 0; }
        u32 sz = 0;
        u8 *data = ramfs_get_file(path, &sz);
        if (!data || sz == 0) {
            api->printk("script: file not found: %s\n", path);
            return 0;
        }
        printk(T, "script: exec %s (%u bytes)\n", path, sz);
        if (kbd_is_initialized()) kbd_poll();
        int ec = elf_execute(data, sz, api);
        printk(T, "script: %s exited %d\n", path, ec);
        return 1;
    }

    /* echo / printk */
    if (starts_with(line, "echo ") || starts_with(line, "printk ")) {
        const char *msg = line + (starts_with(line, "echo ") ? 5 : 7);
        api->printk("%s\n", msg);
        return 1;
    }

    /* bare /path or name -> look up in ramfs and run as ELF */
    char fpath[256];
    u32 fp = 0;
    const char *p = line;
    if (*p != '/') fpath[fp++] = '/';
    while (*p && *p != ' ' && *p != '\t' && fp + 1 < sizeof(fpath))
        fpath[fp++] = *p++;
    fpath[fp] = '\0';

    u32 sz = 0;
    u8 *data = ramfs_get_file(fpath, &sz);
    if (data && sz > 0) {
        printk(T, "script: exec %s (%u bytes)\n", fpath, sz);
        if (kbd_is_initialized()) kbd_poll();
        int ec = elf_execute(data, sz, api);
        printk(T, "script: %s exited %d\n", fpath, ec);
        return 1;
    }

    api->printk("script: unknown command: %s\n", line);
    return 0;
}

/* ------------------------------------------------------------------ */
/* Run a #!init script buffer                                          */
/* ------------------------------------------------------------------ */

static u8 run_init_script(const u8 *data, u32 size) {
    u32 pos = 0;
    char line[SCRIPT_MAX_LINE];
    u8 executed = 0;

    /* skip shebang line */
    read_line(data, size, &pos, line, sizeof(line));

    while (pos < size) {
        u32 len = read_line(data, size, &pos, line, sizeof(line));
        if (len == 0 && pos >= size) break;

        char *ln = ltrim(line);
        if (!*ln || *ln == '#') continue; /* blank or comment */

        printk(T, "script: $ %s\n", ln);
        if (exec_line(ln))
            executed = 1;
    }
    return executed;
}

/* ------------------------------------------------------------------ */
/* Public: scan ramfs for #!init files and run them                   */
/* ------------------------------------------------------------------ */

u8 script_scan_and_execute(void) {
    u32 file_count = ramfs_get_file_count();
    printk(T, "script: scanning %u ramfs file(s) for #!init\n", file_count);

    for (u32 i = 0; i < file_count; i++) {
        char fname[RAMFS_MAX_FILENAME];
        u8  *fdata = NULL;
        u32  fsize = 0;

        if (!ramfs_get_file_by_index(i, fname, &fdata, &fsize)) continue;
        if (!fdata || fsize < 2) continue;
        if (fdata[0] != '#' || fdata[1] != '!') continue;

        /* check shebang word is "init" */
        u32 k = 2;
        while (k < fsize && (fdata[k]==' '||fdata[k]=='\t')) k++;
        if (k + 4 > fsize) continue;
        if (fdata[k]!='i'||fdata[k+1]!='n'||fdata[k+2]!='i'||fdata[k+3]!='t') continue;
        char c = (k+4 < fsize) ? (char)fdata[k+4] : '\0';
        if (c && c!=' '&&c!='\t'&&c!='\n'&&c!='\r') continue;

        printk(T, "script: found #!init script: %s (%u bytes)\n", fname, fsize);
        if (run_init_script(fdata, fsize))
            return 1;
    }

    printk(T, "script: no #!init scripts found\n");
    return 0;
}
