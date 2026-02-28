/**
 * kconfig/menuconfig.c — Full Linux-style menuconfig for Ark kernel
 *
 * Features:
 *   - ncurses + lxdialog-style look (blue/grey theme, checkboxes, radio,
 *     submenus, string entry, numeric entry)
 *   - Multi-level nested menus
 *   - Scrollable item lists
 *   - Help text per item
 *   - defconfig / tinyconfig / allyes / allno presets
 *   - Saves .kconfig key=value file
 *
 * Build:  gcc -O2 -o kconfig/menuconfig kconfig/menuconfig.c -lncurses
 */

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* ====================================================================
 * Theme / colour pairs (Linux menuconfig classic blue)
 * ==================================================================== */
#define CLR_TITLE    1   /* white on blue  */
#define CLR_MENU     2   /* white on blue  */
#define CLR_SEL      3   /* black on cyan  */
#define CLR_BORDER   4   /* cyan on blue   */
#define CLR_BUTTON   5   /* black on grey  */
#define CLR_BTNSEL   6   /* white on blue  */
#define CLR_HELP     7   /* white on black */
#define CLR_ARROW    8   /* cyan on blue   */
#define CLR_CHECK    9   /* green on blue  */
#define CLR_UNCHECK 10   /* red on blue    */

static void init_colours(void) {
    if (!has_colors()) return;
    start_color();
    use_default_colors();
    /* Standard Linux menuconfig palette */
    init_pair(CLR_TITLE,   COLOR_WHITE,  COLOR_BLUE);
    init_pair(CLR_MENU,    COLOR_WHITE,  COLOR_BLUE);
    init_pair(CLR_SEL,     COLOR_BLACK,  COLOR_CYAN);
    init_pair(CLR_BORDER,  COLOR_CYAN,   COLOR_BLUE);
    init_pair(CLR_BUTTON,  COLOR_BLACK,  COLOR_WHITE);
    init_pair(CLR_BTNSEL,  COLOR_WHITE,  COLOR_BLUE);
    init_pair(CLR_HELP,    COLOR_WHITE,  COLOR_BLACK);
    init_pair(CLR_ARROW,   COLOR_CYAN,   COLOR_BLUE);
    init_pair(CLR_CHECK,   COLOR_GREEN,  COLOR_BLUE);
    init_pair(CLR_UNCHECK, COLOR_RED,    COLOR_BLUE);
}

/* ====================================================================
 * Config item types
 * ==================================================================== */
typedef enum {
    IT_BOOL,       /* [*] / [ ]  toggle */
    IT_TRISTATE,   /* <*> / <M> / < >  not used yet, reserved */
    IT_CHOICE,     /* cycles through string options */
    IT_INT,        /* numeric entry */
    IT_STRING,     /* text entry   */
    IT_SUBMENU,    /* enter sub-menu */
    IT_SEPARATOR,  /* ---- visual divider */
    IT_COMMENT,    /* static text */
    IT_PRESET,     /* apply config preset */
} item_type_t;

#define MAX_CHOICE_OPTS 16
#define KKEY_MAX        64
#define LABEL_MAX       80
#define HELP_MAX        512

typedef struct config_item config_item_t;

struct config_item {
    item_type_t  type;
    const char  *label;       /* display label */
    const char  *kkey;        /* key in .kconfig file (NULL for non-saved) */
    const char  *help;        /* one-liner or multi-line help */
    int          val_bool;    /* current boolean value */
    int          val_int;     /* current integer value */
    char         val_str[256];/* current string value (choice or string) */

    /* IT_CHOICE options */
    const char  *choices[MAX_CHOICE_OPTS];
    int          n_choices;

    /* IT_SUBMENU: pointer to child items array + count */
    config_item_t *sub_items;
    int            sub_count;

    /* visibility condition (evaluated at draw time) */
    int         *depends_on;  /* pointer to a val_bool that must be 1 */
};

/* ====================================================================
 * Global config state
 * ==================================================================== */
typedef struct {
    /* ---- General ---- */
    char   arch[32];
    int    bits;
    char   codename[64];
    int    opt_level;   /* 0,1,2,s */
    int    debug;
    int    werror;

    /* ---- Init / Userspace ---- */
    char   init_bin[256];
    int    build_init;

    /* ---- Printk / Serial ---- */
    int    printk_enable;
    int    serial_enable;
    int    serial_port;   /* 0x3F8 etc. */
    int    loglevel;      /* 0-7 */

    /* ---- Memory ---- */
    int    pmm_enable;
    int    vmm_enable;
    int    heap_size_kb;
    int    stack_size_kb;

    /* ---- Framebuffer ---- */
    int    fb_enable;
    int    fb_width;
    int    fb_height;
    int    fb_bpp;
    char   fb_driver[32]; /* "bga", "vesa", "vga" */

    /* ---- I/O ---- */
    int    pio_enable;    /* x86 port IO */
    int    mmio_enable;   /* MMIO helpers */
    int    ioapic_enable;
    int    pic_enable;

    /* ---- Interrupts ---- */
    int    idt_enable;
    int    irq_stack;     /* dedicated IRQ stack */
    int    nmi_enable;

    /* ---- USB ---- */
    int    usb_enable;
    int    usb_xhci;
    int    usb_ehci;
    int    usb_uhci;
    int    usb_hid;

    /* ---- Storage ---- */
    int    ata_enable;
    int    sata_enable;
    int    sd_enable;
    int    fat32_enable;
    int    ramfs_enable;
    int    vfs_enable;
    int    zip_enable;
    int    ata_dma;

    /* ---- Networking ---- */
    int    net_enable;
    int    e1000_enable;
    int    e100_enable;
    int    ip_enable;
    int    udp_enable;
    int    tcp_enable;   /* stub */

    /* ---- Audio ---- */
    int    audio_enable;
    int    ac97_enable;
    int    hda_enable;

    /* ---- HID ---- */
    int    kbd_enable;
    int    mouse_enable;
    int    touch_enable;

    /* ---- GPU ---- */
    int    gpu_enable;
    int    vesa_enable;

    /* ---- PCI ---- */
    int    pci_enable;
    int    pci_probe_all;

    /* ---- Scheduler ---- */
    int    sched_enable;
    int    sched_preempt;
    int    sched_timeslice_ms;

    /* ---- Syscalls ---- */
    int    syscall_enable;
    int    elf_loader;

    /* ---- Debug ---- */
    int    debug_verbose;
    int    debug_kasan;  /* stub */
    int    debug_panic_dump;

    /* ---- QEMU ---- */
    int    qemu_ram_mb;
    int    qemu_smp;
    int    qemu_net;
    int    qemu_usb;
    int    qemu_nographic;
} ark_cfg_t;

static ark_cfg_t G;

/* ====================================================================
 * Preset definitions
 * ==================================================================== */
