/*
 * gen/init.c — Ark kernel main entry point
 *
 * Boot sequence:
 *   1. Banner + platform init (serial, IDT, PCI, SMBIOS, PIT)
 *   2. Subsystem init (audio, USB, input, fb, network, storage)
 *   3. Mount ramfs — multiboot module (-initrd <elf>) is already loaded
 *      as /init by arch entry before kernel_main() is called
 *   4. Run /init: ELF detected → execute directly
 *
 * Usage:
 *   qemu ... -kernel ArkImage -initrd myapp
 *
 * The -initrd ELF is passed as multiboot module 0, added to ramfs as
 * /init by modules_load_from_multiboot() in arch/*built-in.c.
 */

#include <hw/vendor.h>
#include <ark/types.h>
#include <ark/printk.h>
#include <ark/panic.h>
#include <ark/clear.h>
#include <ark/usb.h>
#include <ark/ip.h>
#include <ark/fb.h>
#include <ark/display.h>
#include <ark/ramfs.h>
#include <ark/ata.h>
#include <ark/sata.h>
#include <ark/elf_loader.h>
#include <ark/script.h>
#include <ark/time.h>
#include <ark/pci.h>
#include <ark/init_api.h>
#include <ark/uid.h>
#include <aud/ac97.h>
#include <hw/ramcheck.h>
#include "clr.h"
#include <pci/eth-dev.h>
#include <gpu/vesa.h>
#include <aud/aud-dev.h>
#include <ark/sd-dev.h>
#include <hw/smbios.h>
#include <gpu/efi_gop.h>
#include "../arch/x86/sysinfo.h"
#include <ark/kconfig.h>
#include <ark/kuuid.h>
#include <ark/arch.h>

extern void ark_fpu_init(void);
extern void show_sysinfo_bios(void);
extern void usb_init(void);
extern void ip_init(void);
extern void ip_poll(void);
extern void dhcp_request(void);
extern int  dhcp_poll(void);
extern void fs_built_in_init(void);
extern int  disk_load_init(void);
extern int serial_init(void);
extern void idt_init(void);
extern void sched_init(void);
extern void ramfs_init(void);
extern void ramfs_mount(void);
extern u8   ramfs_has_init(void);
extern u8  *ramfs_get_init(u32 *out_size);
extern void dkm_init_kernel(void);
extern void smbios_dump(void);
extern void panic_set_boot_phase(const char *phase);

extern ark_fb_info_t g_fb_info;

u8   fs_has_init(void)   { return ramfs_has_init(); }
void fs_mount_root(void) { ramfs_mount(); }

void kernel_aud_init(void) {
#if CONFIG_AUDIO_ENABLE
    audio_scan_result_t scan;
    audio_scanner_run(&scan);
    if (scan.count == 0)
        printk(T, "audio: no PCI audio devices found\n");
#else
    printk(T, "audio: disabled\n");
#endif
}

void id_ldm(void) {
    char id[22];
    id_init();
    create_unique_id(id);
    printk(T, "LDM ID: %s\n", id);
}

static int run_elf(u8 *data, u32 size) {
    return elf_execute(data, size, ark_kernel_api());
}

