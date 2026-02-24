/**
 * Core kernel boot flow for Ark.
*/
#include <hw/vendor.h>
#include <ark/types.h>
#include <ark/printk.h>
#include <ark/panic.h>
#include <ark/clear.h>
#include <ark/usb.h>
#include <ark/ip.h>
#include <ark/fb.h>
#include <ark/ramfs.h>
#include <ark/ata.h>
#include <ark/sata.h>
#include <ark/elf_loader.h>
#include <ark/userspacebuf.h>
#include <ark/script.h>
#include <ark/time.h>
#include <ark/pci.h>
#include <ark/init_api.h>
#include <ark/elf_loader.h>
#include "../mp/built-in.h"
#include <ark/uid.h>
#include <aud/ac97.h>
#include <hw/ramcheck.h>
#include "clr.h"
#include <pci/eth-dev.h>
#include <gpu/vesa.h>
#include <hw/vendor.h>
#include <aud/aud-dev.h>
#include <ark/sd-dev.h>

extern void ac97_test();
extern void ac97_init();
extern void show_sysinfo_bios(void);
extern void clear_screen(void);
extern void usb_init(void);
extern void ip_init(void);
extern void ip_poll(void);
extern void fs_built_in_init(void);
extern int  disk_load_init(void);
extern void fb_init(const ark_fb_info_t *info);
extern void serial_init(void);
extern void idt_init(void);
extern void ramfs_init(void);
extern void ramfs_mount(void);
extern u8   ramfs_has_init(void);
extern u8  *ramfs_get_init(u32 *out_size);
extern ark_fb_info_t   g_fb_info;
extern uspace_buffer_t g_uspace_buffer;

u8   fs_has_init(void);
void fs_mount_root(void);

/* Parse shebang (#! or #/bin/sh) from start of script; return 1 if script, 0 otherwise.
   If script, out_interpreter is filled with path (e.g. "/bin/sh"), max out_len bytes. */
static u8 parse_shebang_interpreter(const u8 *data, u32 size, char *out_interpreter, u32 out_len) {
    if (!data || size < 3 || !out_interpreter || out_len < 2) return 0;
    /* #! or #/ */
    if (data[0] != '#') return 0;
    if (data[1] != '!' && data[1] != '/') return 0;
    u32 i = 2;
    while (i < size && (data[i] == ' ' || data[i] == '\t')) i++;
    u32 j = 0;
    while (i < size && j + 1 < out_len) {
        char c = (char)data[i];
        if (c == '\n' || c == '\r' || c == ' ' || c == '\t') break;
        out_interpreter[j++] = c;
        i++;
    }
    out_interpreter[j] = '\0';
    if (j == 0) return 0;
    if (out_interpreter[0] != '/' && j + 2 <= out_len) {
        /* Prepend / so /bin/sh not bin/sh */
        for (u32 k = j + 1; k > 0; k--) out_interpreter[k] = out_interpreter[k - 1];
        out_interpreter[0] = '/';
    }
    return 1;
}
void input_init(void);
void input_poll(void);
void scanAll(void);
void cpu_verify(void);
void mem_verify(void);
extern void get_eth_devices();

/* ══════════════════════════════════════════════════════════════════════════
 * Boot timestamp counter
 *
 * We don't have a real timer yet at early boot, so we use a software
 * tick counter incremented by busy_delay().  Each "unit" of busy_delay
 * maps to a fixed microsecond estimate so the log looks realistic.
 *
 * busy_delay(20000000) ≈ ~100 ms on a 400 MHz effective loop rate
 * → we add 100000 µs per 20000000-loop call.
 * ══════════════════════════════════════════════════════════════════════════ */
static u32 boot_us = 0;   /* microseconds since kernel_main entered */

/* Call this instead of busy_delay() everywhere — keeps the clock ticking */
static void delay(u32 loops) {
    for (volatile u32 i = 0; i < loops; ++i)
        __asm__ __volatile__("");
    /* Estimate: 20,000,000 loops ≈ 100,000 µs (100 ms) */
    boot_us += (loops / 200);
}

/* ══════════════════════════════════════════════════════════════════════════
 * Helpers
 * ══════════════════════════════════════════════════════════════════════════ */
static void print_userspace_output(void) {
    while (g_uspace_buffer.read_pos < g_uspace_buffer.write_pos) {
        u32 pos = g_uspace_buffer.read_pos % USP_BUFFER_SIZE;
        printk("%c", g_uspace_buffer.buffer[pos]);
        g_uspace_buffer.read_pos++;
    }
}