static void apply_defconfig(void) {
    memset(&G, 0, sizeof(G));
    strcpy(G.arch,     "x86_64");
    G.bits            = 64;
    strcpy(G.codename, "affectionate cat");
    G.opt_level       = 2;
    G.debug           = 0;
    G.werror          = 0;
    strcpy(G.init_bin, "/init.bin");
    G.build_init      = 0;
    G.printk_enable   = 1;
    G.serial_enable   = 1;
    G.serial_port     = 0x3F8;
    G.loglevel        = 4;
    G.pmm_enable      = 1;
    G.vmm_enable      = 1;
    G.heap_size_kb    = 4096;
    G.stack_size_kb   = 64;
    G.fb_enable       = 1;
    G.fb_width        = 1024;
    G.fb_height       = 768;
    G.fb_bpp          = 32;
    strcpy(G.fb_driver, "bga");
    G.pio_enable      = 1;
    G.mmio_enable     = 1;
    G.ioapic_enable   = 1;
    G.pic_enable      = 1;
    G.idt_enable      = 1;
    G.irq_stack       = 1;
    G.nmi_enable      = 1;
    G.usb_enable      = 1;
    G.usb_xhci        = 1;
    G.usb_ehci        = 1;
    G.usb_uhci        = 0;
    G.usb_hid         = 1;
    G.ata_enable      = 1;
    G.sata_enable     = 1;
    G.sd_enable       = 1;
    G.fat32_enable    = 1;
    G.ramfs_enable    = 1;
    G.vfs_enable      = 1;
    G.zip_enable      = 1;
    G.ata_dma         = 1;
    G.net_enable      = 1;
    G.e1000_enable    = 1;
    G.e100_enable     = 0;
    G.ip_enable       = 1;
    G.udp_enable      = 1;
    G.tcp_enable      = 0;
    G.audio_enable    = 1;
    G.ac97_enable     = 1;
    G.hda_enable      = 0;
    G.kbd_enable      = 1;
    G.mouse_enable    = 1;
    G.touch_enable    = 0;
    G.gpu_enable      = 1;
    G.vesa_enable     = 1;
    G.pci_enable      = 1;
    G.pci_probe_all   = 1;
    G.sched_enable    = 1;
    G.sched_preempt   = 1;
    G.sched_timeslice_ms = 10;
    G.syscall_enable  = 1;
    G.elf_loader      = 1;
    G.debug_verbose   = 0;
    G.debug_kasan     = 0;
    G.debug_panic_dump= 1;
    G.qemu_ram_mb     = 256;
    G.qemu_smp        = 1;
    G.qemu_net        = 1;
    G.qemu_usb        = 1;
    G.qemu_nographic  = 0;
}

static void apply_tinyconfig(void) {
    memset(&G, 0, sizeof(G));
    strcpy(G.arch,     "x86");
    G.bits            = 32;
    strcpy(G.codename, "tiny");
    G.opt_level       = 2; /* -Os would be best but keep 2 for compat */
    G.debug           = 0;
    G.werror          = 0;
    strcpy(G.init_bin, "/init.bin");
    G.build_init      = 0;
    G.printk_enable   = 1;   /* keep — need boot messages */
    G.serial_enable   = 0;
    G.serial_port     = 0x3F8;
    G.loglevel        = 2;
    G.pmm_enable      = 1;
    G.vmm_enable      = 0;
    G.heap_size_kb    = 512;
    G.stack_size_kb   = 16;
    G.fb_enable       = 1;
    G.fb_width        = 640;
    G.fb_height       = 480;
    G.fb_bpp          = 32;
    strcpy(G.fb_driver, "bga");
    G.pio_enable      = 1;
    G.mmio_enable     = 1;
    G.ioapic_enable   = 0;
    G.pic_enable      = 1;
    G.idt_enable      = 1;
    G.irq_stack       = 0;
    G.nmi_enable      = 0;
    G.usb_enable      = 0;
    G.ata_enable      = 1;
    G.sata_enable     = 0;
    G.sd_enable       = 0;
    G.fat32_enable    = 1;
    G.ramfs_enable    = 1;
    G.vfs_enable      = 1;
    G.zip_enable      = 0;
    G.ata_dma         = 0;
    G.net_enable      = 0;
    G.audio_enable    = 0;
    G.kbd_enable      = 1;
    G.mouse_enable    = 0;
    G.touch_enable    = 0;
    G.gpu_enable      = 1;
    G.vesa_enable     = 0;
    G.pci_enable      = 1;
    G.pci_probe_all   = 0;
    G.sched_enable    = 0;
    G.syscall_enable  = 1;
    G.elf_loader      = 0;
    G.debug_verbose   = 0;
    G.debug_panic_dump= 0;
    G.qemu_ram_mb     = 64;
    G.qemu_smp        = 1;
    G.qemu_net        = 0;
    G.qemu_usb        = 0;
    G.qemu_nographic  = 0;
}

static void apply_allyes(void) {
    apply_defconfig();
    /* flip everything on */
    G.debug=1; G.werror=0; G.serial_enable=1; G.vmm_enable=1;
    G.ioapic_enable=1; G.usb_xhci=1; G.usb_ehci=1; G.usb_uhci=1;
    G.usb_hid=1; G.sata_enable=1; G.sd_enable=1; G.zip_enable=1;
    G.ata_dma=1; G.e1000_enable=1; G.e100_enable=1;
    G.ip_enable=1; G.udp_enable=1; G.tcp_enable=1;
    G.ac97_enable=1; G.hda_enable=1;
    G.mouse_enable=1; G.touch_enable=1;
    G.vesa_enable=1; G.pci_probe_all=1;
    G.sched_enable=1; G.sched_preempt=1;
    G.elf_loader=1; G.debug_verbose=1;
    G.debug_kasan=1; G.debug_panic_dump=1;
    G.qemu_ram_mb=512; G.qemu_smp=4;
}

static void apply_allno(void) {
    memset(&G, 0, sizeof(G));
    strcpy(G.arch, "x86");
    G.bits = 32;
    strcpy(G.codename, "allno");
    G.opt_level = 2;
    strcpy(G.init_bin, "/init.bin");
    G.printk_enable = 1;  /* must stay on for sanity */
    G.pio_enable = 1;
    G.idt_enable = 1;
    G.pic_enable = 1;
    G.pmm_enable = 1;
    G.fb_enable  = 1;
    G.fb_width   = 640; G.fb_height = 480; G.fb_bpp = 32;
    strcpy(G.fb_driver, "bga");
    G.pci_enable = 1;
    G.ata_enable = 1;
    G.ramfs_enable = 1;
    G.vfs_enable = 1;
    G.fat32_enable = 1;
    G.kbd_enable = 1;
    G.qemu_ram_mb = 64;
    G.qemu_smp = 1;
    G.qemu_net = 0;
    G.qemu_usb = 0;
}

/* ====================================================================
 * .kconfig load / save
 * ==================================================================== */
#define KCONF_PATH ".kconfig"

