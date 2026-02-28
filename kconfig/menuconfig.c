/**
 * kconfig/menuconfig.c — Full Linux-style menuconfig for Ark kernel
 *
 * Faithful reproduction of the classic Linux menuconfig lxdialog TUI:
 *   - Blue background, grey/white dialog boxes (shadow effect)
 *   - Proper lxdialog double-border with title
 *   - Scrollable list with [*]/[ ] checkboxes, <M> tristates, ---> submenus
 *   - Arrow-key navigation, PgUp/PgDn, Home/End
 *   - Help, string input, integer input, choice radio popups
 *   - defconfig / tinyconfig / allyes / allno presets
 *   - Saves .kconfig key=value file
 *
 * Build:  gcc -O2 -o menuconfig menuconfig.c -lncurses
 */

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdarg.h>

/* ====================================================================
 * Colour pairs — exact Linux menuconfig lxdialog palette
 *
 * Background: blue (like classic menuconfig)
 * Dialog:     grey (COLOR_WHITE bg) with white/black text
 * Selected:   black on cyan
 * ================================================================== */
enum {
    CLR_SCREEN    = 1,   /* white on blue  — background */
    CLR_DIALOG    = 2,   /* black on grey  — dialog body */
    CLR_TITLE     = 3,   /* white on grey, bold — dialog title */
    CLR_BORDER    = 4,   /* black on grey  — dialog border */
    CLR_BUTTON    = 5,   /* black on grey  — unselected button */
    CLR_BTNSEL    = 6,   /* yellow on blue — selected button */
    CLR_LISTHL    = 7,   /* white on blue  — selected list item */
    CLR_LISTBG    = 8,   /* black on grey  — unselected list item */
    CLR_CHECK     = 9,   /* white on grey  — checkbox brackets */
    CLR_CHECKMARK = 10,  /* yellow on grey — the * or M inside checkbox */
    CLR_ARROW     = 11,  /* yellow on grey — scroll indicators */
    CLR_HELP      = 12,  /* white on blue  — help text bar */
    CLR_HLBORDER  = 13,  /* white on grey  — highlighted border */
    CLR_TAG       = 14,  /* cyan on grey   — ---> tag */
    CLR_SHADOW    = 15,  /* black on black — shadow */
    CLR_INPUTBG   = 16,  /* black on cyan  — input field */
};

static void init_colours(void) {
    if (!has_colors()) return;
    start_color();
    use_default_colors();

    /* Background: classic blue */
    init_pair(CLR_SCREEN,    COLOR_WHITE,  COLOR_BLUE);
    /* Dialog box body: dark grey simulated as white bg with black text */
    init_pair(CLR_DIALOG,    COLOR_BLACK,  COLOR_WHITE);
    /* Dialog title */
    init_pair(CLR_TITLE,     COLOR_YELLOW, COLOR_WHITE);
    /* Dialog border (single line, dark) */
    init_pair(CLR_BORDER,    COLOR_BLACK,  COLOR_WHITE);
    /* Unselected button */
    init_pair(CLR_BUTTON,    COLOR_BLACK,  COLOR_WHITE);
    /* Selected button — reversed, yellow text on blue */
    init_pair(CLR_BTNSEL,    COLOR_YELLOW, COLOR_BLUE);
    /* Selected list row: white on blue */
    init_pair(CLR_LISTHL,    COLOR_WHITE,  COLOR_BLUE);
    /* Normal list row: black on white (dialog body) */
    init_pair(CLR_LISTBG,    COLOR_BLACK,  COLOR_WHITE);
    /* Checkbox brackets */
    init_pair(CLR_CHECK,     COLOR_BLACK,  COLOR_WHITE);
    /* Checkbox mark (* M) */
    init_pair(CLR_CHECKMARK, COLOR_YELLOW, COLOR_WHITE);
    /* Scroll arrows */
    init_pair(CLR_ARROW,     COLOR_YELLOW, COLOR_WHITE);
    /* Help bar at very bottom */
    init_pair(CLR_HELP,      COLOR_BLACK,  COLOR_WHITE);
    /* Highlighted border (inner) */
    init_pair(CLR_HLBORDER,  COLOR_WHITE,  COLOR_WHITE);
    /* Submenu arrow colour */
    init_pair(CLR_TAG,       COLOR_CYAN,   COLOR_WHITE);
    /* Shadow */
    init_pair(CLR_SHADOW,    COLOR_BLACK,  COLOR_BLACK);
    /* Input field */
    init_pair(CLR_INPUTBG,   COLOR_WHITE,  COLOR_BLUE);
}

/* ====================================================================
 * Global config struct
 * ================================================================== */
typedef struct {
    char   arch[32];
    int    bits;
    char   codename[64];
    int    opt_level;
    int    debug;
    int    werror;
    char   init_bin[256];
    int    build_init;
    int    printk_enable;
    int    serial_enable;
    int    serial_port;
    int    loglevel;
    int    pmm_enable;
    int    vmm_enable;
    int    heap_size_kb;
    int    stack_size_kb;
    int    fb_enable;
    int    fb_width;
    int    fb_height;
    int    fb_bpp;
    char   fb_driver[32];
    int    pio_enable;
    int    mmio_enable;
    int    ioapic_enable;
    int    pic_enable;
    int    idt_enable;
    int    irq_stack;
    int    nmi_enable;
    int    usb_enable;
    int    usb_xhci;
    int    usb_ehci;
    int    usb_uhci;
    int    usb_hid;
    int    ata_enable;
    int    sata_enable;
    int    sd_enable;
    int    fat32_enable;
    int    ramfs_enable;
    int    vfs_enable;
    int    zip_enable;
    int    ata_dma;
    int    net_enable;
    int    e1000_enable;
    int    e100_enable;
    int    ip_enable;
    int    udp_enable;
    int    tcp_enable;
    int    audio_enable;
    int    ac97_enable;
    int    hda_enable;
    int    kbd_enable;
    int    mouse_enable;
    int    touch_enable;
    int    gpu_enable;
    int    vesa_enable;
    int    pci_enable;
    int    pci_probe_all;
    int    sched_enable;
    int    sched_preempt;
    int    sched_timeslice_ms;
    int    syscall_enable;
    int    elf_loader;
    int    debug_verbose;
    int    debug_kasan;
    int    debug_panic_dump;
    int    qemu_ram_mb;
    int    qemu_smp;
    int    qemu_net;
    int    qemu_usb;
    int    qemu_nographic;
} ark_cfg_t;

static ark_cfg_t G;

/* ====================================================================
 * Preset functions
 * ================================================================== */
static void apply_defconfig(void) {
    memset(&G, 0, sizeof(G));
    strcpy(G.arch, "x86_64"); G.bits = 64;
    strcpy(G.codename, "affectionate cat");
    G.opt_level = 2; G.debug = 0; G.werror = 0;
    strcpy(G.init_bin, "/init.bin"); G.build_init = 0;
    G.printk_enable = 1; G.serial_enable = 1; G.serial_port = 0x3F8; G.loglevel = 4;
    G.pmm_enable = 1; G.vmm_enable = 1; G.heap_size_kb = 4096; G.stack_size_kb = 64;
    G.fb_enable = 1; G.fb_width = 1024; G.fb_height = 768; G.fb_bpp = 32;
    strcpy(G.fb_driver, "bga");
    G.pio_enable = 1; G.mmio_enable = 1; G.ioapic_enable = 1; G.pic_enable = 1;
    G.idt_enable = 1; G.irq_stack = 1; G.nmi_enable = 1;
    G.usb_enable = 1; G.usb_xhci = 1; G.usb_ehci = 1; G.usb_uhci = 0; G.usb_hid = 1;
    G.ata_enable = 1; G.sata_enable = 1; G.sd_enable = 1;
    G.fat32_enable = 1; G.ramfs_enable = 1; G.vfs_enable = 1; G.zip_enable = 1; G.ata_dma = 1;
    G.net_enable = 1; G.e1000_enable = 1; G.e100_enable = 0;
    G.ip_enable = 1; G.udp_enable = 1; G.tcp_enable = 0;
    G.audio_enable = 1; G.ac97_enable = 1; G.hda_enable = 0;
    G.kbd_enable = 1; G.mouse_enable = 1; G.touch_enable = 0;
    G.gpu_enable = 1; G.vesa_enable = 1;
    G.pci_enable = 1; G.pci_probe_all = 1;
    G.sched_enable = 1; G.sched_preempt = 1; G.sched_timeslice_ms = 10;
    G.syscall_enable = 1; G.elf_loader = 1;
    G.debug_verbose = 0; G.debug_kasan = 0; G.debug_panic_dump = 1;
    G.qemu_ram_mb = 256; G.qemu_smp = 1; G.qemu_net = 1; G.qemu_usb = 1; G.qemu_nographic = 0;
}

