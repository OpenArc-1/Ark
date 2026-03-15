/*
 * gen/shell.c — Ark interactive kernel shell
 *
 * Uses input_read() / input_getc() from gen/input.c which now sleeps
 * with HLT between keystrokes — no busy-spin, no prompt flood.
 *
 * The old [ark:/]$ flood was caused by:
 *   - run_text_script() in init.c looping after each unknown command
 *   - input_getc() busy-spinning with no HLT, returning 0 immediately
 *   - the loop reprinting the prompt thousands of times per second
 *
 * This shell only prints the prompt ONCE, then blocks inside input_read()
 * which calls input_getc() → input_poll() → HLT until a key arrives.
 */

#include <ark/types.h>
#include <ark/printk.h>
#include <ark/ramfs.h>
#include <ark/elf_loader.h>
#include <ark/init_api.h>
#include <ark/input.h>
#include <ark/kconfig.h>

/* input_read is in gen/input.c */
extern void input_read(char *buf, int max_len, bool hide_input);

/* ── Built-in commands ───────────────────────────────────────────── */
static void cmd_help(void) {
    printk("Ark shell — built-in commands:\n");
    printk("  help          — this message\n");
    printk("  ls            — list files in ramfs\n");
    printk("  cat <file>    — print a ramfs file\n");
    printk("  run <file>    — execute an ELF from ramfs\n");
    printk("  clear         — clear the screen\n");
    printk("  reboot        — reboot the system\n");
    printk("  halt          — halt the CPU\n");
    printk("  <path>        — shorthand for: run <path>\n");
}

static void cmd_ls(void) {
    extern void ramfs_list_files(void);
    ramfs_list_files();
}

static void cmd_cat(const char *arg) {
    if (!arg || !arg[0]) { printk("cat: missing filename\n"); return; }
    char path[256]; u32 pi = 0;
    if (arg[0] != '/') path[pi++] = '/';
    for (u32 i = 0; arg[i] && pi + 1 < 256; i++) path[pi++] = arg[i];
    path[pi] = '\0';
    u32 sz = 0;
    u8 *data = ramfs_get_file(path, &sz);
    if (!data || !sz) { printk("cat: %s: not found\n", path); return; }
    for (u32 i = 0; i < sz; i++) {
        char c = (char)data[i];
        if (c != '\r') printk("%c", c);
    }
    if (sz && data[sz-1] != '\n') printk("\n");
}

static void cmd_run(const char *arg) {
    if (!arg || !arg[0]) { printk("run: missing filename\n"); return; }
    char path[256]; u32 pi = 0;
    if (arg[0] != '/') path[pi++] = '/';
    for (u32 i = 0; arg[i] && pi + 1 < 256; i++) path[pi++] = arg[i];
    path[pi] = '\0';
    u32 sz = 0;
    u8 *data = ramfs_get_file(path, &sz);
    if (!data || !sz) { printk("run: %s: not found\n", path); return; }
    int ec = elf_execute(data, sz, ark_kernel_api());
    printk("\n[exited %d]\n", ec);
}

static void cmd_clear(void) {
    extern void clear_screen(void);
    clear_screen();
}

static void cmd_reboot(void) {
    printk("Rebooting...\n");
    for (volatile int i = 0; i < 500000; i++);
    __asm__ volatile(
        "1: inb $0x64, %%al\n"
        "   testb $0x02, %%al\n"
        "   jnz 1b\n"
        "   movb $0xFE, %%al\n"
        "   outb %%al, $0x64\n"
        : : : "eax");
    for (;;) __asm__ volatile("hlt");
}

static void cmd_halt(void) {
    printk("System halted.\n");
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}

/* ── strncmp helper (avoid libc dependency) ──────────────────────── */
static int sh_strcmp(const char *a, const char *b) {
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}

/* ── Shell main loop ─────────────────────────────────────────────── */
void ark_shell_run(void) {
    printk("\nArk shell ready. Type 'help' for commands.\n\n");

    char line[256];
    for (;;) {
        /* Print prompt ONCE, then BLOCK inside input_read() via HLT */
        printk("[ark:/]$ ");
        input_read(line, sizeof(line), false);

        /* Skip empty lines — just reprint the prompt */
        u32 i = 0;
        while (line[i] == ' ' || line[i] == '\t') i++;
        if (!line[i]) continue;

        /* Split into command + argument */
        char cmd[64], arg[192];
        u32 ci = 0, ai = 0;
        while (line[i] && line[i] != ' ' && line[i] != '\t' && ci + 1 < 64)
            cmd[ci++] = line[i++];
        cmd[ci] = '\0';
        while (line[i] == ' ' || line[i] == '\t') i++;
        while (line[i] && ai + 1 < 192) arg[ai++] = line[i++];
        arg[ai] = '\0';

        if      (!sh_strcmp(cmd, "help"))   cmd_help();
        else if (!sh_strcmp(cmd, "ls"))     cmd_ls();
        else if (!sh_strcmp(cmd, "cat"))    cmd_cat(arg);
        else if (!sh_strcmp(cmd, "run"))    cmd_run(arg);
        else if (!sh_strcmp(cmd, "clear"))  cmd_clear();
        else if (!sh_strcmp(cmd, "reboot")) cmd_reboot();
        else if (!sh_strcmp(cmd, "halt"))   cmd_halt();
        else                                cmd_run(cmd); /* try as ELF path */
    }
}