static void load_kconfig(void) {
    apply_defconfig(); /* start from defaults */
    FILE *f = fopen(KCONF_PATH, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n'); if (nl) *nl = 0;
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq = 0;
        char *k = line, *v = eq + 1;
        /* trim */
        while (isspace((unsigned char)*k)) k++;
        while (isspace((unsigned char)*v)) v++;
#define LOAD_STR(key, field)  if (!strcmp(k, #key)) { strncpy(G.field, v, sizeof(G.field)-1); continue; }
#define LOAD_INT(key, field)  if (!strcmp(k, #key)) { G.field = atoi(v); continue; }
        LOAD_STR(ARCH,          arch)
        LOAD_INT(BITS,          bits)
        LOAD_STR(CODENAME,      codename)
        LOAD_INT(OPT_LEVEL,     opt_level)
        LOAD_INT(DEBUG,         debug)
        LOAD_INT(WERROR,        werror)
        LOAD_STR(INIT_BIN,      init_bin)
        LOAD_INT(BUILD_INIT,    build_init)
        LOAD_INT(PRINTK_ENABLE, printk_enable)
        LOAD_INT(SERIAL_ENABLE, serial_enable)
        LOAD_INT(SERIAL_PORT,   serial_port)
        LOAD_INT(LOGLEVEL,      loglevel)
        LOAD_INT(PMM_ENABLE,    pmm_enable)
        LOAD_INT(VMM_ENABLE,    vmm_enable)
        LOAD_INT(HEAP_SIZE_KB,  heap_size_kb)
        LOAD_INT(STACK_SIZE_KB, stack_size_kb)
        LOAD_INT(FB_ENABLE,     fb_enable)
        LOAD_INT(FB_WIDTH,      fb_width)
        LOAD_INT(FB_HEIGHT,     fb_height)
        LOAD_INT(FB_BPP,        fb_bpp)
        LOAD_STR(FB_DRIVER,     fb_driver)
        LOAD_INT(PIO_ENABLE,    pio_enable)
        LOAD_INT(MMIO_ENABLE,   mmio_enable)
        LOAD_INT(IOAPIC_ENABLE, ioapic_enable)
        LOAD_INT(PIC_ENABLE,    pic_enable)
        LOAD_INT(IDT_ENABLE,    idt_enable)
        LOAD_INT(IRQ_STACK,     irq_stack)
        LOAD_INT(NMI_ENABLE,    nmi_enable)
        LOAD_INT(USB_ENABLE,    usb_enable)
        LOAD_INT(USB_XHCI,      usb_xhci)
        LOAD_INT(USB_EHCI,      usb_ehci)
        LOAD_INT(USB_UHCI,      usb_uhci)
        LOAD_INT(USB_HID,       usb_hid)
        LOAD_INT(ATA_ENABLE,    ata_enable)
        LOAD_INT(SATA_ENABLE,   sata_enable)
        LOAD_INT(SD_ENABLE,     sd_enable)
        LOAD_INT(FAT32_ENABLE,  fat32_enable)
        LOAD_INT(RAMFS_ENABLE,  ramfs_enable)
        LOAD_INT(VFS_ENABLE,    vfs_enable)
        LOAD_INT(ZIP_ENABLE,    zip_enable)
        LOAD_INT(ATA_DMA,       ata_dma)
        LOAD_INT(NET_ENABLE,    net_enable)
        LOAD_INT(E1000_ENABLE,  e1000_enable)
        LOAD_INT(E100_ENABLE,   e100_enable)
        LOAD_INT(IP_ENABLE,     ip_enable)
        LOAD_INT(UDP_ENABLE,    udp_enable)
        LOAD_INT(TCP_ENABLE,    tcp_enable)
        LOAD_INT(AUDIO_ENABLE,  audio_enable)
        LOAD_INT(AC97_ENABLE,   ac97_enable)
        LOAD_INT(HDA_ENABLE,    hda_enable)
        LOAD_INT(KBD_ENABLE,    kbd_enable)
        LOAD_INT(MOUSE_ENABLE,  mouse_enable)
        LOAD_INT(TOUCH_ENABLE,  touch_enable)
        LOAD_INT(GPU_ENABLE,    gpu_enable)
        LOAD_INT(VESA_ENABLE,   vesa_enable)
        LOAD_INT(PCI_ENABLE,    pci_enable)
        LOAD_INT(PCI_PROBE_ALL, pci_probe_all)
        LOAD_INT(SCHED_ENABLE,  sched_enable)
        LOAD_INT(SCHED_PREEMPT, sched_preempt)
        LOAD_INT(SCHED_TIMESLICE_MS, sched_timeslice_ms)
        LOAD_INT(SYSCALL_ENABLE,syscall_enable)
        LOAD_INT(ELF_LOADER,    elf_loader)
        LOAD_INT(DEBUG_VERBOSE, debug_verbose)
        LOAD_INT(DEBUG_KASAN,   debug_kasan)
        LOAD_INT(DEBUG_PANIC_DUMP, debug_panic_dump)
        LOAD_INT(QEMU_RAM_MB,   qemu_ram_mb)
        LOAD_INT(QEMU_SMP,      qemu_smp)
        LOAD_INT(QEMU_NET,      qemu_net)
        LOAD_INT(QEMU_USB,      qemu_usb)
        LOAD_INT(QEMU_NOGRAPHIC,qemu_nographic)
#undef LOAD_STR
#undef LOAD_INT
    }
    fclose(f);
}

