/**
 * Core kernel boot flow for Ark.
 * Linux-style boot log timestamps: [    X.XXXXXX]
 * DO this objcopy -O elf32-i386 -B i386 -I binary font.psf ../font.o {in the font folder}
 */
#include "hw/vendor.h"
#include "ark/types.h"
#include "ark/printk.h"
#include "ark/panic.h"
#include "ark/clear.h"
#include "ark/usb.h"
#include "ark/ip.h"
#include "ark/fb.h"
#include "ark/ramfs.h"
#include "ark/ata.h"
#include "ark/sata.h"
#include "ark/elf_loader.h"
#include "ark/userspacebuf.h"
#include "ark/script.h"
#include "ark/time.h"
#include "ark/pci.h"
#include "ark/init_api.h"
#include "../mp/built-in.h"
#include "ark/uid.h"
#include "aud/ac97.h"
#include "hw/ramcheck.h"
#include "hw/pager.h"
#include "clr.h"
#include "pci/eth-dev.h"
#include "gpu/vesa.h"
#include "hw/vendor.h"
#include "aud/aud-dev.h"
#include "ark/sd-dev.h"

extern void ac97_test();
extern void ac97_init();
extern void show_sysinfo_bios(void);
extern void clear_screen(void);
extern void usb_init(void);
extern void ip_init(void);
extern void ip_poll(void);
extern void fs_built_in_init(void);
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
    // Initialize serial FIRST (before any graphics setup)
    serial_init();
    
    // Try to initialize VESA graphics
    //vesa_init_default();
    
    if (vesa_is_ready()) {
        // Graphics mode is active — disable VGA text writes
        extern void printk_set_graphics_mode(bool enabled);
        printk_set_graphics_mode(true);
        
        // Clear to black and draw test pattern
        vesa_clear_screen(0x000000);
        vesa_test_pattern();
        
        // All printk output now goes to serial only
        printk(T,"Ark kernel starting on x86 (GRAPHICS MODE)\n");
        printk(T,"kernel: %s  build: %s\n", K_ID, BUILD);
    } else {
        // Fallback to VGA text mode
        //if (font_init == 0)
          //  vga_load_font();
        clear_screen();
        //font_init();
        
        printk(T,"Ark kernel starting on x86 (TEXT MODE)\n");
        printk(T,"kernel: %s  build: %s\n", K_ID, BUILD);
        printk(T,"CPU: ");
        cpu_name();
    }

    cpu_verify();
    mem_verify();
    
    printk(T,"Paging memory\n");
    //init_paging();
    delay(20000000);
    
    /* Framebuffer (already initialized at top) */
    fb_init(&g_fb_info);
    printk(T,"Framebuffer: initialized\n");
    delay(5000000);

    serial_init();
    printk(T,"Serial: console on COM1\n");
    delay(5000000);

    /* IDT */
    idt_init();
    printk(T,"IDT: initialized (int 0x80 syscall gate ready)\n");
    delay(5000000);

    /* PCI scan */
    printk(T,"PCI: scanning bus\n");
    scanAll();
    delay(10000000);

    /* Audio */
    kernel_aud_init();
    delay(10000000);

    /* BIOS info */
    printk(T,"BIOS: reading system info\n");
    show_sysinfo_bios();
    delay(10000000);

    /* RTC time */
    rtc_time_t t = read_rtc();
    printk(T,"rtc: %02d:%02d:%02d UTC\n", t.hour, t.min, t.sec);
    delay(5000000);

    /* ── USB — must come before input so kbd driver sees live HC ── */
    printk(T,"USB: initializing host controllers\n");
    usb_init();
    delay(20000000);
    scan_usb_controllers();
    printk(T,"USB: subsystem ready\n");
    delay(10000000);

    /* ── Input — after USB ─────────────────────────────────────── */
    printk(T,"input: initializing PS/2 + USB HID\n");
    input_init();
    printk(T,"input: ready\n");
    delay(10000000);

    /* ── Network ───────────────────────────────────────────────── */
    printk(T,"eth: scanning for ethernet controllers\n");
    print_eth_devices();
    delay(10000000);

    printk(T,"ip: initializing network stack\n");
    ip_init();
    printk(T,"ip: ready\n");
    delay(10000000);

    /* ── Storage ───────────────────────────────────────────────── */
    printk(T,"ata/sata: initializing storage drivers\n");
    fs_built_in_init();
    printk(T,"ata/sata: ready\n");
    delay(10000000);

    /* ── Ramfs ─────────────────────────────────────────────────── */
    printk(T,"ramfs: mounting root filesystem\n");
    fs_mount_root();
    printk(T,"ramfs: mounted at /\n");
    delay(5000000);

    extern void ramfs_list_files(void);
    ramfs_list_files();
    delay(10000000);
    /* ── Script init ────────────────────────────────────────────── */
    printk(T,"init: scanning ramfs for #!init scripts\n");
    u8 script_found = script_scan_and_execute();
    delay(10000000);
    //scan_and_display_disks(); 
    if (script_found) {
        printk(T,"init: script executed successfully\n");
        goto script_done;
    }
    printk(T,"init: no scripts found, falling back to /init binary\n");

    /* ── Binary init ────────────────────────────────────────────── */
    printk(T,"init: probing ramfs for /init\n");
    wait_for_init_bin();

    if (!fs_has_init()) {
        printk(T,"init: no init binary found — kernel idle\n");
    } else {
        printk(T,"init: /init found in ramfs\n");
    }

    if (fs_has_init()) {
        u32 init_size = 0;
        u8 *init_data = ramfs_get_init(&init_size);

        if (init_data && init_size > 0) {
            printk(T,"init: executing /init (%u bytes)\n", init_size);
            printk(T,"\n");

            g_uspace_buffer.read_pos      = 0;
            g_uspace_buffer.write_pos     = 0;
            g_uspace_buffer.activity_flag = 0;

            int exit_code = elf_execute(init_data, init_size, ark_kernel_api());

            if (g_uspace_buffer.activity_flag || g_uspace_buffer.write_pos > 0) {
                print_userspace_output();
            }

            printk(T,"init: /init exited with code %d\n", exit_code);
            delay(10000000);
        } else {
            printk(T,"init: ERROR — init data invalid\n");
        }
    }

script_done:
    if (!script_found) {
        printk(T,"init: no usable init found\n");
        if (!fs_has_init()) {
            printk(T,"init: hint — use: make run-with-init\n");
        }
        printk(T,"Kernel panic — not syncing: No init found\n");
        delay(20000000);
        kernel_panic("No init found");
    } else {
        printk(T,"init: complete\n");
        printk(T,"kernel: entering idle\n");
        delay(20000000);
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