static void wait_for_init_bin(void) {
    if (fs_has_init()) {
        printk(T,"ark-init: found /init in ramfs (loaded by bootloader)\n");
        return;
    }
    printk(T,"ark-init: /init not found in ramfs\n");
}

/* ══════════════════════════════════════════════════════════════════════════
 * kernel_main
 * ══════════════════════════════════════════════════════════════════════════ */
void kernel_main(void) {
    /* arch_x86_entry already set up the framebuffer and printk routing.
     * Just print the banner here -- it goes to VESA text or VGA text
     * depending on what the bootloader gave us. */
    printk(T,"Ark kernel  %s  build: %s\n", K_ID, BUILD);
    printk(T,"Display: %ux%ux32 VESA framebuffer (%ux%u chars, PSF font)\n",
           vesa_get_width(), vesa_get_height(),
           vesa_get_width() / 8, vesa_get_height() / 16);

    cpu_verify();
    mem_verify();
    /* Paging intentionally disabled — enabling it without mapping the VESA
    * framebuffer region (0xE0000000) causes an immediate page fault and
     * reboot when printk tries to render text. */
    serial_init();
    printk(T,"Serial: COM1 ready\n");

    /* IDT */
    idt_init();
    printk(T,"IDT: initialized (int 0x80 syscall gate ready)\n");

    /* PCI scan */
    printk(T,"PCI: scanning bus\n");
    scanAll();

    /* Audio */
#if CONFIG_AUDIO_ENABLE
    kernel_aud_init();
#else
    printk(T,"audio: disabled by config\n");
#endif

    /* BIOS info */
    printk(T,"BIOS: reading system info\n");
    show_sysinfo_bios();

    /* RTC time */
    rtc_time_t t = read_rtc();
    printk(T,"rtc: %02d:%02d:%02d UTC\n", t.hour, t.min, t.sec);

    /* ── USB — must come before input so kbd driver sees live HC ── */
#if CONFIG_USB_ENABLE
    printk(T,"USB: initializing host controllers\n");
    usb_init();
    scan_usb_controllers();
    printk(T,"USB: subsystem ready\n");
    #else
    printk(T,"USB: disabled by config\n");
    #endif
    
    /* ── Input — after USB ─────────────────────────────────────── */
    printk(T,"input: initializing PS/2 + USB HID\n");
    input_init();
    printk(T,"input: ready\n");
    /* ── Network ───────────────────────────────────────────────── */
#if CONFIG_NET_ENABLE
    printk(T,"eth: scanning for ethernet controllers\n");
    print_eth_devices();
    //e1000_init(); //memory leaker

    printk(T,"ip: initializing network stack\n");
    ip_init();
    printk(T,"ip: ready\n");
#else
    printk(T,"net: disabled by config\n");
#endif

    /* ── Storage ───────────────────────────────────────────────── */
    printk(T,"ata/sata: initializing storage drivers\n");
    fs_built_in_init();
    printk(T,"ata/sata: ready\n");

    /* ── Disk .init loader ─────────────────────────────────────────
     * If no /init came from a ZIP initramfs, scan ATA disks for a
     * FAT16/FAT32 partition containing an INIT file and load it. */
    if (!fs_has_init()) {
        printk(T,"disk: no /init in ramfs — scanning disks\n");
        disk_load_init();
    } else {
        printk(T,"disk: /init already in ramfs, skipping disk scan\n");
    }

    /* ── initramfs / ramfs ────────────────────────── */
    /* arch_x86_entry already called ramfs_init() + loaded any ZIP
     * initramfs before kernel_main() was called. We mount here.
     * Mirrors Linux: initramfs unpacked by arch, kernel mounts, /init runs. */
    printk(T,"initramfs: mounting root filesystem\n");
    fs_mount_root();
    printk(T,"initramfs: mounted at /\n");

    extern void ramfs_list_files(void);
    {
        u32 fc = ramfs_get_file_count();
        if (fc > 0) {
            printk(T,"initramfs: %u file(s) unpacked from ZIP initramfs\n", fc);
        } else {
            printk(T,"initramfs: empty -- boot with a ZIP containing /init\n");
            printk(T,"initramfs:   e.g.  module /boot/initramfs.zip\n");
        }
    }
    ramfs_list_files();
    u8 script_found = 0;

    /* ── Universal init executor ────────────────────────── */
    /* Probe /init and dispatch based on what it actually is.
     * Priority order (mirrors Linux behaviour + Ark extensions):
     *   1. ELF binary           -> elf_execute()
     *   2. #! shebang script    -> run interpreter from ramfs
     *   3. #!init + file:/path  -> legacy Ark .init script (runs named binary)
     *   4. Flat/plain text      -> run each non-comment line as a shell command
     *                             via the kernel built-in command runner
     *   5. Raw bytes (no magic) -> attempt direct execution as flat binary
     */
    printk(T,"init: probing /init\n");
    {
        u32 init_size = 0;
        u8 *init_data = ramfs_get_init(&init_size);

        if (!init_data || init_size == 0) {
            printk(T,"init: /init not found in ramfs\n");
            goto try_legacy_script;
        }

        printk(T,"init: /init found (%u bytes)\n", init_size);

        /* ── 1. ELF binary ───────────────────────────────── */
        if (init_size >= 4 &&
            init_data[0] == 0x7f && init_data[1] == 'E' &&
            init_data[2] == 'L'  && init_data[3] == 'F') {
            printk(T,"init: detected ELF binary\n");
            g_uspace_buffer.read_pos = g_uspace_buffer.write_pos = 0;
            g_uspace_buffer.activity_flag = 0;
            int ec = elf_execute(init_data, init_size, ark_kernel_api());
            if (g_uspace_buffer.write_pos > 0) print_userspace_output();
            printk(T,"init: ELF exited %d\n", ec);
            script_found = 1;
            goto script_done;
        }

        /* ── 2. Shebang (#!) ─────────────────────────── */
        if (init_size >= 2 && init_data[0] == '#' && init_data[1] == '!') {
            char interp[256];
            /* Read interpreter path from shebang line */
            u32 i = 2;
            while (i < init_size && (init_data[i] == ' ' || init_data[i] == '\t')) i++;
            u32 j = 0;
            while (i < init_size && j + 1 < sizeof(interp)) {
                char c = (char)init_data[i];
                if (c == '\n' || c == '\r' || c == ' ' || c == '\t') break;
                interp[j++] = c; i++;
            }
            interp[j] = '\0';

            /* ─ 3. Legacy #!init script with file:/ directive ─ */
            if (j >= 4 && interp[0]=='i' && interp[1]=='n' && interp[2]=='i' && interp[3]=='t'
                && (interp[4]=='\0' || interp[4]==' ')) {
                printk(T,"init: detected #!init script (legacy Ark format)\n");
                /* scan_and_execute already handles this format */
                u8 ok = script_scan_and_execute();
                if (ok) { script_found = 1; goto script_done; }
                printk(T,"init: #!init script execution failed\n");
                goto try_legacy_script;
            }

            /* Real shebang: look up interpreter in ramfs */
            if (j > 0) {
                printk(T,"init: shebang interpreter: %s\n", interp);
                u32 isz = 0;
                u8 *idata = ramfs_get_file(interp, &isz);
                if (idata && isz > 0) {
                    ark_set_startup_script_path("/init");
                    g_uspace_buffer.read_pos = g_uspace_buffer.write_pos = 0;
                    g_uspace_buffer.activity_flag = 0;
                    int ec = elf_execute(idata, isz, ark_kernel_api());
                    ark_set_startup_script_path(NULL);
                    if (g_uspace_buffer.write_pos > 0) print_userspace_output();
                    printk(T,"init: interpreter exited %d\n", ec);
                    script_found = 1;
                    goto script_done;
                }
                printk(T,"init: interpreter '%s' not in ramfs\n", interp);
            }

            /* ─ 4. Plain-text command script (no interpreter needed) ─ */
            printk(T,"init: running as plain-text command script\n");
            {
                const ark_kernel_api_t *api = ark_kernel_api();
                u32 pos = 0;
                /* skip shebang line */
                while (pos < init_size && init_data[pos] != '\n') pos++;
                if (pos < init_size) pos++; /* skip newline */

                while (pos < init_size) {
                    /* read one line */
                    char line[256];
                    u32 li = 0;
                    while (pos < init_size && li + 1 < sizeof(line)) {
                        char c = (char)init_data[pos++];
                        if (c == '\n' || c == '\r') break;
                        line[li++] = c;
                    }
                    line[li] = '\0';
                    /* skip blanks and comments */
                    char *ln = line;
                    while (*ln == ' ' || *ln == '\t') ln++;
                    if (!*ln || *ln == '#') continue;

                    /* print and execute via printk so user sees it */
                    api->printk("$ %s\n", ln);

                    /* parse: first word = command, rest = args */
                    char cmd[64], args[192];
                    u32 ci = 0;
                    while (*ln && *ln != ' ' && *ln != '\t' && ci+1 < sizeof(cmd))
                        cmd[ci++] = *ln++;
                    cmd[ci] = '\0';
                    while (*ln == ' ' || *ln == '\t') ln++;
                    u32 ai = 0;
                    while (*ln && ai+1 < sizeof(args)) args[ai++] = *ln++;
                    args[ai] = '\0';

                    /* built-in commands recognised by the kernel runner */
                    if (cmd[0]=='e' && cmd[1]=='c' && cmd[2]=='h' && cmd[3]=='o' && !cmd[4])
                        api->printk("%s\n", args);
                    else if (cmd[0]=='p' && cmd[1]=='r' && cmd[2]=='i' && cmd[3]=='n' && cmd[4]=='t' && cmd[5]=='k' && !cmd[6])
                        api->printk("%s\n", args);
                    else {
                        /* treat as path to an ELF in ramfs */
                        char fpath[256];
                        u32 fp = 0;
                        if (cmd[0] != '/') { fpath[fp++]='/'; }
                        for (u32 k=0; cmd[k] && fp+1<sizeof(fpath); k++) fpath[fp++]=cmd[k];
                        fpath[fp] = '\0';
                        u32 bsz = 0;
                        u8 *bdata = ramfs_get_file(fpath, &bsz);
                        if (bdata && bsz > 0) {
                            g_uspace_buffer.read_pos = g_uspace_buffer.write_pos = 0;
                            g_uspace_buffer.activity_flag = 0;
                            elf_execute(bdata, bsz, ark_kernel_api());
                            if (g_uspace_buffer.write_pos > 0) print_userspace_output();
                        } else {
                            api->printk("init: unknown command: %s\n", cmd);
                        }
                    }
                }
                script_found = 1;
                goto script_done;
            }
        }

        /* ── 5. Raw/flat binary (no magic, no shebang) ───────────── */
        printk(T,"init: no ELF/shebang magic, trying as raw binary\n");
        {
            g_uspace_buffer.read_pos = g_uspace_buffer.write_pos = 0;
            g_uspace_buffer.activity_flag = 0;
            int ec = elf_execute(init_data, init_size, ark_kernel_api());
            if (g_uspace_buffer.write_pos > 0) print_userspace_output();
            printk(T,"init: raw binary exited %d\n", ec);
            script_found = 1;
            goto script_done;
        }
    }

try_legacy_script:
    /* Fall back to scanning all ramfs files for #!init scripts */
    printk(T,"init: scanning ramfs for #!init scripts\n");
    {
        u8 ok = script_scan_and_execute();
        if (ok) { script_found = 1; goto script_done; }
    }
    printk(T,"init: no runnable init found\n");
script_done:
    if (!script_found) {
        printk(T,"init: no usable init found\n");
        if (!fs_has_init()) {
            printk(T,"init: hint — use: make run-with-init\n");
        }
        printk(T,"Kernel panic — not syncing: No init found\n");
 //       kernel_panic("No init found");
    } else {
        printk(T,"init: complete\n");
        printk(T,"kernel: entering idle\n");
    }
}

u8   fs_has_init(void)   { return ramfs_has_init(); }
void fs_mount_root(void) { ramfs_mount(); }

void vesa_test(void) {
    if (!vesa_is_ready()) {
        printk(T,"[VESA] Not initialized, cannot test\n");
        return;
    }
    
    // Draw diagonal line
    vesa_draw_line(0, 0, 1023, 767, VESA_BLUE);
    
    // Draw some colored boxes
    vesa_fill_rect(100, 100, 200, 200, VESA_RED);
    vesa_fill_rect(400, 200, 200, 200, VESA_GREEN);
    vesa_fill_rect(700, 100, 200, 200, VESA_CYAN);
}

//new audio device stuff
void kernel_aud_init(){
    audio_scan_result_t scan;

    audio_scanner_run(&scan);
    if (scan.count == 0) {
        printk(T, "No audio devices found on PCI bus.\n");
        return;
    }
}