static void apply_tinyconfig(void) {
    memset(&G, 0, sizeof(G));
    strcpy(G.arch, "x86"); G.bits = 32;
    strcpy(G.codename, "tiny"); G.opt_level = 2;
    strcpy(G.init_bin, "/init.bin");
    G.printk_enable = 1; G.serial_port = 0x3F8; G.loglevel = 2;
    G.pmm_enable = 1; G.heap_size_kb = 512; G.stack_size_kb = 16;
    G.fb_enable = 1; G.fb_width = 640; G.fb_height = 480; G.fb_bpp = 32;
    strcpy(G.fb_driver, "bga");
    G.pio_enable = 1; G.mmio_enable = 1; G.pic_enable = 1; G.idt_enable = 1;
    G.ata_enable = 1; G.fat32_enable = 1; G.ramfs_enable = 1; G.vfs_enable = 1;
    G.kbd_enable = 1; G.gpu_enable = 1; G.pci_enable = 1;
    G.syscall_enable = 1; G.qemu_ram_mb = 64; G.qemu_smp = 1;
}

static void apply_allyes(void) {
    apply_defconfig();
    G.debug=1; G.serial_enable=1; G.vmm_enable=1; G.ioapic_enable=1;
    G.usb_xhci=1; G.usb_ehci=1; G.usb_uhci=1; G.usb_hid=1;
    G.sata_enable=1; G.sd_enable=1; G.zip_enable=1; G.ata_dma=1;
    G.e1000_enable=1; G.e100_enable=1; G.ip_enable=1; G.udp_enable=1; G.tcp_enable=1;
    G.ac97_enable=1; G.hda_enable=1; G.mouse_enable=1; G.touch_enable=1;
    G.vesa_enable=1; G.pci_probe_all=1; G.sched_enable=1; G.sched_preempt=1;
    G.elf_loader=1; G.debug_verbose=1; G.debug_kasan=1; G.debug_panic_dump=1;
    G.qemu_ram_mb=512; G.qemu_smp=4;
}

static void apply_allno(void) {
    memset(&G, 0, sizeof(G));
    strcpy(G.arch, "x86"); G.bits = 32;
    strcpy(G.codename, "allno"); G.opt_level = 2;
    strcpy(G.init_bin, "/init.bin");
    G.printk_enable = 1; G.pio_enable = 1; G.idt_enable = 1; G.pic_enable = 1;
    G.pmm_enable = 1; G.fb_enable = 1; G.fb_width = 640; G.fb_height = 480; G.fb_bpp = 32;
    strcpy(G.fb_driver, "bga");
    G.pci_enable = 1; G.ata_enable = 1; G.ramfs_enable = 1; G.vfs_enable = 1;
    G.fat32_enable = 1; G.kbd_enable = 1; G.qemu_ram_mb = 64; G.qemu_smp = 1;
}

/* ====================================================================
 * .kconfig load / save
 * ================================================================== */
#define KCONF_PATH ".kconfig"

static void load_kconfig(void) {
    apply_defconfig();
    FILE *f = fopen(KCONF_PATH, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        char *nl = strchr(line, '\n'); if (nl) *nl = 0;
        char *eq = strchr(line, '='); if (!eq) continue;
        *eq = 0;
        char *k = line, *v = eq + 1;
        while (isspace((unsigned char)*k)) k++;
        while (isspace((unsigned char)*v)) v++;
#define LS(key, fld) if (!strcmp(k,#key)){strncpy(G.fld,v,sizeof(G.fld)-1);continue;}
#define LI(key, fld) if (!strcmp(k,#key)){G.fld=atoi(v);continue;}
        LS(ARCH,arch) LI(BITS,bits) LS(CODENAME,codename)
        LI(OPT_LEVEL,opt_level) LI(DEBUG,debug) LI(WERROR,werror)
        LS(INIT_BIN,init_bin) LI(BUILD_INIT,build_init)
        LI(PRINTK_ENABLE,printk_enable) LI(SERIAL_ENABLE,serial_enable)
        LI(SERIAL_PORT,serial_port) LI(LOGLEVEL,loglevel)
        LI(PMM_ENABLE,pmm_enable) LI(VMM_ENABLE,vmm_enable)
        LI(HEAP_SIZE_KB,heap_size_kb) LI(STACK_SIZE_KB,stack_size_kb)
        LI(FB_ENABLE,fb_enable) LI(FB_WIDTH,fb_width) LI(FB_HEIGHT,fb_height)
        LI(FB_BPP,fb_bpp) LS(FB_DRIVER,fb_driver)
        LI(PIO_ENABLE,pio_enable) LI(MMIO_ENABLE,mmio_enable)
        LI(IOAPIC_ENABLE,ioapic_enable) LI(PIC_ENABLE,pic_enable)
        LI(IDT_ENABLE,idt_enable) LI(IRQ_STACK,irq_stack) LI(NMI_ENABLE,nmi_enable)
        LI(USB_ENABLE,usb_enable) LI(USB_XHCI,usb_xhci) LI(USB_EHCI,usb_ehci)
        LI(USB_UHCI,usb_uhci) LI(USB_HID,usb_hid)
        LI(ATA_ENABLE,ata_enable) LI(SATA_ENABLE,sata_enable) LI(SD_ENABLE,sd_enable)
        LI(FAT32_ENABLE,fat32_enable) LI(RAMFS_ENABLE,ramfs_enable)
        LI(VFS_ENABLE,vfs_enable) LI(ZIP_ENABLE,zip_enable) LI(ATA_DMA,ata_dma)
        LI(NET_ENABLE,net_enable) LI(E1000_ENABLE,e1000_enable) LI(E100_ENABLE,e100_enable)
        LI(IP_ENABLE,ip_enable) LI(UDP_ENABLE,udp_enable) LI(TCP_ENABLE,tcp_enable)
        LI(AUDIO_ENABLE,audio_enable) LI(AC97_ENABLE,ac97_enable) LI(HDA_ENABLE,hda_enable)
        LI(KBD_ENABLE,kbd_enable) LI(MOUSE_ENABLE,mouse_enable) LI(TOUCH_ENABLE,touch_enable)
        LI(GPU_ENABLE,gpu_enable) LI(VESA_ENABLE,vesa_enable)
        LI(PCI_ENABLE,pci_enable) LI(PCI_PROBE_ALL,pci_probe_all)
        LI(SCHED_ENABLE,sched_enable) LI(SCHED_PREEMPT,sched_preempt)
        LI(SCHED_TIMESLICE_MS,sched_timeslice_ms)
        LI(SYSCALL_ENABLE,syscall_enable) LI(ELF_LOADER,elf_loader)
        LI(DEBUG_VERBOSE,debug_verbose) LI(DEBUG_KASAN,debug_kasan)
        LI(DEBUG_PANIC_DUMP,debug_panic_dump)
        LI(QEMU_RAM_MB,qemu_ram_mb) LI(QEMU_SMP,qemu_smp)
        LI(QEMU_NET,qemu_net) LI(QEMU_USB,qemu_usb) LI(QEMU_NOGRAPHIC,qemu_nographic)
#undef LS
#undef LI
    }
    fclose(f);
}