static void save_kconfig(void) {
    FILE *f = fopen(KCONF_PATH, "w");
    if (!f) return;
    fprintf(f, "# Ark kernel kernel configuration\n");
    fprintf(f, "# Generated by Ark menuconfig\n\n");

    fprintf(f, "# Architecture\n");
    fprintf(f, "ARCH=%s\n",       G.arch);
    fprintf(f, "BITS=%d\n",       G.bits);
    fprintf(f, "CODENAME=%s\n",   G.codename);

    fprintf(f, "\n# Build\n");
    fprintf(f, "OPT_LEVEL=%d\n",  G.opt_level);
    fprintf(f, "DEBUG=%d\n",      G.debug);
    fprintf(f, "WERROR=%d\n",     G.werror);

    fprintf(f, "\n# Userspace\n");
    fprintf(f, "INIT_BIN=%s\n",   G.init_bin);
    fprintf(f, "BUILD_INIT=%d\n", G.build_init);

    fprintf(f, "\n# Printk / Serial\n");
    fprintf(f, "PRINTK_ENABLE=%d\n", G.printk_enable);
    fprintf(f, "SERIAL_ENABLE=%d\n", G.serial_enable);
    fprintf(f, "SERIAL_PORT=%d\n",   G.serial_port);
    fprintf(f, "LOGLEVEL=%d\n",      G.loglevel);

    fprintf(f, "\n# Memory\n");
    fprintf(f, "PMM_ENABLE=%d\n",    G.pmm_enable);
    fprintf(f, "VMM_ENABLE=%d\n",    G.vmm_enable);
    fprintf(f, "HEAP_SIZE_KB=%d\n",  G.heap_size_kb);
    fprintf(f, "STACK_SIZE_KB=%d\n", G.stack_size_kb);

    fprintf(f, "\n# Framebuffer\n");
    fprintf(f, "FB_ENABLE=%d\n",     G.fb_enable);
    fprintf(f, "FB_WIDTH=%d\n",      G.fb_width);
    fprintf(f, "FB_HEIGHT=%d\n",     G.fb_height);
    fprintf(f, "FB_BPP=%d\n",        G.fb_bpp);
    fprintf(f, "FB_DRIVER=%s\n",     G.fb_driver);

    fprintf(f, "\n# I/O\n");
    fprintf(f, "PIO_ENABLE=%d\n",    G.pio_enable);
    fprintf(f, "MMIO_ENABLE=%d\n",   G.mmio_enable);
    fprintf(f, "IOAPIC_ENABLE=%d\n", G.ioapic_enable);
    fprintf(f, "PIC_ENABLE=%d\n",    G.pic_enable);

    fprintf(f, "\n# Interrupts\n");
    fprintf(f, "IDT_ENABLE=%d\n",    G.idt_enable);
    fprintf(f, "IRQ_STACK=%d\n",     G.irq_stack);
    fprintf(f, "NMI_ENABLE=%d\n",    G.nmi_enable);

    fprintf(f, "\n# USB\n");
    fprintf(f, "USB_ENABLE=%d\n",    G.usb_enable);
    fprintf(f, "USB_XHCI=%d\n",      G.usb_xhci);
    fprintf(f, "USB_EHCI=%d\n",      G.usb_ehci);
    fprintf(f, "USB_UHCI=%d\n",      G.usb_uhci);
    fprintf(f, "USB_HID=%d\n",       G.usb_hid);

    fprintf(f, "\n# Storage\n");
    fprintf(f, "ATA_ENABLE=%d\n",    G.ata_enable);
    fprintf(f, "SATA_ENABLE=%d\n",   G.sata_enable);
    fprintf(f, "SD_ENABLE=%d\n",     G.sd_enable);
    fprintf(f, "FAT32_ENABLE=%d\n",  G.fat32_enable);
    fprintf(f, "RAMFS_ENABLE=%d\n",  G.ramfs_enable);
    fprintf(f, "VFS_ENABLE=%d\n",    G.vfs_enable);
    fprintf(f, "ZIP_ENABLE=%d\n",    G.zip_enable);
    fprintf(f, "ATA_DMA=%d\n",       G.ata_dma);

    fprintf(f, "\n# Networking\n");
    fprintf(f, "NET_ENABLE=%d\n",    G.net_enable);
    fprintf(f, "E1000_ENABLE=%d\n",  G.e1000_enable);
    fprintf(f, "E100_ENABLE=%d\n",   G.e100_enable);
    fprintf(f, "IP_ENABLE=%d\n",     G.ip_enable);
    fprintf(f, "UDP_ENABLE=%d\n",    G.udp_enable);
    fprintf(f, "TCP_ENABLE=%d\n",    G.tcp_enable);

    fprintf(f, "\n# Audio\n");
    fprintf(f, "AUDIO_ENABLE=%d\n",  G.audio_enable);
    fprintf(f, "AC97_ENABLE=%d\n",   G.ac97_enable);
    fprintf(f, "HDA_ENABLE=%d\n",    G.hda_enable);

    fprintf(f, "\n# HID\n");
    fprintf(f, "KBD_ENABLE=%d\n",    G.kbd_enable);
    fprintf(f, "MOUSE_ENABLE=%d\n",  G.mouse_enable);
    fprintf(f, "TOUCH_ENABLE=%d\n",  G.touch_enable);

    fprintf(f, "\n# GPU\n");
    fprintf(f, "GPU_ENABLE=%d\n",    G.gpu_enable);
    fprintf(f, "VESA_ENABLE=%d\n",   G.vesa_enable);

    fprintf(f, "\n# PCI\n");
    fprintf(f, "PCI_ENABLE=%d\n",    G.pci_enable);
    fprintf(f, "PCI_PROBE_ALL=%d\n", G.pci_probe_all);

    fprintf(f, "\n# Scheduler\n");
    fprintf(f, "SCHED_ENABLE=%d\n",  G.sched_enable);
    fprintf(f, "SCHED_PREEMPT=%d\n", G.sched_preempt);
    fprintf(f, "SCHED_TIMESLICE_MS=%d\n", G.sched_timeslice_ms);

    fprintf(f, "\n# Syscalls\n");
    fprintf(f, "SYSCALL_ENABLE=%d\n",G.syscall_enable);
    fprintf(f, "ELF_LOADER=%d\n",    G.elf_loader);

    fprintf(f, "\n# Debugging\n");
    fprintf(f, "DEBUG_VERBOSE=%d\n", G.debug_verbose);
    fprintf(f, "DEBUG_KASAN=%d\n",   G.debug_kasan);
    fprintf(f, "DEBUG_PANIC_DUMP=%d\n", G.debug_panic_dump);

    fprintf(f, "\n# QEMU defaults\n");
    fprintf(f, "QEMU_RAM_MB=%d\n",   G.qemu_ram_mb);
    fprintf(f, "QEMU_SMP=%d\n",      G.qemu_smp);
    fprintf(f, "QEMU_NET=%d\n",      G.qemu_net);
    fprintf(f, "QEMU_USB=%d\n",      G.qemu_usb);
    fprintf(f, "QEMU_NOGRAPHIC=%d\n",G.qemu_nographic);

    fclose(f);
}

/* ====================================================================
 * Low-level drawing utilities
 * ==================================================================== */
#define MIN(a,b) ((a)<(b)?(a):(b))

/* Draw a box with lxdialog-style double border */
static void draw_box(WINDOW *w, int r, int c, int h, int ww, const char *title) {
    wattron(w, COLOR_PAIR(CLR_BORDER));
    for (int i = r; i < r+h; i++) {
        mvwhline(w, i, c,   ' ', ww);
    }
    mvwhline(w, r,     c, ACS_HLINE, ww);
    mvwhline(w, r+h-1, c, ACS_HLINE, ww);
    mvwvline(w, r, c,        ACS_VLINE, h);
    mvwvline(w, r, c+ww-1,   ACS_VLINE, h);
    mvwaddch(w, r,     c,        ACS_ULCORNER);
    mvwaddch(w, r,     c+ww-1,   ACS_URCORNER);
    mvwaddch(w, r+h-1, c,        ACS_LLCORNER);
    mvwaddch(w, r+h-1, c+ww-1,   ACS_LRCORNER);
    wattroff(w, COLOR_PAIR(CLR_BORDER));

    if (title && title[0]) {
        wattron(w, COLOR_PAIR(CLR_TITLE) | A_BOLD);
        int tlen = (int)strlen(title);
        int tx   = c + (ww - tlen - 4) / 2;
        mvwprintw(w, r, tx, "[ %s ]", title);
        wattroff(w, COLOR_PAIR(CLR_TITLE) | A_BOLD);
    }
}

/* Draw bottom button bar */
static void draw_buttons(WINDOW *w, int row, int cols,
                         const char **labels, int n, int sel) {
    /* clear row */
    mvwhline(w, row, 1, ' ', cols - 2);
    int x = 2;
    for (int i = 0; i < n; i++) {
        const char *lbl = labels[i];
        if (i == sel) {
            wattron(w, COLOR_PAIR(CLR_BTNSEL) | A_REVERSE | A_BOLD);
        } else {
            wattron(w, COLOR_PAIR(CLR_BUTTON));
        }
        mvwprintw(w, row, x, "  %s  ", lbl);
        if (i == sel) {
            wattroff(w, COLOR_PAIR(CLR_BTNSEL) | A_REVERSE | A_BOLD);
        } else {
            wattroff(w, COLOR_PAIR(CLR_BUTTON));
        }
        x += (int)strlen(lbl) + 6;
    }
}

/* ====================================================================
 * Help popup
 * ==================================================================== */