void kernel_main(void) {

    panic_set_boot_phase("boot");
    kid_main();
    printk(T, "display: %ux%u\n", vesa_get_width(), vesa_get_height());
    panic_set_boot_phase("display");

    /* Wire display.c to the framebuffer so display_putc / display_puts
     * and the shadow buffer are live for the TTY, shell, and WM layers. */
    display_init(&g_fb_info);
    printk(T, "dbg: display_init ok (cols=%u rows=%u)\n", 
           vesa_get_width() / 8, vesa_get_height() / 8);

    ark_fpu_init();
    printk(T, "dbg: fpu_init ok\n");
    panic_set_boot_phase("fpu");

    { extern void cpu_verify(void); cpu_verify(); }
    printk(T, "dbg: cpu_verify ok\n");

    id_ldm();
    printk(T, "dbg: id_ldm ok\n");
    panic_set_boot_phase("id");

    serial_init();
    printk(T, "serial: COM1 ready\n");

    idt_init();
    printk(T, "idt: ready\n");
    panic_set_boot_phase("idt");

    /* Early init file check - fail fast before expensive PCI/USB scans */
    {
        u32 init_size = 0;
        u8 *init_data = ramfs_get_init(&init_size);
        if (!init_data || init_size == 0) {
            panic_set_boot_phase("no-init");
            kernel_panic("no /init found — pass your ELF via: qemu -initrd <elf>");
        }
    }
    panic_set_boot_phase("init-check");

#if CONFIG_SCHED_ENABLE
    sched_init();
#endif

    printk(T, "PCI: scanning\n");
    scanAll();
    panic_set_boot_phase("pci");

    if (smbios_init()) { printk(T, "SMBIOS: OK\n"); smbios_dump(); }
    else               { printk(T, "SMBIOS: not found\n"); }
    panic_set_boot_phase("smbios");

#if CONFIG_AUDIO_ENABLE
    kernel_aud_init();
#endif
    panic_set_boot_phase("audio");

    show_sysinfo_bios();
    rtc_time_t t = read_rtc();
    printk(T, "rtc: %02d:%02d:%02d UTC\n", t.hour, t.min, t.sec);
    panic_set_boot_phase("rtc");

#if CONFIG_USB_ENABLE
    printk(T, "USB: init\n");
    usb_init();
    scan_usb_controllers();
    printk(T, "USB: ready\n");
    panic_set_boot_phase("usb");
#endif

    input_init();
    panic_set_boot_phase("input");

    {
        extern void ark_syscall_fb_map(void);
        extern void ps2_mouse_kernel_init(void);
        ark_syscall_fb_map();
        ps2_mouse_kernel_init();
    }
    panic_set_boot_phase("input2");

#if CONFIG_NET_ENABLE
    print_eth_devices();
    ip_init();
    dhcp_request();
    for (int i = 0; i < 500 && !dhcp_poll(); i++) ip_poll();
    if (dhcp_poll())
        printk(T, "net: DHCP bound — IP=%s\n", ip_print(g_net_config.local_ip));
    else
        printk(T, "net: DHCP timeout\n");
    panic_set_boot_phase("net");
#endif

    printk(T, "fs: init\n");
    fs_built_in_init();
    panic_set_boot_phase("fs");

    dkm_init_kernel();
    panic_set_boot_phase("dkm");

#if CONFIG_SCHED_ENABLE
    {
        extern void driver_affinity_init(void);
        extern void driver_affinity_wait_storage(void);
        driver_affinity_init();
        driver_affinity_wait_storage();
    }
#else
#if CONFIG_ATA_ENABLE
    { extern void ata_init(void); ata_init(); }
#endif
#if CONFIG_SATA_ENABLE
    { extern void sata_init(void); sata_init(); }
#endif
#endif

    /* Mount ramfs — /init was already added by arch entry from -initrd module */
    printk(T, "initramfs: mounting\n");
    fs_mount_root();

    {
        extern void ramfs_list_files(void);
        u32 fc = ramfs_get_file_count();
        printk(T, "initramfs: %u file(s)\n", fc);
        ramfs_list_files();
    }

    /* ── Find and execute /init or /.init ──────────────────────────────── */
    u32 init_size = 0;
    u8 *init_data = ramfs_get_init(&init_size);

    /* If /init not found in ramfs, it was already probed by driver_affinity.
     * Re-check in case afs_disk_probe loaded it during storage init. */
    if (!init_data || init_size == 0) {
        init_data = ramfs_get_init(&init_size);
    }

    /* If /init not found, fall back to scanning ramfs for #!init scripts */
    if (!init_data || init_size == 0) {
        printk(T, "init: no /init found — scanning for #!init script\n");
        if (script_scan_and_execute())
            kernel_panic("init script complete");
        kernel_panic("no /init found — pass your ELF via: qemu -initrd <elf>");
    }

    printk(T, "init: /init found (%u bytes)\n", init_size);

    /* ELF binary */
    if (init_size >= 4 &&
        init_data[0] == 0x7F && init_data[1] == 'E' &&
        init_data[2] == 'L'  && init_data[3] == 'F') {
        printk(T, "init: ELF — executing\n");
        int ec = run_elf(init_data, init_size);
        printk(T, "init: exited %d\n", ec);
        kernel_panic("init returned");
    }

    /* #!init script */
    if (init_size >= 6 &&
        init_data[0] == '#' && init_data[1] == '!' &&
        init_data[2] == 'i' && init_data[3] == 'n' &&
        init_data[4] == 'i' && init_data[5] == 't') {
        printk(T, "init: #!init script — running\n");
        script_scan_and_execute();
        kernel_panic("init script complete — no ELF returned");
    }

    kernel_panic("init: unknown format");
}