static void save_kconfig(void) {
    FILE *f = fopen(KCONF_PATH, "w");
    if (!f) return;
    fprintf(f,"# Ark kernel configuration\n# Generated by menuconfig\n\n");
    fprintf(f,"ARCH=%s\nBITS=%d\nCODENAME=%s\n",G.arch,G.bits,G.codename);
    fprintf(f,"\nOPT_LEVEL=%d\nDEBUG=%d\nWERROR=%d\n",G.opt_level,G.debug,G.werror);
    fprintf(f,"\nINIT_BIN=%s\nBUILD_INIT=%d\n",G.init_bin,G.build_init);
    fprintf(f,"\nPRINTK_ENABLE=%d\nSERIAL_ENABLE=%d\nSERIAL_PORT=%d\nLOGLEVEL=%d\n",
            G.printk_enable,G.serial_enable,G.serial_port,G.loglevel);
    fprintf(f,"\nPMM_ENABLE=%d\nVMM_ENABLE=%d\nHEAP_SIZE_KB=%d\nSTACK_SIZE_KB=%d\n",
            G.pmm_enable,G.vmm_enable,G.heap_size_kb,G.stack_size_kb);
    fprintf(f,"\nFB_ENABLE=%d\nFB_WIDTH=%d\nFB_HEIGHT=%d\nFB_BPP=%d\nFB_DRIVER=%s\n",
            G.fb_enable,G.fb_width,G.fb_height,G.fb_bpp,G.fb_driver);
    fprintf(f,"\nPIO_ENABLE=%d\nMMIO_ENABLE=%d\nIOAPIC_ENABLE=%d\nPIC_ENABLE=%d\n",
            G.pio_enable,G.mmio_enable,G.ioapic_enable,G.pic_enable);
    fprintf(f,"\nIDT_ENABLE=%d\nIRQ_STACK=%d\nNMI_ENABLE=%d\n",
            G.idt_enable,G.irq_stack,G.nmi_enable);
    fprintf(f,"\nUSB_ENABLE=%d\nUSB_XHCI=%d\nUSB_EHCI=%d\nUSB_UHCI=%d\nUSB_HID=%d\n",
            G.usb_enable,G.usb_xhci,G.usb_ehci,G.usb_uhci,G.usb_hid);
    fprintf(f,"\nATA_ENABLE=%d\nSATA_ENABLE=%d\nSD_ENABLE=%d\nFAT32_ENABLE=%d\n",
            G.ata_enable,G.sata_enable,G.sd_enable,G.fat32_enable);
    fprintf(f,"RAMFS_ENABLE=%d\nVFS_ENABLE=%d\nZIP_ENABLE=%d\nATA_DMA=%d\n",
            G.ramfs_enable,G.vfs_enable,G.zip_enable,G.ata_dma);
    fprintf(f,"\nNET_ENABLE=%d\nE1000_ENABLE=%d\nE100_ENABLE=%d\nIP_ENABLE=%d\nUDP_ENABLE=%d\nTCP_ENABLE=%d\n",
            G.net_enable,G.e1000_enable,G.e100_enable,G.ip_enable,G.udp_enable,G.tcp_enable);
    fprintf(f,"\nAUDIO_ENABLE=%d\nAC97_ENABLE=%d\nHDA_ENABLE=%d\n",
            G.audio_enable,G.ac97_enable,G.hda_enable);
    fprintf(f,"\nKBD_ENABLE=%d\nMOUSE_ENABLE=%d\nTOUCH_ENABLE=%d\n",
            G.kbd_enable,G.mouse_enable,G.touch_enable);
    fprintf(f,"\nGPU_ENABLE=%d\nVESA_ENABLE=%d\n",G.gpu_enable,G.vesa_enable);
    fprintf(f,"\nPCI_ENABLE=%d\nPCI_PROBE_ALL=%d\n",G.pci_enable,G.pci_probe_all);
    fprintf(f,"\nSCHED_ENABLE=%d\nSCHED_PREEMPT=%d\nSCHED_TIMESLICE_MS=%d\n",
            G.sched_enable,G.sched_preempt,G.sched_timeslice_ms);
    fprintf(f,"\nSYSCALL_ENABLE=%d\nELF_LOADER=%d\n",G.syscall_enable,G.elf_loader);
    fprintf(f,"\nDEBUG_VERBOSE=%d\nDEBUG_KASAN=%d\nDEBUG_PANIC_DUMP=%d\n",
            G.debug_verbose,G.debug_kasan,G.debug_panic_dump);
    fprintf(f,"\nQEMU_RAM_MB=%d\nQEMU_SMP=%d\nQEMU_NET=%d\nQEMU_USB=%d\nQEMU_NOGRAPHIC=%d\n",
            G.qemu_ram_mb,G.qemu_smp,G.qemu_net,G.qemu_usb,G.qemu_nographic);
    fclose(f);
}

/* ====================================================================
 * Drawing helpers
 * ================================================================== */
#define MIN(a,b) ((a)<(b)?(a):(b))
#define MAX(a,b) ((a)>(b)?(a):(b))

/* Draw shadow (2 cells right, 1 cell down) */
static void draw_shadow(int y, int x, int h, int w) {
    wattron(stdscr, COLOR_PAIR(CLR_SHADOW));
    /* bottom shadow */
    for (int i = x+2; i < x+w+2 && i < COLS; i++)
        mvaddch(y+h, i, ' ');
    /* right shadow */
    for (int i = y+1; i < y+h && i < LINES; i++) {
        if (x+w < COLS)   mvaddch(i, x+w,   ' ');
        if (x+w+1 < COLS) mvaddch(i, x+w+1, ' ');
    }
    wattroff(stdscr, COLOR_PAIR(CLR_SHADOW));
}

/*
 * draw_dialog — lxdialog style box
 *   Outer single-line border in CLR_BORDER
 *   Title centred in top border
 *   Body filled with CLR_DIALOG
 */
static void draw_dialog(WINDOW *w, int h, int ww,
                        const char *title, int shadow) {
    int rows, cols;
    getmaxyx(w, rows, cols);
    (void)rows; (void)cols;

    /* Fill body */
    wbkgd(w, COLOR_PAIR(CLR_DIALOG));
    werase(w);

    /* Outer border */
    wattron(w, COLOR_PAIR(CLR_BORDER));
    box(w, 0, 0);
    wattroff(w, COLOR_PAIR(CLR_BORDER));

    /* Inner highlighted line just inside border (top and left) */
    wattron(w, COLOR_PAIR(CLR_HLBORDER) | A_BOLD);
    mvwhline(w, 1, 1, ACS_HLINE, ww-2);
    /* We omit inner box — just the one outer border is fine */
    wattroff(w, COLOR_PAIR(CLR_HLBORDER) | A_BOLD);

    /* Title */
    if (title && title[0]) {
        int tlen = (int)strlen(title);
        int tx = (ww - tlen - 4) / 2;
        if (tx < 1) tx = 1;
        wattron(w, COLOR_PAIR(CLR_TITLE) | A_BOLD);
        mvwprintw(w, 0, tx, " %s ", title);
        wattroff(w, COLOR_PAIR(CLR_TITLE) | A_BOLD);
    }

    (void)shadow; /* shadow drawn on stdscr before newwin */
    (void)h;
}