static void show_help(const char *title, const char *text) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int w = MIN(cols - 10, 72);
    int h = 10;
    int y = (rows - h) / 2;
    int x = (cols - w) / 2;
    WINDOW *popup = newwin(h, w, y, x);
    keypad(popup, TRUE);
    draw_box(popup, 0, 0, h, w, title);
    wattron(popup, COLOR_PAIR(CLR_HELP));
    /* word-wrap text */
    int row = 1;
    const char *p = text;
    while (*p && row < h - 3) {
        char line[128];
        int n = 0;
        while (*p && *p != '\n' && n < w - 4) line[n++] = *p++;
        line[n] = 0;
        if (*p == '\n') p++;
        mvwprintw(popup, row++, 2, "%s", line);
    }
    wattroff(popup, COLOR_PAIR(CLR_HELP));
    const char *btns[] = { "OK" };
    draw_buttons(popup, h - 2, w, btns, 1, 0);
    wrefresh(popup);
    wgetch(popup);
    delwin(popup);
    touchwin(stdscr);
    refresh();
}

/* ====================================================================
 * String input dialog
 * ==================================================================== */
static void input_string(const char *prompt, char *buf, int buflen) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int w = MIN(cols - 10, 64);
    int h = 7;
    int y = (rows - h) / 2;
    int x = (cols - w) / 2;
    WINDOW *popup = newwin(h, w, y, x);
    keypad(popup, TRUE);
    draw_box(popup, 0, 0, h, w, prompt);
    wattron(popup, COLOR_PAIR(CLR_MENU));
    mvwprintw(popup, 2, 2, "Value: ");
    wattroff(popup, COLOR_PAIR(CLR_MENU));

    /* show existing value */
    char tmp[256];
    strncpy(tmp, buf, sizeof(tmp) - 1);
    tmp[sizeof(tmp)-1] = 0;

    echo();
    curs_set(1);
    mvwprintw(popup, 3, 2, "%-*s", w - 4, tmp);
    wmove(popup, 3, 2);
    wrefresh(popup);
    char input[256] = {0};
    wgetnstr(popup, input, MIN(buflen - 1, w - 4));
    noecho();
    curs_set(0);

    if (input[0]) strncpy(buf, input, buflen - 1);
    delwin(popup);
    touchwin(stdscr);
    refresh();
}

/* ====================================================================
 * Integer input dialog
 * ==================================================================== */
static void input_int(const char *prompt, int *val) {
    char tmp[32];
    snprintf(tmp, sizeof(tmp), "%d", *val);
    input_string(prompt, tmp, sizeof(tmp));
    *val = atoi(tmp);
}

/* ====================================================================
 * Choice popup — radio-button style
 * ==================================================================== */
static int choice_popup(const char *title, const char **opts, int n, int cur) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int w = MIN(cols - 10, 50);
    int h = n + 6;
    int y = (rows - h) / 2;
    int x = (cols - w) / 2;
    WINDOW *popup = newwin(h, w, y, x);
    keypad(popup, TRUE);

    int sel = cur;
    for (;;) {
        draw_box(popup, 0, 0, h, w, title);
        for (int i = 0; i < n; i++) {
            int row = i + 2;
            if (i == sel) {
                wattron(popup, COLOR_PAIR(CLR_SEL) | A_REVERSE);
            } else {
                wattron(popup, COLOR_PAIR(CLR_MENU));
            }
            mvwprintw(popup, row, 2, "(%c) %-*s",
                      (i == sel) ? '*' : ' ',
                      w - 8, opts[i]);
            if (i == sel)
                wattroff(popup, COLOR_PAIR(CLR_SEL) | A_REVERSE);
            else
                wattroff(popup, COLOR_PAIR(CLR_MENU));
        }
        const char *btns[] = { "Select", "Cancel" };
        draw_buttons(popup, h - 2, w, btns, 2, 0);
        wrefresh(popup);

        int ch = wgetch(popup);
        switch (ch) {
        case KEY_UP:   sel = (sel - 1 + n) % n; break;
        case KEY_DOWN: sel = (sel + 1)     % n; break;
        case '\n': case KEY_RIGHT: delwin(popup); return sel;
        case 'q': case 'Q': case 27: delwin(popup); return cur;
        }
    }
}

/* ====================================================================
 * Confirm dialog
 * ==================================================================== */
static int confirm(const char *msg) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int w = MIN(cols - 10, 56);
    int h = 7;
    int y = (rows - h) / 2;
    int x = (cols - w) / 2;
    WINDOW *popup = newwin(h, w, y, x);
    keypad(popup, TRUE);
    draw_box(popup, 0, 0, h, w, "Confirm");
    wattron(popup, COLOR_PAIR(CLR_MENU));
    mvwprintw(popup, 2, 2, "%-*s", w - 4, msg);
    wattroff(popup, COLOR_PAIR(CLR_MENU));
    const char *btns[] = { "Yes", "No" };
    int sel = 1;
    for (;;) {
        draw_buttons(popup, h - 2, w, btns, 2, sel);
        wrefresh(popup);
        int ch = wgetch(popup);
        switch (ch) {
        case KEY_LEFT:  sel = 0; break;
        case KEY_RIGHT: sel = 1; break;
        case 'y': case 'Y': delwin(popup); return 1;
        case 'n': case 'N': delwin(popup); return 0;
        case '\n': { int r = (sel == 0); delwin(popup); return r; }
        case 27: delwin(popup); return 0;
        }
    }
}

/* ====================================================================
 * Main menu item descriptor (flat + scrollable)
 * ==================================================================== */
typedef struct {
    int type;       /* 0=bool 1=choice 2=int 3=string 4=sep 5=preset 6=submenu */
    const char *label;
    const char *help;
    int   *b;       /* bool field */
    int   *i;       /* int field  */
    char  *s;       /* string field */
    int    slen;
    /* choice */
    const char **ch_opts;
    int    ch_n;
    /* submenu callback */
    void (*sub_fn)(void);
    /* preset callback */
    void (*preset_fn)(void);
} mitem_t;

#define MI_BOOL(lbl, field, hlp)          { 0, lbl, hlp, &G.field, 0, 0, 0, 0, 0, 0, 0 }
#define MI_CHOICE(lbl, field, opts, hlp)  { 1, lbl, hlp, 0, 0, G.field, sizeof(G.field), opts, (int)(sizeof(opts)/sizeof(opts[0])), 0, 0 }
#define MI_INT(lbl, field, hlp)           { 2, lbl, hlp, 0, &G.field, 0, 0, 0, 0, 0, 0 }
#define MI_STR(lbl, field, hlp)           { 3, lbl, hlp, 0, 0, G.field, sizeof(G.field), 0, 0, 0, 0 }
#define MI_SEP()                          { 4, "────────────────────────────────────", "", 0, 0, 0, 0, 0, 0, 0, 0 }
#define MI_SUB(lbl, fn, hlp)              { 6, lbl, hlp, 0, 0, 0, 0, 0, 0, fn, 0 }
#define MI_PRESET(lbl, fn, hlp)           { 5, lbl, hlp, 0, 0, 0, 0, 0, 0, 0, fn }

/* ====================================================================
 * Generic scrollable menu runner
 * ==================================================================== */