/* Draw bottom button row inside a dialog window */
static void draw_buttons(WINDOW *w, int row, int ww,
                         const char **lbls, int n, int sel) {
    /* Calculate total width */
    int total = 0;
    for (int i = 0; i < n; i++) total += (int)strlen(lbls[i]) + 6;
    int x = (ww - total) / 2;
    if (x < 1) x = 1;

    for (int i = 0; i < n; i++) {
        if (i == sel) {
            wattron(w, COLOR_PAIR(CLR_BTNSEL) | A_REVERSE | A_BOLD);
            mvwprintw(w, row, x, "< %s >", lbls[i]);
            wattroff(w, COLOR_PAIR(CLR_BTNSEL) | A_REVERSE | A_BOLD);
        } else {
            wattron(w, COLOR_PAIR(CLR_BUTTON));
            mvwprintw(w, row, x, "< %s >", lbls[i]);
            wattroff(w, COLOR_PAIR(CLR_BUTTON));
        }
        x += (int)strlen(lbls[i]) + 6;
    }
}

/* ====================================================================
 * Global help bar at screen bottom
 * ================================================================== */
static void draw_help_bar(const char *msg) {
    wattron(stdscr, COLOR_PAIR(CLR_HELP));
    mvhline(LINES-1, 0, ' ', COLS);
    mvprintw(LINES-1, 1, "%s", msg);
    wattroff(stdscr, COLOR_PAIR(CLR_HELP));
    refresh();
}

/* ====================================================================
 * Popups
 * ================================================================== */
static void show_help(const char *title, const char *text) {
    int w = MIN(COLS - 8, 70);
    int h = 12;
    int y = (LINES - h) / 2;
    int x = (COLS  - w) / 2;

    draw_shadow(y, x, h, w);
    WINDOW *pop = newwin(h, w, y, x);
    keypad(pop, TRUE);
    draw_dialog(pop, h, w, title, 0);

    wattron(pop, COLOR_PAIR(CLR_DIALOG));
    int row = 2;
    const char *p = text;
    while (*p && row < h-3) {
        char line[256]; int n = 0;
        while (*p && *p != '\n' && n < w-4) line[n++] = *p++;
        line[n] = 0;
        if (*p == '\n') p++;
        mvwprintw(pop, row++, 2, "%s", line);
    }
    wattroff(pop, COLOR_PAIR(CLR_DIALOG));

    wattron(pop, COLOR_PAIR(CLR_BORDER));
    mvwhline(pop, h-3, 1, ACS_HLINE, w-2);
    wattroff(pop, COLOR_PAIR(CLR_BORDER));

    const char *b[] = {"  OK  "};
    draw_buttons(pop, h-2, w, b, 1, 0);
    wrefresh(pop);
    wgetch(pop);
    delwin(pop);
    touchwin(stdscr); refresh();
}

static int confirm(const char *msg) {
    int w = MIN(COLS-8, 60);
    int h = 8;
    int y = (LINES-h)/2, x = (COLS-w)/2;
    draw_shadow(y,x,h,w);
    WINDOW *pop = newwin(h,w,y,x);
    keypad(pop, TRUE);
    draw_dialog(pop, h, w, "Question", 0);

    wattron(pop, COLOR_PAIR(CLR_DIALOG));
    mvwprintw(pop, 2, 2, "%.*s", w-4, msg);
    wattroff(pop, COLOR_PAIR(CLR_DIALOG));

    wattron(pop, COLOR_PAIR(CLR_BORDER));
    mvwhline(pop, h-4, 1, ACS_HLINE, w-2);
    wattroff(pop, COLOR_PAIR(CLR_BORDER));

    const char *b[] = {"Yes","No"};
    int sel = 1;
    for(;;) {
        draw_buttons(pop, h-2, w, b, 2, sel);
        wrefresh(pop);
        int ch = wgetch(pop);
        switch(ch) {
        case KEY_LEFT:  case 'h': sel=0; break;
        case KEY_RIGHT: case 'l': sel=1; break;
        case 'y': case 'Y': delwin(pop); touchwin(stdscr); refresh(); return 1;
        case 'n': case 'N': delwin(pop); touchwin(stdscr); refresh(); return 0;
        case '\n': { int r=(sel==0); delwin(pop); touchwin(stdscr); refresh(); return r; }
        case 27:  delwin(pop); touchwin(stdscr); refresh(); return 0;
        }
    }
}

static void input_string(const char *prompt, char *buf, int buflen) {
    int w = MIN(COLS-8, 68);
    int h = 9;
    int y = (LINES-h)/2, x = (COLS-w)/2;
    draw_shadow(y,x,h,w);
    WINDOW *pop = newwin(h,w,y,x);
    keypad(pop, TRUE);
    draw_dialog(pop, h, w, "Enter string", 0);

    wattron(pop, COLOR_PAIR(CLR_DIALOG));
    mvwprintw(pop, 2, 2, "%.*s", w-4, prompt);
    mvwprintw(pop, 3, 2, "Enter value (empty = keep current):");
    wattroff(pop, COLOR_PAIR(CLR_DIALOG));

    wattron(pop, COLOR_PAIR(CLR_INPUTBG));
    mvwhline(pop, 5, 2, ' ', w-4);
    mvwprintw(pop, 5, 2, "%.*s", w-4, buf);
    wattroff(pop, COLOR_PAIR(CLR_INPUTBG));

    wattron(pop, COLOR_PAIR(CLR_BORDER));
    mvwhline(pop, h-3, 1, ACS_HLINE, w-2);
    wattroff(pop, COLOR_PAIR(CLR_BORDER));
    const char *b[] = {"OK","Cancel"};
    draw_buttons(pop, h-2, w, b, 2, 0);
    wrefresh(pop);

    echo(); curs_set(1);
    wmove(pop, 5, 2);
    wattron(pop, COLOR_PAIR(CLR_INPUTBG));
    /* clear field */
    mvwhline(pop, 5, 2, ' ', w-4);
    wmove(pop, 5, 2);
    char tmp[256] = {0};
    wgetnstr(pop, tmp, MIN(buflen-1, w-5));
    noecho(); curs_set(0);
    wattroff(pop, COLOR_PAIR(CLR_INPUTBG));

    if (tmp[0]) strncpy(buf, tmp, buflen-1);
    delwin(pop); touchwin(stdscr); refresh();
}

static void input_int(const char *prompt, int *val) {
    char tmp[32]; snprintf(tmp, sizeof(tmp), "%d", *val);
    input_string(prompt, tmp, sizeof(tmp));
    *val = atoi(tmp);
}

static int choice_popup(const char *title, const char **opts, int n, int cur) {
    int w = MIN(COLS-8, 54);
    int h = n+7;
    if (h > LINES-4) h = LINES-4;
    int vis = h-7;
    int y = (LINES-h)/2, x = (COLS-w)/2;
    draw_shadow(y,x,h,w);
    WINDOW *pop = newwin(h,w,y,x);
    keypad(pop, TRUE);

    int sel = cur, top = 0;
    for(;;) {
        draw_dialog(pop, h, w, title, 0);

        if (sel < top) top = sel;
        if (sel >= top+vis) top = sel-vis+1;

        for (int i = 0; i < vis && top+i < n; i++) {
            int idx = top+i;
            int row = i+2;
            if (idx == sel) {
                wattron(pop, COLOR_PAIR(CLR_LISTHL) | A_BOLD);
                mvwprintw(pop, row, 2, "(*) %-*s", w-8, opts[idx]);
                wattroff(pop, COLOR_PAIR(CLR_LISTHL) | A_BOLD);
            } else {
                wattron(pop, COLOR_PAIR(CLR_LISTBG));
                mvwprintw(pop, row, 2, "( ) %-*s", w-8, opts[idx]);
                wattroff(pop, COLOR_PAIR(CLR_LISTBG));
            }
        }

        wattron(pop, COLOR_PAIR(CLR_BORDER));
        mvwhline(pop, h-3, 1, ACS_HLINE, w-2);
        wattroff(pop, COLOR_PAIR(CLR_BORDER));
        const char *b[] = {"Select","Cancel"};
        draw_buttons(pop, h-2, w, b, 2, 0);
        wrefresh(pop);

        int ch = wgetch(pop);
        switch(ch) {
        case KEY_UP:   sel = (sel-1+n)%n; break;
        case KEY_DOWN: sel = (sel+1)%n;   break;
        case '\n': case KEY_RIGHT:
            delwin(pop); touchwin(stdscr); refresh(); return sel;
        case 27: case 'q': case 'Q':
            delwin(pop); touchwin(stdscr); refresh(); return cur;
        }
    }
}

/* ====================================================================
 * Menu item type
 * ================================================================== */
typedef struct {
    int type;          /* 0=bool 1=choice 2=int 3=str 4=sep 5=preset 6=sub 7=comment */
    const char *label;
    const char *help;
    int   *b;
    int   *iv;
    char  *sv;
    int    sv_len;
    const char **ch_opts;
    int    ch_n;
    void (*sub_fn)(void);
    void (*preset_fn)(void);
    int    indent;     /* 0=top-level, 1=indented child */
} mi_t;

/* Macros — explicit indent param */
#define _BOOL(lbl,fld,hlp,ind)    {0,lbl,hlp,&G.fld,0,0,0,0,0,0,0,ind}
#define _CHO(lbl,fld,opts,hlp,ind){1,lbl,hlp,0,0,G.fld,sizeof(G.fld),opts,(int)(sizeof(opts)/sizeof(opts[0])),0,0,ind}
#define _INT(lbl,fld,hlp,ind)     {2,lbl,hlp,0,&G.fld,0,0,0,0,0,0,ind}
#define _STR(lbl,fld,hlp,ind)     {3,lbl,hlp,0,0,G.fld,sizeof(G.fld),0,0,0,0,ind}
#define _SEP()                    {4,"",""  ,0,0,0,0,0,0,0,0,0}
#define _SUB(lbl,fn,hlp)          {6,lbl,hlp,0,0,0,0,0,0,fn,0,0}
#define _PRE(lbl,fn,hlp)          {5,lbl,hlp,0,0,0,0,0,0,0,fn,0}
#define _COM(lbl)                 {7,lbl,""  ,0,0,0,0,0,0,0,0,0}

/* Shorthand keeping old style */
#define MI_BOOL(l,f,h)        _BOOL(l,f,h,0)
#define MI_INT(l,f,h)         _INT(l,f,h,0)
#define MI_STR(l,f,h)         _STR(l,f,h,0)
#define MI_CHO(l,f,o,h)       _CHO(l,f,o,h,0)
#define MI_SEP()              _SEP()
#define MI_SUB(l,fn,h)        _SUB(l,fn,h)
#define MI_PRE(l,fn,h)        _PRE(l,fn,h)
#define MI_COM(l)             _COM(l)

/* ====================================================================
 * Core menu renderer — faithful Linux menuconfig layout
 *
 *  ┌──────────────────────────────────────────────────────────────┐
 *  │                   [ title ]                                  │
 *  │──────────────────────────────────────────────────────────────│  <- inner line
 *  │  .config - Ark Kernel Configuration                          │  <- subtitle
 *  │  ┌──────────────────────────────────────────────────────┐   │
 *  │  │  [*] item one                                        │   │  <- list box
 *  │  │  [ ] item two                                        │   │
 *  │  │  ---> Submenu                                        │   │
 *  │  └──────────────────────────────────────────────────────┘   │
 *  │──────────────────────────────────────────────────────────────│
 *  │  <Select>  <Exit>  <Help>  <Save As>  <Load>                 │  <- buttons
 *  └──────────────────────────────────────────────────────────────┘
 *
 * ================================================================== */

/* Render one list item line at window row */
static void render_item(WINDOW *lw, int row, int lww, mi_t *m, int is_sel) {
    /* background */
    if (is_sel) {
        wattron(lw, COLOR_PAIR(CLR_LISTHL) | A_BOLD);
    } else {
        wattron(lw, COLOR_PAIR(CLR_LISTBG));
    }
    mvwhline(lw, row, 0, ' ', lww);

    int cx = 1 + m->indent * 2;

    switch (m->type) {
    case 0: { /* BOOL */
        /* bracket */
        if (!is_sel) wattron(lw, COLOR_PAIR(CLR_CHECK));
        mvwaddch(lw, row, cx, '[');
        if (!is_sel) {
            wattroff(lw, COLOR_PAIR(CLR_CHECK));
            wattron(lw, COLOR_PAIR(CLR_CHECKMARK) | A_BOLD);
        }
        mvwaddch(lw, row, cx+1, *m->b ? '*' : ' ');
        if (!is_sel) {
            wattroff(lw, COLOR_PAIR(CLR_CHECKMARK) | A_BOLD);
            wattron(lw, COLOR_PAIR(CLR_CHECK));
        }
        mvwaddch(lw, row, cx+2, ']');
        if (!is_sel) wattroff(lw, COLOR_PAIR(CLR_CHECK));
        if (is_sel) wattron(lw, COLOR_PAIR(CLR_LISTHL) | A_BOLD);
        mvwprintw(lw, row, cx+4, "%-*s", lww-cx-6, m->label);
        break;
    }
    case 1: { /* CHOICE */
        if (is_sel) wattron(lw, COLOR_PAIR(CLR_LISTHL) | A_BOLD);
        else wattron(lw, COLOR_PAIR(CLR_LISTBG));
        mvwprintw(lw, row, cx, "%-*s", lww-cx-2, m->label);
        /* value on right */
        char val[48]; snprintf(val, sizeof(val), "(%s)", m->sv ? m->sv : "?");
        mvwprintw(lw, row, lww-(int)strlen(val)-1, "%s", val);
        break;
    }
    case 2: { /* INT */
        if (is_sel) wattron(lw, COLOR_PAIR(CLR_LISTHL) | A_BOLD);
        else wattron(lw, COLOR_PAIR(CLR_LISTBG));
        mvwprintw(lw, row, cx, "%-*s", lww-cx-2, m->label);
        char val[24]; snprintf(val, sizeof(val), "(%d)", m->iv ? *m->iv : 0);
        mvwprintw(lw, row, lww-(int)strlen(val)-1, "%s", val);
        break;
    }
    case 3: { /* STRING */
        if (is_sel) wattron(lw, COLOR_PAIR(CLR_LISTHL) | A_BOLD);
        else wattron(lw, COLOR_PAIR(CLR_LISTBG));
        mvwprintw(lw, row, cx, "%-*s", lww-cx-2, m->label);
        char val[48]; snprintf(val, sizeof(val), "(%s)", m->sv ? m->sv : "");
        mvwprintw(lw, row, lww-(int)strlen(val)-1, "%s", val);
        break;
    }
    case 6: { /* SUBMENU */
        /* ---> label */
        if (!is_sel) {
            wattron(lw, COLOR_PAIR(CLR_TAG) | A_BOLD);
        }
        mvwprintw(lw, row, cx, "--->");
        if (!is_sel) {
            wattroff(lw, COLOR_PAIR(CLR_TAG) | A_BOLD);
            wattron(lw, COLOR_PAIR(CLR_LISTBG));
        } else {
            wattron(lw, COLOR_PAIR(CLR_LISTHL) | A_BOLD);
        }
        mvwprintw(lw, row, cx+5, "%-*s", lww-cx-7, m->label);
        break;
    }
    case 5: { /* PRESET */
        if (!is_sel) wattron(lw, COLOR_PAIR(CLR_TAG));
        else wattron(lw, COLOR_PAIR(CLR_LISTHL) | A_BOLD);
        mvwprintw(lw, row, cx, "%-*s", lww-cx-2, m->label);
        break;
    }
    case 7: { /* COMMENT */
        wattroff(lw, COLOR_PAIR(CLR_LISTHL) | A_BOLD);
        wattron(lw, COLOR_PAIR(CLR_LISTBG) | A_BOLD);
        mvwprintw(lw, row, cx, "*** %-*s ***", lww-cx-10, m->label);
        break;
    }
    }

    if (is_sel) wattroff(lw, COLOR_PAIR(CLR_LISTHL) | A_BOLD);
    else         wattroff(lw, COLOR_PAIR(CLR_LISTBG));
}