static void run_menu(const char *title, mitem_t *items, int n) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    int w = MIN(cols - 4, 76);
    int visible_h = rows - 12;  /* scrollable body height */
    if (visible_h < 4) visible_h = 4;
    int h = visible_h + 8;      /* body + borders + header + buttons */
    int y = (rows - h) / 2;
    int x = (cols - w) / 2;

    WINDOW *win = newwin(h, w, y, x);
    keypad(win, TRUE);

    int top    = 0;   /* first visible item */
    int cur    = 0;   /* selected item */
    int done   = 0;
    int saved  = 0;

    /* Skip to first non-separator */
    while (cur < n && items[cur].type == 4) cur++;

    while (!done) {
        /* ---- Redraw ---- */
        werase(win);
        wbkgd(win, COLOR_PAIR(CLR_MENU));
        draw_box(win, 0, 0, h, w, title);

        /* Navigation hint */
        wattron(win, COLOR_PAIR(CLR_MENU));
        mvwprintw(win, 1, 2,
          "Arrow:navigate  Enter:toggle/enter  Space:toggle  ?:help  S:save  Q:quit");
        wattron(win, COLOR_PAIR(CLR_BORDER));
        mvwhline(win, 2, 1, ACS_HLINE, w - 2);
        wattroff(win, COLOR_PAIR(CLR_BORDER));

        /* Scroll to keep cursor visible */
        if (cur < top) top = cur;
        if (cur >= top + visible_h) top = cur - visible_h + 1;

        for (int i = 0; i < visible_h && (top + i) < n; i++) {
            int idx  = top + i;
            int row  = i + 3;   /* row in window */
            mitem_t *m = &items[idx];

            if (idx == cur) {
                wattron(win, COLOR_PAIR(CLR_SEL) | A_REVERSE);
            } else {
                wattron(win, COLOR_PAIR(CLR_MENU));
            }

            /* Clear the line */
            mvwhline(win, row, 1, ' ', w - 2);
            wmove(win, row, 2);

            switch (m->type) {
            case 0: /* BOOL */
                if (*m->b) {
                    wattron(win, (idx==cur)?0:COLOR_PAIR(CLR_CHECK));
                    wprintw(win, "[*] ");
                    wattroff(win, (idx==cur)?0:COLOR_PAIR(CLR_CHECK));
                } else {
                    wattron(win, (idx==cur)?0:COLOR_PAIR(CLR_UNCHECK));
                    wprintw(win, "[ ] ");
                    wattroff(win, (idx==cur)?0:COLOR_PAIR(CLR_UNCHECK));
                }
                wprintw(win, "%-*s", w - 8, m->label);
                break;
            case 1: /* CHOICE */
                wprintw(win, "(%s) %-*s", m->s ? m->s : "?", w - 12, m->label);
                break;
            case 2: /* INT */
                wprintw(win, "(%d) %-*s", m->i ? *m->i : 0, w - 12, m->label);
                break;
            case 3: /* STRING */
                wprintw(win, "(%s) %-*s", m->s ? m->s : "", w - 12, m->label);
                break;
            case 4: /* SEPARATOR */
                wattroff(win, COLOR_PAIR(CLR_SEL) | A_REVERSE);
                wattron(win, COLOR_PAIR(CLR_BORDER));
                mvwhline(win, row, 1, ACS_HLINE, w - 2);
                mvwprintw(win, row, 2, "  %s  ", m->label);
                wattroff(win, COLOR_PAIR(CLR_BORDER));
                goto next_item;
            case 5: /* PRESET */
                wprintw(win, ">>> %-*s", w - 8, m->label);
                break;
            case 6: /* SUBMENU */
                wprintw(win, "  ---> %-*s", w - 10, m->label);
                break;
            }

            if (idx == cur) {
                wattroff(win, COLOR_PAIR(CLR_SEL) | A_REVERSE);
            } else {
                wattroff(win, COLOR_PAIR(CLR_MENU));
            }
next_item:;
        }

        /* Scroll arrows */
        if (top > 0) {
            wattron(win, COLOR_PAIR(CLR_ARROW) | A_BOLD);
            mvwprintw(win, 3, w - 3, " ^ ");
            wattroff(win, COLOR_PAIR(CLR_ARROW) | A_BOLD);
        }
        if (top + visible_h < n) {
            wattron(win, COLOR_PAIR(CLR_ARROW) | A_BOLD);
            mvwprintw(win, 3 + visible_h - 1, w - 3, " v ");
            wattroff(win, COLOR_PAIR(CLR_ARROW) | A_BOLD);
        }

        /* Bottom buttons */
        const char *btns[] = { "Select", "Save", "Exit" };
        draw_buttons(win, h - 2, w, btns, 3, -1);
        wrefresh(win);

        /* ---- Input ---- */
        int ch = wgetch(win);
        switch (ch) {
        case KEY_UP:
            do { cur = (cur - 1 + n) % n; } while (items[cur].type == 4);
            break;
        case KEY_DOWN:
            do { cur = (cur + 1) % n; } while (items[cur].type == 4);
            break;
        case KEY_PPAGE:
            cur -= visible_h; if (cur < 0) cur = 0;
            while (items[cur].type == 4 && cur < n-1) cur++;
            break;
        case KEY_NPAGE:
            cur += visible_h; if (cur >= n) cur = n - 1;
            while (items[cur].type == 4 && cur > 0) cur--;
            break;
        case ' ':
        case '\n':
        case KEY_RIGHT:
        {
            mitem_t *m = &items[cur];
            switch (m->type) {
            case 0: /* bool: toggle */
                *m->b = !*m->b;
                break;
            case 1: /* choice: popup */
            {
                int idx = 0;
                for (int j = 0; j < m->ch_n; j++) {
                    if (!strcmp(m->ch_opts[j], m->s)) { idx = j; break; }
                }
                int ni = choice_popup(m->label, m->ch_opts, m->ch_n, idx);
                strncpy(m->s, m->ch_opts[ni], m->slen - 1);
            }
                break;
            case 2: /* int */
                input_int(m->label, m->i);
                break;
            case 3: /* string */
                input_string(m->label, m->s, m->slen);
                break;
            case 5: /* preset */
                if (m->preset_fn) {
                    if (confirm("Apply preset? (unsaved changes will be lost)")) {
                        m->preset_fn();
                    }
                }
                break;
            case 6: /* submenu */
                if (m->sub_fn) {
                    delwin(win);
                    m->sub_fn();
                    win = newwin(h, w, y, x);
                    keypad(win, TRUE);
                }
                break;
            }
            break;
        }
        case '?':
        {
            const char *hlp = items[cur].help;
            if (hlp && hlp[0]) show_help(items[cur].label, hlp);
        }
            break;
        case 's':
        case 'S':
            save_kconfig();
            saved = 1;
            show_help("Saved", "Configuration written to .kconfig");
            break;
        case 'q':
        case 'Q':
        case KEY_LEFT:
            if (!saved) {
                if (confirm("Exit without saving?")) {
                    done = 1;
                }
            } else {
                done = 1;
            }
            break;
        case 27: /* ESC */
            done = 1;
            break;
        }
    }

    delwin(win);
    touchwin(stdscr);
    refresh();
}

/* ====================================================================
 * Sub-menu builders
 * ==================================================================== */