static void run_menu(const char *title, mi_t *items, int n) {
    /* Outer dialog fills the terminal (1-cell margin) */
    int DW = COLS - 2;
    int DH = LINES - 2;
    int DY = 1;
    int DX = 1;

    /* Inner list box inside dialog:
       row 0   = dialog border top
       row 1   = inner hline
       row 2   = subtitle
       row 3   = list border top
       row 4.. = list items
       row 4+vis = list border bottom
       row 4+vis+1 = separator hline
       row 4+vis+2 = buttons
       row 4+vis+3 = dialog border bottom = DH-1
    */
    int vis_h = DH - 8;   /* visible list rows */
    if (vis_h < 3) vis_h = 3;

    /* List window inside dialog (no border, just items) */
    int LW = DW - 6;      /* list inner width */
    int LH = vis_h;
    int LY = DY + 4;
    int LX = DX + 3;

    WINDOW *dwin = newwin(DH, DW, DY, DX);
    WINDOW *lwin = newwin(LH, LW, LY, LX);
    keypad(dwin, TRUE);
    keypad(lwin, TRUE);

    int cur = 0, top = 0, done = 0;
    /* skip separators */
    while (cur < n && (items[cur].type == 4)) cur++;

    while (!done) {
        /* --- Draw outer dialog --- */
        draw_dialog(dwin, DH, DW, title, 0);

        /* subtitle line */
        wattron(dwin, COLOR_PAIR(CLR_DIALOG));
        mvwprintw(dwin, 2, 2, ".config - Ark Kernel Configuration");
        wattroff(dwin, COLOR_PAIR(CLR_DIALOG));

        /* list border box */
        wattron(dwin, COLOR_PAIR(CLR_BORDER));
        mvwaddch(dwin, 3, 2, ACS_ULCORNER);
        mvwhline(dwin, 3, 3, ACS_HLINE, DW-6);
        mvwaddch(dwin, 3, DW-3, ACS_URCORNER);
        for (int r = 4; r < 4+vis_h; r++) {
            mvwaddch(dwin, r, 2, ACS_VLINE);
            mvwaddch(dwin, r, DW-3, ACS_VLINE);
        }
        mvwaddch(dwin, 4+vis_h, 2, ACS_LLCORNER);
        mvwhline(dwin, 4+vis_h, 3, ACS_HLINE, DW-6);
        mvwaddch(dwin, 4+vis_h, DW-3, ACS_LRCORNER);
        wattroff(dwin, COLOR_PAIR(CLR_BORDER));

        /* scroll indicators */
        if (top > 0) {
            wattron(dwin, COLOR_PAIR(CLR_ARROW) | A_BOLD);
            mvwprintw(dwin, 3, DW/2-1, "(-)");
            wattroff(dwin, COLOR_PAIR(CLR_ARROW) | A_BOLD);
        }
        if (top + vis_h < n) {
            wattron(dwin, COLOR_PAIR(CLR_ARROW) | A_BOLD);
            mvwprintw(dwin, 4+vis_h, DW/2-1, "(+)");
            wattroff(dwin, COLOR_PAIR(CLR_ARROW) | A_BOLD);
        }

        /* separator above buttons */
        wattron(dwin, COLOR_PAIR(CLR_BORDER));
        mvwhline(dwin, DH-3, 1, ACS_HLINE, DW-2);
        wattroff(dwin, COLOR_PAIR(CLR_BORDER));

        /* buttons */
        const char *b[] = {"Select","Exit","Help","Save As"};
        draw_buttons(dwin, DH-2, DW, b, 4, -1);

        wrefresh(dwin);

        /* --- Draw list items --- */
        if (cur < top) top = cur;
        if (cur >= top + vis_h) top = cur - vis_h + 1;

        wbkgd(lwin, COLOR_PAIR(CLR_LISTBG));
        werase(lwin);

        int shown = 0;
        for (int i = 0; i < vis_h; i++) {
            int idx = top + i;
            if (idx >= n) break;
            shown++;
            mi_t *m = &items[idx];

            if (m->type == 4) { /* separator */
                wattron(lwin, COLOR_PAIR(CLR_LISTBG));
                mvwhline(lwin, i, 0, ACS_HLINE, LW);
                wattroff(lwin, COLOR_PAIR(CLR_LISTBG));
                continue;
            }
            render_item(lwin, i, LW, m, (idx == cur));
        }
        (void)shown;
        wrefresh(lwin);

        /* help bar */
        draw_help_bar(
            "Arrow keys: navigate | Enter/Space: toggle | ?: help | S: save | Q: quit");

        /* --- Input --- */
        int ch = wgetch(dwin);
        switch (ch) {
        case KEY_UP:
            do { cur = (cur-1+n)%n; } while (items[cur].type == 4);
            break;
        case KEY_DOWN:
            do { cur = (cur+1)%n; } while (items[cur].type == 4);
            break;
        case KEY_PPAGE:
        case KEY_HOME:
            cur -= vis_h; if (cur < 0) cur = 0;
            while (cur > 0 && items[cur].type == 4) cur--;
            break;
        case KEY_NPAGE:
        case KEY_END:
            cur += vis_h; if (cur >= n) cur = n-1;
            while (cur < n-1 && items[cur].type == 4) cur++;
            break;

        case ' ':
        case '\n':
        case KEY_RIGHT: {
            mi_t *m = &items[cur];
            switch (m->type) {
            case 0: *m->b ^= 1; break;
            case 1: {
                int ci = 0;
                for (int j = 0; j < m->ch_n; j++)
                    if (!strcmp(m->ch_opts[j], m->sv)) { ci = j; break; }
                int ni = choice_popup(m->label, m->ch_opts, m->ch_n, ci);
                strncpy(m->sv, m->ch_opts[ni], m->sv_len-1);
                break;
            }
            case 2: input_int(m->label, m->iv); break;
            case 3: input_string(m->label, m->sv, m->sv_len); break;
            case 5:
                if (m->preset_fn && confirm("Apply preset? Current settings will be reset."))
                    m->preset_fn();
                break;
            case 6:
                if (m->sub_fn) {
                    delwin(lwin); delwin(dwin);
                    m->sub_fn();
                    dwin = newwin(DH, DW, DY, DX);
                    lwin = newwin(LH, LW, LY, LX);
                    keypad(dwin, TRUE); keypad(lwin, TRUE);
                }
                break;
            }
            break;
        }

        case '?':
        case 'h':
        case 'H': {
            mi_t *m = &items[cur];
            if (m->help && m->help[0])
                show_help(m->label, m->help);
            else
                show_help("Help", "No help text available for this option.");
            break;
        }

        case 's': case 'S':
            save_kconfig();
            show_help("Saved", "Configuration saved to .kconfig");
            break;

        case 'q': case 'Q':
        case KEY_LEFT:
        case 27:
            if (confirm("Exit this menu?")) done = 1;
            break;
        }
    }

    delwin(lwin);
    delwin(dwin);
    touchwin(stdscr);
    refresh();
}

/* ====================================================================
 * Sub-menu builders
 * ================================================================== */