static void menu_io(void) {
    mitem_t items[] = {
        MI_BOOL("Port I/O (in/out)",   pio_enable,    "Enable x86 port I/O helpers (inb/outb etc)."),
        MI_BOOL("MMIO helpers",        mmio_enable,   "Enable memory-mapped I/O read/write helpers."),
        MI_SEP(),
        MI_BOOL("8259 PIC",            pic_enable,    "Programmable Interrupt Controller legacy mode."),
        MI_BOOL("I/O APIC",            ioapic_enable, "Advanced PIC for SMP. Requires PIC enabled too."),
        MI_SEP(),
        MI_BOOL("IDT support",         idt_enable,    "Interrupt Descriptor Table. Required for any IRQ."),
        MI_BOOL("Dedicated IRQ stack", irq_stack,     "Allocate a separate kernel stack for IRQ handlers."),
        MI_BOOL("NMI handler",         nmi_enable,    "Register a Non-Maskable Interrupt handler."),
    };
    run_menu("I/O and Interrupts", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_storage(void) {
    mitem_t items[] = {
        MI_BOOL("ATA/IDE driver",   ata_enable,   "Classic ATA (PIO/DMA) drive support."),
        MI_BOOL("ATA DMA mode",     ata_dma,      "Use DMA for ATA transfers (faster)."),
        MI_BOOL("SATA driver",      sata_enable,  "AHCI SATA driver."),
        MI_BOOL("SD card driver",   sd_enable,    "SD/MMC block device driver."),
        MI_SEP(),
        MI_BOOL("FAT32 filesystem", fat32_enable, "FAT32 read/write support."),
        MI_BOOL("RAM filesystem",   ramfs_enable, "In-memory filesystem (initramfs)."),
        MI_BOOL("VFS layer",        vfs_enable,   "Virtual filesystem abstraction layer."),
        MI_BOOL("ZIP initramfs",    zip_enable,   "Decompress ZIP-format initramfs on boot."),
    };
    run_menu("Storage", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_net(void) {
    mitem_t items[] = {
        MI_BOOL("Networking stack", net_enable,   "Master network enable."),
        MI_BOOL("Intel e1000",      e1000_enable, "Intel Gigabit (e1000) NIC driver."),
        MI_BOOL("Intel e100",       e100_enable,  "Intel Fast Ethernet (e100) NIC driver."),
        MI_SEP(),
        MI_BOOL("IP layer",         ip_enable,    "IPv4 packet handling."),
        MI_BOOL("UDP",              udp_enable,   "UDP datagram sockets."),
        MI_BOOL("TCP (stub)",       tcp_enable,   "TCP stub — not yet fully implemented."),
    };
    run_menu("Networking", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_usb(void) {
    mitem_t items[] = {
        MI_BOOL("USB subsystem",     usb_enable, "Master USB enable."),
        MI_BOOL("xHCI (USB3/2)",     usb_xhci,   "xHCI host controller (USB 3.0 + 2.0)."),
        MI_BOOL("EHCI (USB2)",       usb_ehci,   "EHCI host controller (USB 2.0)."),
        MI_BOOL("UHCI (USB1.1)",     usb_uhci,   "UHCI host controller (USB 1.1 legacy)."),
        MI_BOOL("USB HID (kbd/mouse)",usb_hid,   "USB Human Interface Device class driver."),
    };
    run_menu("USB", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_audio(void) {
    mitem_t items[] = {
        MI_BOOL("Audio subsystem",  audio_enable, "Master audio enable."),
        MI_BOOL("AC97 codec",       ac97_enable,  "Intel AC97 audio codec driver."),
        MI_BOOL("HDA (Intel HD)",   hda_enable,   "Intel High Definition Audio driver (stub)."),
    };
    run_menu("Audio", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_hid(void) {
    mitem_t items[] = {
        MI_BOOL("PS/2 keyboard",    kbd_enable,   "PS/2 / AT keyboard driver."),
        MI_BOOL("PS/2 mouse",       mouse_enable, "PS/2 mouse driver."),
        MI_BOOL("Touchscreen",      touch_enable, "Generic touchscreen input driver."),
    };
    run_menu("HID / Input", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_fb(void) {
    static const char *bpp_opts[]   = { "8", "16", "24", "32" };
    static const char *drv_opts[]   = { "bga", "vesa", "vga" };
    mitem_t items[] = {
        MI_BOOL("Framebuffer enable", fb_enable,  "Enable framebuffer console/graphics."),
        MI_INT( "Width (pixels)",     fb_width,   "Horizontal resolution."),
        MI_INT( "Height (pixels)",    fb_height,  "Vertical resolution."),
        MI_CHOICE("Bits per pixel",   fb_driver,  bpp_opts, "Colour depth."),
        MI_CHOICE("FB driver",        fb_driver,  drv_opts, "Framebuffer backend: bga=Bochs VBE, vesa, vga."),
        MI_SEP(),
        MI_BOOL("GPU/VESA layer",     vesa_enable,"Enable VESA graphics layer above FB."),
        MI_BOOL("GPU subsystem",      gpu_enable, "GPU driver subsystem."),
    };
    /* Fix: bpp is an int not string — handle separately in bool field as workaround.
     * For simplicity we keep fb_bpp as a plain MI_INT. */
    (void)bpp_opts; /* suppress unused */
    /* Re-define bpp item as INT */
    mitem_t items2[] = {
        MI_BOOL("Framebuffer enable", fb_enable,  "Enable framebuffer console/graphics."),
        MI_INT( "Width (pixels)",     fb_width,   "Horizontal resolution."),
        MI_INT( "Height (pixels)",    fb_height,  "Vertical resolution."),
        MI_INT( "Bits per pixel",     fb_bpp,     "Colour depth: 16 or 32."),
        MI_CHOICE("FB driver",        fb_driver,  drv_opts, "bga=Bochs VBE, vesa, vga."),
        MI_SEP(),
        MI_BOOL("GPU/VESA layer",     vesa_enable,"Enable VESA graphics layer above FB."),
        MI_BOOL("GPU subsystem",      gpu_enable, "GPU driver subsystem."),
    };
    (void)items; /* suppress unused */
    run_menu("Framebuffer", items2, (int)(sizeof(items2)/sizeof(items2[0])));
}

static void menu_sched(void) {
    mitem_t items[] = {
        MI_BOOL("Scheduler",          sched_enable,       "Enable kernel task scheduler."),
        MI_BOOL("Preemptive mode",    sched_preempt,      "Allow preemptive context switches."),
        MI_INT( "Timeslice (ms)",     sched_timeslice_ms, "Scheduler tick timeslice in milliseconds."),
    };
    run_menu("Scheduler", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_debug(void) {
    mitem_t items[] = {
        MI_BOOL("Verbose debug logs", debug_verbose,   "Print extra debug messages at boot."),
        MI_BOOL("Kernel panic dump",  debug_panic_dump, "Print register dump on kernel panic."),
        MI_BOOL("KASAN (stub)",       debug_kasan,      "Kernel address sanitiser — stub."),
        MI_INT( "Log level (0-7)",    loglevel,         "Verbosity: 0=quiet 4=info 7=debug."),
    };
    run_menu("Debugging", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_build(void) {
    static const char *opt_opts[] = { "0", "1", "2", "s" };
    static const char *opt_str;
    /* map int → string for display */
    switch (G.opt_level) {
        case 0: opt_str = "0"; break;
        case 1: opt_str = "1"; break;
        case 's': opt_str = "s"; break;
        default: opt_str = "2"; break;
    }
    (void)opt_opts; (void)opt_str;
    mitem_t items[] = {
        MI_INT( "Optimisation level", opt_level,  "-O level passed to GCC (0,1,2)."),
        MI_BOOL("Debug build",        debug,       "Add -g and -O0 regardless of opt_level."),
        MI_BOOL("Werror",             werror,      "Treat warnings as errors."),
        MI_SEP(),
        MI_BOOL("Build userspace init",build_init, "Compile userspace/init.c with ark-gcc."),
        MI_STR( "Init binary path",   init_bin,    "Path inside initramfs to userspace init."),
        MI_SEP(),
        MI_BOOL("Serial console",     serial_enable,"Enable serial port for console output."),
        MI_INT( "Serial port",        serial_port, "Base I/O port: 0x3F8=COM1 0x2F8=COM2."),
    };
    run_menu("Build & Boot", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_qemu(void) {
    mitem_t items[] = {
        MI_INT( "RAM (MB)",    qemu_ram_mb,   "Memory to pass to QEMU with -m."),
        MI_INT( "SMP cores",   qemu_smp,      "Number of vCPUs (-smp)."),
        MI_BOOL("e1000 NIC",   qemu_net,      "Add -device e1000 to QEMU."),
        MI_BOOL("USB device",  qemu_usb,      "Add -usb -device usb-mouse to QEMU."),
        MI_BOOL("No-graphic",  qemu_nographic,"Pass -nographic to QEMU."),
    };
    run_menu("QEMU Settings", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_memory(void) {
    mitem_t items[] = {
        MI_BOOL("Physical memory manager (PMM)", pmm_enable,   "Enable PMM for frame allocation."),
        MI_BOOL("Virtual memory manager (VMM)",  vmm_enable,   "Enable paging / VMM."),
        MI_INT( "Heap size (KB)",                heap_size_kb, "Kernel heap size."),
        MI_INT( "Stack size (KB)",               stack_size_kb,"Initial kernel stack size."),
    };
    run_menu("Memory", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_pci(void) {
    mitem_t items[] = {
        MI_BOOL("PCI subsystem",    pci_enable,    "Enable PCI bus enumeration."),
        MI_BOOL("Probe all devices",pci_probe_all, "Scan all PCI functions (not just bus 0)."),
    };
    run_menu("PCI", items, (int)(sizeof(items)/sizeof(items[0])));
}

/* ====================================================================
 * Top-level / root menu
 * ==================================================================== */
static const char *arch_opts[] = { "x86", "x86_64" };

static void top_menu(void) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    for (;;) {
        mitem_t items[] = {
            MI_SEP(),
            MI_CHOICE("Architecture",  arch,  arch_opts, "Target ISA: x86 (32-bit) or x86_64 (64-bit)."),
            MI_STR(   "Kernel codename", codename, "Human-readable build codename."),
            MI_SEP(),
            MI_SUB("Build & Boot",        menu_build,   "Compiler flags, init, serial."),
            MI_SUB("Memory",              menu_memory,  "PMM, VMM, heap size."),
            MI_SUB("I/O and Interrupts",  menu_io,      "Port I/O, APIC, IDT."),
            MI_SUB("Framebuffer / GPU",   menu_fb,      "Display and graphics settings."),
            MI_SUB("Storage",             menu_storage, "ATA, SATA, FAT32, VFS."),
            MI_SUB("Networking",          menu_net,     "e1000, IP, UDP, TCP."),
            MI_SUB("USB",                 menu_usb,     "xHCI, EHCI, HID."),
            MI_SUB("Audio",               menu_audio,   "AC97, HDA."),
            MI_SUB("HID / Input",         menu_hid,     "Keyboard, mouse."),
            MI_SUB("PCI",                 menu_pci,     "PCI enumeration."),
            MI_SUB("Scheduler",           menu_sched,   "Preemptive task scheduler."),
            MI_BOOL("Syscall interface",  syscall_enable,"Enable system call interface."),
            MI_BOOL("ELF loader",         elf_loader,    "Load ELF binaries from disk."),
            MI_SUB("Debugging",           menu_debug,   "Verbose logs, KASAN, panic dump."),
            MI_SUB("QEMU settings",       menu_qemu,    "RAM, SMP, devices."),
            MI_SEP(),
            MI_PRESET("--- Apply defconfig  (recommended defaults) ---",
                      apply_defconfig,  "Reset to sane defaults for x86_64."),
            MI_PRESET("--- Apply tinyconfig (minimal footprint)    ---",
                      apply_tinyconfig, "Reset to minimal x86 32-bit config."),
            MI_PRESET("--- Apply allyes    (enable everything)     ---",
                      apply_allyes,     "Turn on every feature."),
            MI_PRESET("--- Apply allno     (bare minimum)          ---",
                      apply_allno,      "Disable everything except basics."),
        };
        (void)rows; (void)cols;

        /* Sync arch bits */
        if (!strcmp(G.arch, "x86_64")) G.bits = 64;
        else                            G.bits = 32;

        run_menu("Ark kernel Kernel Configuration", items,
                 (int)(sizeof(items)/sizeof(items[0])));
        /* run_menu returns → done */
        break;
    }
}

/* ====================================================================
 * Title screen
 * ==================================================================== */
static void draw_title_screen(void) {
    int rows, cols;
    getmaxyx(stdscr, rows, cols);
    bkgd(COLOR_PAIR(CLR_MENU));
    clear();

    const char *title = "Ark kernel Kernel Configuration System";
    const char *sub1  = "Arrow keys navigate the menu.";
    const char *sub2  = "Enter/Space to toggle.  ? for help.  S to save.  Q to exit.";
    const char *sub3  = "Press any key to start...";

    wattron(stdscr, COLOR_PAIR(CLR_TITLE) | A_BOLD);
    mvprintw(rows/2 - 3, (cols - (int)strlen(title)) / 2, "%s", title);
    wattroff(stdscr, COLOR_PAIR(CLR_TITLE) | A_BOLD);

    wattron(stdscr, COLOR_PAIR(CLR_MENU));
    mvprintw(rows/2 - 1, (cols - (int)strlen(sub1)) / 2, "%s", sub1);
    mvprintw(rows/2,     (cols - (int)strlen(sub2)) / 2, "%s", sub2);
    wattroff(stdscr, COLOR_PAIR(CLR_MENU));

    wattron(stdscr, COLOR_PAIR(CLR_ARROW) | A_BOLD);
    mvprintw(rows/2 + 2, (cols - (int)strlen(sub3)) / 2, "%s", sub3);
    wattroff(stdscr, COLOR_PAIR(CLR_ARROW) | A_BOLD);

    refresh();
    getch();
}

/* ====================================================================
 * main
 * ==================================================================== */
int main(int argc, char **argv) {
    /* Quick preset mode: ./menuconfig --defconfig etc. */
    if (argc >= 2) {
        load_kconfig();
        if (!strcmp(argv[1], "--defconfig"))  { apply_defconfig();  save_kconfig(); return 0; }
        if (!strcmp(argv[1], "--tinyconfig")) { apply_tinyconfig(); save_kconfig(); return 0; }
        if (!strcmp(argv[1], "--allyes"))     { apply_allyes();     save_kconfig(); return 0; }
        if (!strcmp(argv[1], "--allno"))      { apply_allno();      save_kconfig(); return 0; }
    }

    load_kconfig();

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    init_colours();

    draw_title_screen();
    top_menu();

    /* Final save prompt */
    if (confirm("Save configuration before exit?")) {
        save_kconfig();
    }

    endwin();
    return 0;
}