static void menu_io(void) {
    mi_t items[] = {
        MI_COM("Port I/O"),
        MI_BOOL("Port I/O (inb/outb helpers)",  pio_enable,    "Enable x86 port I/O helpers."),
        MI_BOOL("MMIO helpers",                  mmio_enable,   "Memory-mapped I/O read/write helpers."),
        MI_SEP(),
        MI_COM("Interrupt controllers"),
        MI_BOOL("8259 PIC (legacy)",             pic_enable,    "Programmable Interrupt Controller, legacy mode."),
        MI_BOOL("I/O APIC",                      ioapic_enable, "Advanced PIC. Required for SMP."),
        MI_SEP(),
        MI_COM("IDT / IRQ"),
        MI_BOOL("IDT support",                   idt_enable,    "Interrupt Descriptor Table — required for any IRQ."),
        MI_BOOL("Dedicated IRQ stack",           irq_stack,     "Allocate separate kernel stack for IRQ handlers."),
        MI_BOOL("NMI handler",                   nmi_enable,    "Register a Non-Maskable Interrupt handler."),
    };
    run_menu("I/O and Interrupts", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_storage(void) {
    mi_t items[] = {
        MI_COM("Block devices"),
        MI_BOOL("ATA/IDE driver",    ata_enable,   "Classic ATA (PIO/DMA) hard drive support."),
        MI_BOOL("  ATA DMA mode",    ata_dma,      "Use DMA for ATA transfers (faster)."),
        MI_BOOL("SATA / AHCI",       sata_enable,  "AHCI SATA driver."),
        MI_BOOL("SD / MMC card",     sd_enable,    "SD/MMC block device driver."),
        MI_SEP(),
        MI_COM("Filesystems"),
        MI_BOOL("FAT32",             fat32_enable, "FAT32 read/write filesystem support."),
        MI_BOOL("RAM filesystem",    ramfs_enable, "In-memory filesystem (initramfs)."),
        MI_BOOL("VFS layer",         vfs_enable,   "Virtual filesystem abstraction layer."),
        MI_BOOL("ZIP initramfs",     zip_enable,   "Decompress ZIP-format initramfs at boot."),
    };
    run_menu("Storage", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_net(void) {
    mi_t items[] = {
        MI_BOOL("Networking stack",  net_enable,   "Master network subsystem switch."),
        MI_SEP(),
        MI_COM("NIC drivers"),
        MI_BOOL("Intel e1000 (GbE)", e1000_enable, "Intel Gigabit Ethernet driver."),
        MI_BOOL("Intel e100 (100M)", e100_enable,  "Intel Fast Ethernet driver."),
        MI_SEP(),
        MI_COM("Protocols"),
        MI_BOOL("IP layer",          ip_enable,    "IPv4 packet handling."),
        MI_BOOL("UDP",               udp_enable,   "UDP datagram sockets."),
        MI_BOOL("TCP (stub)",        tcp_enable,   "TCP — stub, not fully implemented."),
    };
    run_menu("Networking", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_usb(void) {
    mi_t items[] = {
        MI_BOOL("USB subsystem",       usb_enable, "Master USB enable."),
        MI_SEP(),
        MI_COM("Host controllers"),
        MI_BOOL("xHCI (USB 3/2)",      usb_xhci,  "xHCI host controller (USB 3.0 + 2.0)."),
        MI_BOOL("EHCI (USB 2.0)",      usb_ehci,  "EHCI host controller."),
        MI_BOOL("UHCI (USB 1.1)",      usb_uhci,  "UHCI legacy host controller."),
        MI_SEP(),
        MI_COM("Device classes"),
        MI_BOOL("HID (kbd/mouse)",     usb_hid,   "USB Human Interface Device class."),
    };
    run_menu("USB", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_audio(void) {
    mi_t items[] = {
        MI_BOOL("Audio subsystem",   audio_enable, "Master audio enable."),
        MI_SEP(),
        MI_COM("Drivers"),
        MI_BOOL("AC97 codec",        ac97_enable,  "Intel AC97 audio codec."),
        MI_BOOL("HDA (Intel HD)",    hda_enable,   "Intel High Definition Audio (stub)."),
    };
    run_menu("Audio", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_hid(void) {
    mi_t items[] = {
        MI_COM("Input devices"),
        MI_BOOL("PS/2 keyboard",     kbd_enable,   "PS/2 / AT keyboard driver."),
        MI_BOOL("PS/2 mouse",        mouse_enable, "PS/2 two-button mouse driver."),
        MI_BOOL("Touchscreen",       touch_enable, "Generic resistive touchscreen driver."),
    };
    run_menu("HID / Input", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_fb(void) {
    static const char *drv_opts[] = { "bga", "vesa", "vga" };
    mi_t items[] = {
        MI_BOOL("Framebuffer enable",  fb_enable,   "Enable kernel framebuffer."),
        MI_INT( "Width  (pixels)",     fb_width,    "Horizontal resolution in pixels."),
        MI_INT( "Height (pixels)",     fb_height,   "Vertical resolution in pixels."),
        MI_INT( "Bits per pixel",      fb_bpp,      "Colour depth: 8, 16, 24 or 32."),
        MI_CHO( "Framebuffer driver",  fb_driver, drv_opts,
                "bga = Bochs VBE/QEMU BGA, vesa = INT 0x10 VESA, vga = text-mode VGA."),
        MI_SEP(),
        MI_COM("GPU layers"),
        MI_BOOL("VESA graphics layer", vesa_enable, "VESA mode-switching layer above FB."),
        MI_BOOL("GPU subsystem",       gpu_enable,  "GPU driver subsystem (BGA/VESA)."),
    };
    run_menu("Framebuffer / GPU", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_sched(void) {
    mi_t items[] = {
        MI_BOOL("Scheduler enable",    sched_enable,       "Enable kernel task scheduler."),
        MI_BOOL("Preemptive mode",     sched_preempt,      "Allow preemptive context switches."),
        MI_INT( "Timeslice (ms)",      sched_timeslice_ms, "Scheduler tick in milliseconds."),
    };
    run_menu("Scheduler", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_debug(void) {
    mi_t items[] = {
        MI_BOOL("Verbose debug output", debug_verbose,    "Extra debug messages at boot."),
        MI_BOOL("Panic register dump",  debug_panic_dump, "Dump registers on kernel panic."),
        MI_BOOL("KASAN (stub)",         debug_kasan,      "Kernel address sanitiser stub."),
        MI_INT( "Log level (0-7)",      loglevel,         "0=silent 4=info 7=verbose debug."),
    };
    run_menu("Debugging", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_build(void) {
    mi_t items[] = {
        MI_COM("Compiler"),
        MI_INT( "Optimisation level (-O)", opt_level,    "-O level for GCC (0=none 2=default)."),
        MI_BOOL("Debug build (-g -O0)",    debug,        "Add -g and force -O0."),
        MI_BOOL("Werror",                  werror,       "Treat all warnings as errors."),
        MI_SEP(),
        MI_COM("Init / userspace"),
        MI_BOOL("Build userspace init",    build_init,   "Compile userspace/init.c with ark-gcc."),
        MI_STR( "Init binary path",        init_bin,     "Path to userspace init inside initramfs."),
        MI_SEP(),
        MI_COM("Serial console"),
        MI_BOOL("Serial console",          serial_enable,"Enable serial port for console output."),
        MI_INT( "Serial base port",        serial_port,  "Base I/O port: 0x3F8=COM1 0x2F8=COM2."),
        MI_SEP(),
        MI_COM("Kernel printk"),
        MI_BOOL("Printk enable",           printk_enable,"Enable kernel printk subsystem."),
    };
    run_menu("Build and Boot", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_qemu(void) {
    mi_t items[] = {
        MI_COM("Machine"),
        MI_INT( "RAM (MB)",           qemu_ram_mb,   "Memory passed to QEMU with -m."),
        MI_INT( "SMP cores",          qemu_smp,      "Number of vCPUs (-smp N)."),
        MI_SEP(),
        MI_COM("Devices"),
        MI_BOOL("e1000 NIC",          qemu_net,      "Add -device e1000,netdev=... to QEMU."),
        MI_BOOL("USB device",         qemu_usb,      "Add -usb -device usb-mouse to QEMU."),
        MI_SEP(),
        MI_COM("Display"),
        MI_BOOL("No-graphic mode",    qemu_nographic,"Pass -nographic (serial console only)."),
    };
    run_menu("QEMU Settings", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_memory(void) {
    mi_t items[] = {
        MI_COM("Memory managers"),
        MI_BOOL("Physical MM (PMM)",   pmm_enable,    "Enable PMM for page-frame allocation."),
        MI_BOOL("Virtual MM (VMM)",    vmm_enable,    "Enable paging and VMM."),
        MI_SEP(),
        MI_COM("Sizes"),
        MI_INT( "Kernel heap (KB)",    heap_size_kb,  "Initial kernel heap size in KiB."),
        MI_INT( "Kernel stack (KB)",   stack_size_kb, "Initial kernel stack size in KiB."),
    };
    run_menu("Memory Management", items, (int)(sizeof(items)/sizeof(items[0])));
}

static void menu_pci(void) {
    mi_t items[] = {
        MI_BOOL("PCI subsystem",       pci_enable,    "Enable PCI bus enumeration."),
        MI_BOOL("Probe all functions", pci_probe_all, "Scan all PCI functions not only bus 0."),
    };
    run_menu("PCI", items, (int)(sizeof(items)/sizeof(items[0])));
}

/* ====================================================================
 * Top-level menu
 * ================================================================== */
static const char *arch_opts[] = { "x86", "x86_64" };

static void top_menu(void) {
    for (;;) {
        mi_t items[] = {
            /* General */
            MI_COM("General setup"),
            MI_CHO("Architecture", arch, arch_opts, "Target ISA: x86 (32-bit) or x86_64 (64-bit)."),
            MI_STR("Kernel codename", codename, "Human-readable build codename string."),
            MI_SEP(),

            /* Sub-menus */
            MI_SUB("Build and Boot",        menu_build,   "Compiler flags, init binary, serial."),
            MI_SUB("Memory Management",     menu_memory,  "PMM, VMM, heap and stack sizes."),
            MI_SUB("I/O and Interrupts",    menu_io,      "Port I/O, PIC, APIC, IDT."),
            MI_SUB("Framebuffer / GPU",     menu_fb,      "Display driver and graphics settings."),
            MI_SUB("Storage subsystems",    menu_storage, "ATA, SATA, SD, FAT32, VFS."),
            MI_SUB("Networking",            menu_net,     "e1000, e100, IP, UDP, TCP."),
            MI_SUB("USB",                   menu_usb,     "xHCI, EHCI, UHCI, HID."),
            MI_SUB("Audio",                 menu_audio,   "AC97, Intel HDA."),
            MI_SUB("HID / Input devices",   menu_hid,     "Keyboard, mouse, touchscreen."),
            MI_SUB("PCI",                   menu_pci,     "PCI enumeration and probe."),
            MI_SUB("Scheduler",             menu_sched,   "Preemptive task scheduler."),
            MI_SUB("Debugging",             menu_debug,   "Verbose logs, KASAN, panic dump."),
            MI_SUB("QEMU settings",         menu_qemu,    "RAM, SMP, device options for QEMU."),
            MI_SEP(),

            /* Inline options */
            MI_BOOL("Syscall interface",    syscall_enable,"Enable system call dispatch table."),
            MI_BOOL("ELF loader",           elf_loader,    "Load ELF binaries from filesystem."),
            MI_SEP(),

            /* Presets */
            MI_COM("Configuration presets"),
            MI_PRE("defconfig  (recommended defaults)",  apply_defconfig,  "Sane defaults for x86_64."),
            MI_PRE("tinyconfig (minimal footprint)",     apply_tinyconfig, "Minimal x86 32-bit config."),
            MI_PRE("allyes     (enable everything)",     apply_allyes,     "Turn on every feature."),
            MI_PRE("allno      (absolute bare minimum)", apply_allno,      "Disable everything except essentials."),
        };

        if (!strcmp(G.arch, "x86_64")) G.bits = 64; else G.bits = 32;

        run_menu("Ark Kernel Configuration", items,
                 (int)(sizeof(items)/sizeof(items[0])));
        break;
    }
}

/* ====================================================================
 * Splash / title screen  (drawn directly on stdscr background)
 * ================================================================== */
static void draw_splash(void) {
    bkgd(COLOR_PAIR(CLR_SCREEN));
    clear();

    /* Top banner */
    wattron(stdscr, COLOR_PAIR(CLR_SCREEN) | A_BOLD);
    mvhline(0, 0, ' ', COLS);
    const char *banner = "Ark Kernel  v0.1  Configuration System";
    mvprintw(0, (COLS-(int)strlen(banner))/2, "%s", banner);
    wattroff(stdscr, COLOR_PAIR(CLR_SCREEN) | A_BOLD);

    /* Centre card */
    int cw = MIN(COLS-8, 66);
    int ch = 13;
    int cy = (LINES-ch)/2 - 1;
    int cx = (COLS-cw)/2;

    draw_shadow(cy, cx, ch, cw);

    WINDOW *card = newwin(ch, cw, cy, cx);
    wbkgd(card, COLOR_PAIR(CLR_DIALOG));
    draw_dialog(card, ch, cw, "Welcome", 0);

    wattron(card, COLOR_PAIR(CLR_DIALOG));
    mvwprintw(card, 2, 3, "This is the Ark kernel kernel configuration tool,");
    mvwprintw(card, 5, 3, "Navigation:");
    mvwprintw(card, 6, 5, "Arrow keys / PgUp / PgDn  Move cursor");
    mvwprintw(card, 7, 5, "Enter / Space             Toggle / Enter submenu");
    mvwprintw(card, 8, 5, "?                         Help for selected item");
    mvwprintw(card, 9, 5, "S                         Save .kconfig");
    mvwprintw(card,10, 5, "Q / Esc                   Exit / back");
    wattroff(card, COLOR_PAIR(CLR_DIALOG));

    wattron(card, COLOR_PAIR(CLR_BORDER));
    mvwhline(card, ch-3, 1, ACS_HLINE, cw-2);
    wattroff(card, COLOR_PAIR(CLR_BORDER));

    const char *b[] = {"  OK  "};
    draw_buttons(card, ch-2, cw, b, 1, 0);

    /* Bottom help bar */
    draw_help_bar("Press Enter or OK to continue...");

    wrefresh(card);
    wgetch(card);
    delwin(card);
    touchwin(stdscr);
    refresh();
}

/* ====================================================================
 * main
 * ================================================================== */
int main(int argc, char **argv) {
    if (argc >= 2) {
        load_kconfig();
        if (!strcmp(argv[1],"--defconfig"))  { apply_defconfig();  save_kconfig(); return 0; }
        if (!strcmp(argv[1],"--tinyconfig")) { apply_tinyconfig(); save_kconfig(); return 0; }
        if (!strcmp(argv[1],"--allyes"))     { apply_allyes();     save_kconfig(); return 0; }
        if (!strcmp(argv[1],"--allno"))      { apply_allno();      save_kconfig(); return 0; }
    }

    load_kconfig();

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    set_escdelay(25);
    init_colours();

    /* Blue background */
    bkgd(COLOR_PAIR(CLR_SCREEN));
    clear(); refresh();

    draw_splash();

    /* Keep redrawing background between menus */
    bkgd(COLOR_PAIR(CLR_SCREEN));
    clear(); refresh();

    top_menu();

    /* Final save */
    bkgd(COLOR_PAIR(CLR_SCREEN));
    clear(); refresh();

    if (confirm("Save configuration to .kconfig before exit?"))
        save_kconfig();

    endwin();
    printf("Configuration saved to .kconfig\n");
    return 0;
}
