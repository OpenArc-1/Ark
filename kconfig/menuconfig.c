/*
 * kconfig/menuconfig.c  —  Ark kernel ncurses menuconfig
 *
 *  Visual design:
 *    - Uniform dark-grey background (no blue)
 *    - White/grey text everywhere; cyan accent for selection
 *    - Tab cycles between list <-> button bar; Enter activates
 *    - Smooth slide transition when entering / leaving submenus
 *    - Presets live in a dedicated F2 popup (not in the main list)
 *    - F1 help, F5 save, F6 load, Esc/q back, Tab button-bar focus
 *
 *  Build:  gcc -O2 -o kconfig/menuconfig kconfig/menuconfig.c -lncurses
 */

#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>


/* =========================================================================
 * Colour pairs — dark-grey monochrome palette
 * ========================================================================= */
#define C_BASE      1   /* white on dark-grey     — general background       */
#define C_BORDER    2   /* bright white on d-grey — box borders              */
#define C_TITLE     3   /* bold white on d-grey   — box title text           */
#define C_SEL       4   /* black on white         — selected list item       */
#define C_TAG       5   /* bold cyan on d-grey    — [*] bracket chars        */
#define C_TAG_SEL   6   /* bold black on white    — [*] bracket when sel     */
#define C_ARROW     7   /* bold white on d-grey   — ---> submenu arrow       */
#define C_BTN       8   /* white on d-grey        — button normal            */
#define C_BTN_SEL   9   /* bold black on white    — button focused           */
#define C_SEP      10   /* white on d-grey        — separator line           */
#define C_HDR      11   /* bold white on d-grey   — section header           */
#define C_INPUT    12   /* black on white         — text input field         */
#define C_DIM      13   /* dim white on d-grey    — hint text                */
#define C_STATUS   14   /* black on white         — header/status bar        */

static short BG_COL = COLOR_BLACK;

static void init_colours(void) {
    if (!has_colors()) return;
    start_color();
    use_default_colors();

    if (COLORS >= 256) {
        BG_COL = 235;  /* very dark grey */
    } else {
        BG_COL = COLOR_BLACK;
    }

    init_pair(C_BASE,    COLOR_WHITE,  BG_COL);
    init_pair(C_BORDER,  COLOR_WHITE,  BG_COL);
    init_pair(C_TITLE,   COLOR_WHITE,  BG_COL);
    init_pair(C_SEL,     COLOR_BLACK,  COLOR_WHITE);
    init_pair(C_TAG,     COLOR_CYAN,   BG_COL);
    init_pair(C_TAG_SEL, COLOR_BLACK,  COLOR_WHITE);
    init_pair(C_ARROW,   COLOR_WHITE,  BG_COL);
    init_pair(C_BTN,     COLOR_WHITE,  BG_COL);
    init_pair(C_BTN_SEL, COLOR_BLACK,  COLOR_WHITE);
    init_pair(C_SEP,     COLOR_WHITE,  BG_COL);
    init_pair(C_HDR,     COLOR_WHITE,  BG_COL);
    init_pair(C_INPUT,   COLOR_BLACK,  COLOR_WHITE);
    init_pair(C_DIM,     COLOR_WHITE,  BG_COL);
    init_pair(C_STATUS,  COLOR_BLACK,  COLOR_WHITE);
}

/* =========================================================================
 * Config state
 * ========================================================================= */
typedef struct {
    char arch[16];
    char codename[64];
    int  opt_level;
    int  debug;
    int  werror;
    char init_bin[256];
    int  build_init;
    int  printk_enable;
    int  serial_enable;
    int  serial_port;
    int  loglevel;
    int  pmm_enable;
    int  vmm_enable;
    int  heap_size_kb;
    int  stack_size_kb;
    int  fb_enable;
    int  fb_width;
    int  fb_height;
    int  fb_bpp;
    char fb_driver[16];
    int  pio_enable;
    int  mmio_enable;
    int  ioapic_enable;
    int  pic_enable;
    int  idt_enable;
    int  irq_stack;
    int  nmi_enable;
    int  usb_enable;
    int  usb_xhci;
    int  usb_ehci;
    int  usb_uhci;
    int  usb_hid;
    int  ata_enable;
    int  sata_enable;
    int  sd_enable;
    int  fat32_enable;
    int  ramfs_enable;
    int  afs_enable;
    int  afs_gpt;
    int  afs_mbr;
    int  vfs_enable;
    int  zip_enable;
    int  ata_dma;
    int  net_enable;
    int  e1000_enable;
    int  e100_enable;
    int  ip_enable;
    int  udp_enable;
    int  tcp_enable;
    int  audio_enable;
    int  ac97_enable;
    int  hda_enable;
    int  kbd_enable;
    int  mouse_enable;
    int  touch_enable;
    int  gpu_enable;
    int  vesa_enable;
    int  pci_enable;
    int  pci_probe_all;
    int  sched_enable;
    int  sched_preempt;
    int  sched_timeslice_ms;
    int  sched_max_tasks;
    int  sched_stack_kb;
    int  sched_job_control;
    int  syscall_enable;
    int  elf_loader;
    int  debug_verbose;
    int  debug_kasan;
    int  debug_panic_dump;
    /* Language runtime support */
    int  zig_enable;
    int  rust_enable;
} ark_cfg_t;

static ark_cfg_t G;

/* =========================================================================
 * Presets
 * ========================================================================= */
static void apply_defconfig(void) {
    memset(&G, 0, sizeof G);
    strcpy(G.arch, "x86_64"); strcpy(G.codename, "affectionate-cat");
    G.opt_level = 2; strcpy(G.init_bin, "/init");
    G.printk_enable=1; G.serial_enable=1; G.serial_port=0x3F8; G.loglevel=4;
    G.pmm_enable=1; G.vmm_enable=1; G.heap_size_kb=4096; G.stack_size_kb=64;
    G.fb_enable=1; G.fb_width=1024; G.fb_height=768; G.fb_bpp=32;
    strcpy(G.fb_driver, "bga");
    G.pio_enable=1; G.mmio_enable=1; G.ioapic_enable=1; G.pic_enable=1;
    G.idt_enable=1; G.irq_stack=1; G.nmi_enable=1;
    G.usb_enable=1; G.usb_xhci=1; G.usb_ehci=1; G.usb_hid=1;
    G.ata_enable=1; G.sata_enable=1; G.sd_enable=1;
    G.fat32_enable=1; G.ramfs_enable=1; G.afs_enable=1; G.afs_gpt=1; G.afs_mbr=1; G.vfs_enable=1; G.zip_enable=1; G.ata_dma=1;
    G.net_enable=1; G.e1000_enable=1; G.ip_enable=1; G.udp_enable=1;
    G.audio_enable=1; G.ac97_enable=1;
    G.kbd_enable=1; G.mouse_enable=1;
    G.gpu_enable=1; G.vesa_enable=1;
    G.pci_enable=1; G.pci_probe_all=1;
    G.sched_enable=1; G.sched_preempt=1; G.sched_timeslice_ms=10;
    G.sched_max_tasks=64; G.sched_stack_kb=16; G.sched_job_control=1;
    G.syscall_enable=1; G.elf_loader=1; G.debug_panic_dump=1;
    G.zig_enable=0; G.rust_enable=0;
}

static void apply_tinyconfig(void) {
    memset(&G, 0, sizeof G);
    strcpy(G.arch, "x86"); strcpy(G.codename, "tiny");
    G.opt_level=2; strcpy(G.init_bin, "/init");
    G.printk_enable=1; G.loglevel=2;
    G.pmm_enable=1; G.heap_size_kb=512; G.stack_size_kb=16;
    G.fb_enable=1; G.fb_width=640; G.fb_height=480; G.fb_bpp=32;
    strcpy(G.fb_driver, "bga");
    G.pio_enable=1; G.mmio_enable=1; G.pic_enable=1; G.idt_enable=1;
    G.ata_enable=1; G.fat32_enable=1; G.afs_enable=1; G.afs_gpt=1; G.afs_mbr=1; G.ramfs_enable=1; G.vfs_enable=1;
    G.kbd_enable=1; G.gpu_enable=1; G.pci_enable=1; G.syscall_enable=1;
}

static void apply_allyes(void) {
    apply_defconfig();
    G.debug=1; G.usb_uhci=1; G.e100_enable=1; G.tcp_enable=1;
    G.hda_enable=1; G.touch_enable=1;
    G.debug_verbose=1; G.debug_kasan=1;
    G.zig_enable=1; G.rust_enable=1;
}

static void apply_allno(void) {
    memset(&G, 0, sizeof G);
    strcpy(G.arch, "x86"); strcpy(G.codename, "allno");
    G.opt_level=2; strcpy(G.init_bin, "/init");
    G.printk_enable=1; G.pio_enable=1; G.idt_enable=1; G.pic_enable=1;
    G.pmm_enable=1; G.fb_enable=1; G.fb_width=640; G.fb_height=480; G.fb_bpp=32;
    strcpy(G.fb_driver, "bga");
    G.pci_enable=1; G.ata_enable=1; G.ramfs_enable=1;
    G.vfs_enable=1; G.fat32_enable=1; G.afs_enable=0; G.afs_gpt=0; G.afs_mbr=0; G.kbd_enable=1;
}

/* =========================================================================
 * .kconfig  load / save
 * ========================================================================= */
#define KCONF_PATH ".kconfig"

static void load_kconfig(void) {
    apply_defconfig();
    FILE *f = fopen(KCONF_PATH, "r");
    if (!f) return;
    char line[512];
    while (fgets(line, sizeof line, f)) {
        char *nl=strchr(line,'\n'); if(nl)*nl=0;
        char *eq=strchr(line,'=');  if(!eq)continue;
        *eq=0; char *k=line, *v=eq+1;
        while(isspace((unsigned char)*k))k++;
        while(isspace((unsigned char)*v))v++;
#define LS(key,fld) if(!strcmp(k,#key)){strncpy(G.fld,v,sizeof(G.fld)-1);continue;}
#define LI(key,fld) if(!strcmp(k,#key)){G.fld=atoi(v);continue;}
        LS(ARCH,arch) LS(CODENAME,codename) LS(INIT_BIN,init_bin) LS(FB_DRIVER,fb_driver)
        LI(OPT_LEVEL,opt_level) LI(DEBUG,debug) LI(WERROR,werror) LI(BUILD_INIT,build_init)
        LI(PRINTK_ENABLE,printk_enable) LI(SERIAL_ENABLE,serial_enable)
        LI(SERIAL_PORT,serial_port) LI(LOGLEVEL,loglevel)
        LI(PMM_ENABLE,pmm_enable) LI(VMM_ENABLE,vmm_enable)
        LI(HEAP_SIZE_KB,heap_size_kb) LI(STACK_SIZE_KB,stack_size_kb)
        LI(FB_ENABLE,fb_enable) LI(FB_WIDTH,fb_width) LI(FB_HEIGHT,fb_height) LI(FB_BPP,fb_bpp)
        LI(PIO_ENABLE,pio_enable) LI(MMIO_ENABLE,mmio_enable)
        LI(IOAPIC_ENABLE,ioapic_enable) LI(PIC_ENABLE,pic_enable)
        LI(IDT_ENABLE,idt_enable) LI(IRQ_STACK,irq_stack) LI(NMI_ENABLE,nmi_enable)
        LI(USB_ENABLE,usb_enable) LI(USB_XHCI,usb_xhci) LI(USB_EHCI,usb_ehci)
        LI(USB_UHCI,usb_uhci) LI(USB_HID,usb_hid)
        LI(ATA_ENABLE,ata_enable) LI(ATA_DMA,ata_dma) LI(SATA_ENABLE,sata_enable)
        LI(SD_ENABLE,sd_enable) LI(FAT32_ENABLE,fat32_enable) LI(RAMFS_ENABLE,ramfs_enable)
        LI(AFS_ENABLE,afs_enable) LI(AFS_GPT,afs_gpt) LI(AFS_MBR,afs_mbr)
        LI(VFS_ENABLE,vfs_enable) LI(ZIP_ENABLE,zip_enable)
        LI(NET_ENABLE,net_enable) LI(E1000_ENABLE,e1000_enable) LI(E100_ENABLE,e100_enable)
        LI(IP_ENABLE,ip_enable) LI(UDP_ENABLE,udp_enable) LI(TCP_ENABLE,tcp_enable)
        LI(AUDIO_ENABLE,audio_enable) LI(AC97_ENABLE,ac97_enable) LI(HDA_ENABLE,hda_enable)
        LI(KBD_ENABLE,kbd_enable) LI(MOUSE_ENABLE,mouse_enable) LI(TOUCH_ENABLE,touch_enable)
        LI(GPU_ENABLE,gpu_enable) LI(VESA_ENABLE,vesa_enable)
        LI(PCI_ENABLE,pci_enable) LI(PCI_PROBE_ALL,pci_probe_all)
        LI(SCHED_ENABLE,sched_enable) LI(SCHED_PREEMPT,sched_preempt)
        LI(SCHED_TIMESLICE_MS,sched_timeslice_ms) LI(SCHED_MAX_TASKS,sched_max_tasks)
        LI(SCHED_STACK_KB,sched_stack_kb) LI(SCHED_JOB_CONTROL,sched_job_control)
        LI(SYSCALL_ENABLE,syscall_enable) LI(ELF_LOADER,elf_loader)
        LI(DEBUG_VERBOSE,debug_verbose) LI(DEBUG_KASAN,debug_kasan)
        LI(DEBUG_PANIC_DUMP,debug_panic_dump)
        LI(ZIG_ENABLE,zig_enable) LI(RUST_ENABLE,rust_enable)
#undef LS
#undef LI
    }
    fclose(f);
}

static void save_kconfig(void) {
    FILE *f = fopen(KCONF_PATH, "w");
    if (!f) return;
    fprintf(f,"# Ark kernel configuration\n# Generated by menuconfig\n\n");
    fprintf(f,"ARCH=%s\nCODENAME=%s\nOPT_LEVEL=%d\nDEBUG=%d\nWERROR=%d\n",
            G.arch,G.codename,G.opt_level,G.debug,G.werror);
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
    fprintf(f,"\nATA_ENABLE=%d\nATA_DMA=%d\nSATA_ENABLE=%d\nSD_ENABLE=%d\n"
              "FAT32_ENABLE=%d\nRAMFS_ENABLE=%d\nAFS_ENABLE=%d\nAFS_GPT=%d\nAFS_MBR=%d\n"
              "VFS_ENABLE=%d\nZIP_ENABLE=%d\n",
            G.ata_enable,G.ata_dma,G.sata_enable,G.sd_enable,
            G.fat32_enable,G.ramfs_enable,G.afs_enable,G.afs_gpt,G.afs_mbr,
            G.vfs_enable,G.zip_enable);
    fprintf(f,"\nNET_ENABLE=%d\nE1000_ENABLE=%d\nE100_ENABLE=%d\n"
              "IP_ENABLE=%d\nUDP_ENABLE=%d\nTCP_ENABLE=%d\n",
            G.net_enable,G.e1000_enable,G.e100_enable,
            G.ip_enable,G.udp_enable,G.tcp_enable);
    fprintf(f,"\nAUDIO_ENABLE=%d\nAC97_ENABLE=%d\nHDA_ENABLE=%d\n",
            G.audio_enable,G.ac97_enable,G.hda_enable);
    fprintf(f,"\nKBD_ENABLE=%d\nMOUSE_ENABLE=%d\nTOUCH_ENABLE=%d\n",
            G.kbd_enable,G.mouse_enable,G.touch_enable);
    fprintf(f,"\nGPU_ENABLE=%d\nVESA_ENABLE=%d\n",G.gpu_enable,G.vesa_enable);
    fprintf(f,"\nPCI_ENABLE=%d\nPCI_PROBE_ALL=%d\n",G.pci_enable,G.pci_probe_all);
    fprintf(f,"\nSCHED_ENABLE=%d\nSCHED_PREEMPT=%d\nSCHED_TIMESLICE_MS=%d\n"
              "SCHED_MAX_TASKS=%d\nSCHED_STACK_KB=%d\nSCHED_JOB_CONTROL=%d\n",
            G.sched_enable,G.sched_preempt,G.sched_timeslice_ms,
            G.sched_max_tasks,G.sched_stack_kb,G.sched_job_control);
    fprintf(f,"\nSYSCALL_ENABLE=%d\nELF_LOADER=%d\n",G.syscall_enable,G.elf_loader);
    fprintf(f,"\nDEBUG_VERBOSE=%d\nDEBUG_KASAN=%d\nDEBUG_PANIC_DUMP=%d\n",
            G.debug_verbose,G.debug_kasan,G.debug_panic_dump);
    fprintf(f,"\nZIG_ENABLE=%d\nRUST_ENABLE=%d\n",
            G.zig_enable,G.rust_enable);
    fclose(f);
}

/* =========================================================================
 * Utilities
 * ========================================================================= */
#define MIN2(a,b) ((a)<(b)?(a):(b))
#define MAX2(a,b) ((a)>(b)?(a):(b))

static void win_fill(WINDOW *w, int rows, int cols) {
    wbkgd(w, COLOR_PAIR(C_BASE));
    for (int r = 0; r < rows; r++) mvwhline(w, r, 0, ' ', cols);
}

/*
 * Draw a grey box.
 * Title appears in the top border as:  [ Title ]
 */
static void draw_box(WINDOW *w, int rows, int cols, const char *title) {
    win_fill(w, rows, cols);
    wattron(w, COLOR_PAIR(C_BORDER) | A_BOLD);
    box(w, 0, 0);
    wattroff(w, COLOR_PAIR(C_BORDER) | A_BOLD);
    if (title && *title) {
        int tl = (int)strlen(title);
        int tx = (cols - tl - 4) / 2;
        if (tx < 1) tx = 1;
        wattron(w, COLOR_PAIR(C_BORDER) | A_BOLD);
        mvwprintw(w, 0, tx, "[ ");
        wattroff(w, COLOR_PAIR(C_BORDER) | A_BOLD);
        wattron(w, COLOR_PAIR(C_TITLE) | A_BOLD);
        wprintw(w, "%s", title);
        wattroff(w, COLOR_PAIR(C_TITLE) | A_BOLD);
        wattron(w, COLOR_PAIR(C_BORDER) | A_BOLD);
        wprintw(w, " ]");
        wattroff(w, COLOR_PAIR(C_BORDER) | A_BOLD);
    }
}



/* =========================================================================
 * Screen chrome  (header + hint bar)
 * ========================================================================= */
static void draw_chrome(int rs, int cs,
                         const char *menu_title, const char *status_msg) {
    /* Header: breadcrumb */
    attron(COLOR_PAIR(C_STATUS) | A_BOLD);
    mvhline(0, 0, ' ', cs);
    mvprintw(0, 2, "Ark Kernel Configuration");
    if (menu_title && *menu_title && strcmp(menu_title, "Ark Kernel Configuration") != 0)
        mvprintw(0, 28, " > %s", menu_title);
    attroff(COLOR_PAIR(C_STATUS) | A_BOLD);

    /* Status bar */
    attron(COLOR_PAIR(C_BASE));
    mvhline(rs-1, 0, ' ', cs);
    attroff(COLOR_PAIR(C_BASE));

    if (status_msg && *status_msg) {
        attron(COLOR_PAIR(C_STATUS) | A_BOLD);
        mvprintw(rs-1, 1, " %s ", status_msg);
        attroff(COLOR_PAIR(C_STATUS) | A_BOLD);
    }

    /* Right-aligned hints */
    attron(COLOR_PAIR(C_DIM));
    const char *hints = "F1=Help  F2=Presets  F5=Save  F6=Load  Tab=Buttons  Esc=Back";
    int hx = cs - (int)strlen(hints) - 1;
    if (hx > 0) mvprintw(rs-1, hx, "%s", hints);
    attroff(COLOR_PAIR(C_DIM));
}

/* =========================================================================
 * Button bar
 * ========================================================================= */
static const char *BTN_LABELS[] = {"Select","Exit","Help","Save","Load"};
#define N_BTNS   5
#define BTN_SELECT 0
#define BTN_EXIT   1
#define BTN_HELP   2
#define BTN_SAVE   3
#define BTN_LOAD   4

static void draw_buttons(int row, int cs, int focused) {
    int total = 0;
    for (int i = 0; i < N_BTNS; i++) total += (int)strlen(BTN_LABELS[i]) + 4 + 2;
    total -= 2;
    int x = (cs - total) / 2;
    if (x < 0) x = 0;

    attron(COLOR_PAIR(C_BASE));
    mvhline(row, 0, ' ', cs);
    attroff(COLOR_PAIR(C_BASE));

    for (int i = 0; i < N_BTNS; i++) {
        if (i == focused) attron(COLOR_PAIR(C_BTN_SEL) | A_BOLD);
        else              attron(COLOR_PAIR(C_BTN));
        mvprintw(row, x, "[ %s ]", BTN_LABELS[i]);
        if (i == focused) attroff(COLOR_PAIR(C_BTN_SEL) | A_BOLD);
        else              attroff(COLOR_PAIR(C_BTN));
        x += (int)strlen(BTN_LABELS[i]) + 4 + 2;
    }
}

/* =========================================================================
 * Help popup
 * ========================================================================= */
static void show_help(const char *title, const char *text) {
    int rs, cs; getmaxyx(stdscr, rs, cs);
    int w = MIN2(cs-6, 68), h = 13;
    WINDOW *p = newwin(h, w, (rs-h)/2, (cs-w)/2);
    keypad(p, TRUE);
    draw_box(p, h, w, title);
    wattron(p, COLOR_PAIR(C_BASE));
    int row = 2;
    const char *ptr = text;
    while (*ptr && row < h-3) {
        char line[160]; int nc=0;
        while (*ptr && *ptr!='\n' && nc < w-5) line[nc++]=*ptr++;
        line[nc]=0; if(*ptr=='\n')ptr++;
        mvwprintw(p, row++, 3, "%s", line);
    }
    wattroff(p, COLOR_PAIR(C_BASE));
    int bx = (w-9)/2;
    wattron(p, COLOR_PAIR(C_BTN_SEL)|A_BOLD);
    mvwprintw(p, h-2, bx, "[ Close ]");
    wattroff(p, COLOR_PAIR(C_BTN_SEL)|A_BOLD);
    wrefresh(p);
    wgetch(p);
    delwin(p); touchwin(stdscr); refresh();
}

/* =========================================================================
 * Input popup
 * ========================================================================= */
static void input_string(const char *prompt, char *buf, int buflen) {
    int rs, cs; getmaxyx(stdscr, rs, cs);
    int w = MIN2(cs-8, 60), h = 8;
    WINDOW *p = newwin(h, w, (rs-h)/2, (cs-w)/2);
    keypad(p, TRUE);
    draw_box(p, h, w, prompt);
    wattron(p, COLOR_PAIR(C_BASE));
    mvwprintw(p, 2, 3, "Current: %s", buf);
    mvwprintw(p, 3, 3, "New value (Enter=confirm  Esc=cancel):");
    wattroff(p, COLOR_PAIR(C_BASE));
    wattron(p, COLOR_PAIR(C_INPUT));
    mvwhline(p, 5, 3, ' ', w-6);
    wattroff(p, COLOR_PAIR(C_INPUT));
    wrefresh(p);
    echo(); curs_set(1);
    wmove(p, 5, 3);
    wattron(p, COLOR_PAIR(C_INPUT));
    char tmp[256]={0};
    wgetnstr(p, tmp, MIN2(buflen-1, w-7));
    wattroff(p, COLOR_PAIR(C_INPUT));
    noecho(); curs_set(0);
    if (tmp[0]) strncpy(buf, tmp, buflen-1);
    delwin(p); touchwin(stdscr); refresh();
}

static void input_int(const char *prompt, int *val) {
    char tmp[32]; snprintf(tmp,sizeof tmp,"%d",*val);
    input_string(prompt, tmp, sizeof tmp);
    *val = atoi(tmp);
}

/* =========================================================================
 * Radio-choice popup
 * ========================================================================= */
static int choice_popup(const char *title, const char **opts, int n, int cur) {
    int rs, cs; getmaxyx(stdscr, rs, cs);
    int w = MIN2(cs-10, 50), h = n+6;
    WINDOW *p = newwin(h, w, (rs-h)/2, (cs-w)/2);
    keypad(p, TRUE);
    int sel = cur;
    for (;;) {
        draw_box(p, h, w, title);
        for (int i = 0; i < n; i++) {
            int s = (i==sel);
            if (s) { wattron(p, COLOR_PAIR(C_SEL)|A_BOLD); }
            else   { wattron(p, COLOR_PAIR(C_BASE)); }
            mvwhline(p, i+2, 1, ' ', w-2);
            wmove(p, i+2, 3);
            if (s) wattron(p, COLOR_PAIR(C_TAG_SEL)|A_BOLD);
            else   wattron(p, COLOR_PAIR(C_TAG)|A_BOLD);
            wprintw(p, "(%c)", s?'*':' ');
            if (s) { wattroff(p, COLOR_PAIR(C_TAG_SEL)|A_BOLD); wattron(p, COLOR_PAIR(C_SEL)|A_BOLD); }
            else   { wattroff(p, COLOR_PAIR(C_TAG)|A_BOLD);     wattron(p, COLOR_PAIR(C_BASE)); }
            wprintw(p, " %-*s", w-10, opts[i]);
            if (s) wattroff(p, COLOR_PAIR(C_SEL)|A_BOLD);
            else   wattroff(p, COLOR_PAIR(C_BASE));
        }
        int bx = (w-22)/2;
        wattron(p, COLOR_PAIR(C_BTN_SEL)|A_BOLD);
        mvwprintw(p, h-2, bx, "[ Select ]");
        wattroff(p, COLOR_PAIR(C_BTN_SEL)|A_BOLD);
        wattron(p, COLOR_PAIR(C_BTN));
        mvwprintw(p, h-2, bx+12, "[ Cancel ]");
        wattroff(p, COLOR_PAIR(C_BTN));
        wrefresh(p);
        int ch = wgetch(p);
        if      (ch==KEY_UP)           sel=(sel-1+n)%n;
        else if (ch==KEY_DOWN)         sel=(sel+1)%n;
        else if (ch=='\n'||ch==' ')    { delwin(p); return sel; }
        else if (ch=='q'||ch==27)      { delwin(p); return cur; }
    }
}

/* =========================================================================
 * Yes/No confirm popup
 * ========================================================================= */
static int confirm(const char *msg) {
    int rs, cs; getmaxyx(stdscr, rs, cs);
    int w = MIN2(cs-8, 58), h = 7;
    WINDOW *p = newwin(h, w, (rs-h)/2, (cs-w)/2);
    keypad(p, TRUE);
    draw_box(p, h, w, "Confirm");
    wattron(p, COLOR_PAIR(C_BASE));
    mvwprintw(p, 2, 3, "%-*s", w-6, msg);
    wattroff(p, COLOR_PAIR(C_BASE));
    int sel = 1;
    for (;;) {
        int bx = (w-20)/2;
        if (sel==0) wattron(p, COLOR_PAIR(C_BTN_SEL)|A_BOLD);
        else        wattron(p, COLOR_PAIR(C_BTN));
        mvwprintw(p, h-2, bx, "[ Yes ]");
        if (sel==0) wattroff(p, COLOR_PAIR(C_BTN_SEL)|A_BOLD);
        else        wattroff(p, COLOR_PAIR(C_BTN));
        if (sel==1) wattron(p, COLOR_PAIR(C_BTN_SEL)|A_BOLD);
        else        wattron(p, COLOR_PAIR(C_BTN));
        mvwprintw(p, h-2, bx+9, "[ No ]");
        if (sel==1) wattroff(p, COLOR_PAIR(C_BTN_SEL)|A_BOLD);
        else        wattroff(p, COLOR_PAIR(C_BTN));
        wrefresh(p);
        int ch = wgetch(p);
        if      (ch==KEY_LEFT||ch==KEY_RIGHT||ch=='\t') sel^=1;
        else if (ch=='y'||ch=='Y') { delwin(p); return 1; }
        else if (ch=='n'||ch=='N') { delwin(p); return 0; }
        else if (ch=='\n')         { int r=(sel==0); delwin(p); return r; }
        else if (ch==27)           { delwin(p); return 0; }
    }
}

/* =========================================================================
 * Presets popup  (F2)
 * ========================================================================= */
static void presets_popup(void) {
    int rs, cs; getmaxyx(stdscr, rs, cs);
    int w = MIN2(cs-4, 66), h = 18;
    WINDOW *p = newwin(h, w, (rs-h)/2, (cs-w)/2);
    keypad(p, TRUE);

    static const char *names[] = {
        "defconfig    Recommended defaults (x86_64)",
        "tinyconfig   Minimal 32-bit footprint (x86)",
        "allyes       Enable every option",
        "allno        Bare minimum kernel",
    };
    static const char *descs[] = {
        "Balanced x86_64 config: networking, storage,\n"
        "USB, audio, scheduler, ELF loader all enabled.\n"
        "Good starting point for most projects.",

        "Stripped x86 32-bit config ideal for embedded\n"
        "or test targets.  Small heap, no networking,\n"
        "no scheduler, no USB.",

        "Every optional feature switched on.\n"
        "Useful as a starting point to disable things\n"
        "you don't need.",

        "All optional features off.\n"
        "Only printk, IDT, PIC, PMM, PCI and VFS\n"
        "remain.  Absolute bare minimum.",
    };
    int sel = 0, n = 4;
    for (;;) {
        draw_box(p, h, w, "Presets  (F2)");
        wattron(p, COLOR_PAIR(C_BASE));
        mvwprintw(p, 1, 3, "Select a preset  (arrow keys + Enter):");
        wattroff(p, COLOR_PAIR(C_BASE));

        for (int i = 0; i < n; i++) {
            int s = (i==sel);
            if (s) { wattron(p, COLOR_PAIR(C_SEL)|A_BOLD); }
            else   { wattron(p, COLOR_PAIR(C_BASE)); }
            mvwhline(p, i+3, 1, ' ', w-2);
            mvwprintw(p, i+3, 4, "%s", names[i]);
            if (s) wattroff(p, COLOR_PAIR(C_SEL)|A_BOLD);
            else   wattroff(p, COLOR_PAIR(C_BASE));
        }

        /* Divider + description */
        wattron(p, COLOR_PAIR(C_SEP)|A_BOLD);
        mvwhline(p, n+3, 1, ACS_HLINE, w-2);
        wattroff(p, COLOR_PAIR(C_SEP)|A_BOLD);

        for (int r = n+4; r < h-3; r++) {
            wattron(p, COLOR_PAIR(C_BASE));
            mvwhline(p, r, 1, ' ', w-2);
            wattroff(p, COLOR_PAIR(C_BASE));
        }
        wattron(p, COLOR_PAIR(C_DIM));
        const char *desc = descs[sel]; int drow = n+4;
        while (*desc && drow < h-3) {
            char line[128]; int nc=0;
            while (*desc && *desc!='\n' && nc < w-5) line[nc++]=*desc++;
            line[nc]=0; if(*desc=='\n')desc++;
            mvwprintw(p, drow++, 4, "%s", line);
        }
        wattroff(p, COLOR_PAIR(C_DIM));

        int bx = (w-24)/2;
        wattron(p, COLOR_PAIR(C_BTN_SEL)|A_BOLD);
        mvwprintw(p, h-2, bx, "[ Apply ]");
        wattroff(p, COLOR_PAIR(C_BTN_SEL)|A_BOLD);
        wattron(p, COLOR_PAIR(C_BTN));
        mvwprintw(p, h-2, bx+11, "[ Cancel ]");
        wattroff(p, COLOR_PAIR(C_BTN));
        wrefresh(p);

        int ch = wgetch(p);
        if      (ch==KEY_UP)           sel=(sel-1+n)%n;
        else if (ch==KEY_DOWN)         sel=(sel+1)%n;
        else if (ch=='\n'||ch==' ') {
            void (*fns[])(void) = {
                apply_defconfig, apply_tinyconfig, apply_allyes, apply_allno
            };
            fns[sel]();
            delwin(p); touchwin(stdscr); refresh();
            show_help("Preset Applied",
                      "Preset loaded successfully.\n\nUse F5 or [ Save ] to write to .kconfig.");
            return;
        }
        else if (ch=='q'||ch==27) { delwin(p); touchwin(stdscr); refresh(); return; }
    }
}

/* =========================================================================
 * Menu item descriptor
 * ========================================================================= */
typedef struct {
    int          type;   /* 0=bool 1=choice 2=int 3=str 4=sep 6=sub 7=comment */
    const char  *label;
    const char  *help;
    int         *bval;
    int         *ival;
    char        *sval;
    int          slen;
    const char **copts;
    int          cnum;
    void       (*subfn)(void);
} mi_t;

#define MB(l,f,h)     {0,l,h,&G.f,0,0,0,0,0,0}
#define MC(l,f,o,h)   {1,l,h,0,0,G.f,sizeof(G.f),o,(int)(sizeof(o)/sizeof(o[0])),0}
#define MI(l,f,h)     {2,l,h,0,&G.f,0,0,0,0,0}
#define MS(l,f,h)     {3,l,h,0,0,G.f,sizeof(G.f),0,0,0}
#define MSEP()        {4,"","",0,0,0,0,0,0,0}
#define MCOMMENT(l)   {7,l, "",0,0,0,0,0,0,0}
#define MSUB(l,fn,h)  {6,l,h,0,0,0,0,0,0,fn}

/* =========================================================================
 * Full screen repaint (used both live and for snapshot)
 * ========================================================================= */
static void repaint_full(WINDOW *box, int bh, int bw,
                          const char *title, mi_t *items, int n,
                          int top, int vis, int cur, int focus, int btn,
                          int rs, int cs, const char *status) {
    bkgd(COLOR_PAIR(C_BASE));
    erase();
    draw_chrome(rs, cs, title, status);
    draw_buttons(rs-2, cs, focus==1 ? btn : -1);

    draw_box(box, bh, bw, title);

    for (int i = 0; i < vis && top+i < n; i++) {
        int  idx = top+i, row = i+1;
        mi_t *m  = &items[idx];
        int  sel = (idx==cur) && (focus==0);
        int  iw  = bw - 8;

        if (sel) { wattron(box, COLOR_PAIR(C_SEL)|A_BOLD); mvwhline(box, row, 1, ' ', bw-2); }
        else     { wattron(box, COLOR_PAIR(C_BASE));        mvwhline(box, row, 1, ' ', bw-2); wattroff(box, COLOR_PAIR(C_BASE)); }

        wmove(box, row, 3);

        switch (m->type) {
        case 0: {
            int on = m->bval ? *m->bval : 0;
            /* opening [ */
            if (sel) wattron(box, COLOR_PAIR(C_TAG_SEL)|A_BOLD);
            else     wattron(box, COLOR_PAIR(C_TAG)|A_BOLD);
            wprintw(box, "[");
            /* fill char */
            if (sel) { wattroff(box, COLOR_PAIR(C_TAG_SEL)|A_BOLD); wattron(box, COLOR_PAIR(C_SEL)|A_BOLD); }
            else     { wattroff(box, COLOR_PAIR(C_TAG)|A_BOLD);     wattron(box, COLOR_PAIR(C_BASE)); }
            wprintw(box, "%c", on ? '*' : ' ');
            /* closing ] */
            if (sel) { wattroff(box, COLOR_PAIR(C_SEL)|A_BOLD);  wattron(box, COLOR_PAIR(C_TAG_SEL)|A_BOLD); }
            else     { wattroff(box, COLOR_PAIR(C_BASE));         wattron(box, COLOR_PAIR(C_TAG)|A_BOLD); }
            wprintw(box, "]");
            /* label */
            if (sel) { wattroff(box, COLOR_PAIR(C_TAG_SEL)|A_BOLD); wattron(box, COLOR_PAIR(C_SEL)|A_BOLD); }
            else     { wattroff(box, COLOR_PAIR(C_TAG)|A_BOLD);     wattron(box, COLOR_PAIR(C_BASE)); }
            wprintw(box, " %.*s", iw, m->label);
            if (sel) wattroff(box, COLOR_PAIR(C_SEL)|A_BOLD);
            else     wattroff(box, COLOR_PAIR(C_BASE));
            break;
        }
        case 1: case 3: {
            const char *v = m->sval ? m->sval : "";
            if (sel) wattron(box, COLOR_PAIR(C_SEL)|A_BOLD);
            else     wattron(box, COLOR_PAIR(C_BASE));
            wprintw(box, "(%s) %.*s", v, iw-(int)strlen(v)-3, m->label);
            if (sel) wattroff(box, COLOR_PAIR(C_SEL)|A_BOLD);
            else     wattroff(box, COLOR_PAIR(C_BASE));
            break;
        }
        case 2: {
            char ib[24]; snprintf(ib, sizeof ib, "(%d)", m->ival?*m->ival:0);
            if (sel) wattron(box, COLOR_PAIR(C_SEL)|A_BOLD);
            else     wattron(box, COLOR_PAIR(C_BASE));
            wprintw(box, "%s %.*s", ib, iw-(int)strlen(ib)-1, m->label);
            if (sel) wattroff(box, COLOR_PAIR(C_SEL)|A_BOLD);
            else     wattroff(box, COLOR_PAIR(C_BASE));
            break;
        }
        case 4:
            if (sel) wattroff(box, COLOR_PAIR(C_SEL)|A_BOLD);
            wattron(box, COLOR_PAIR(C_SEP));
            mvwhline(box, row, 2, ACS_HLINE, bw-4);
            wattroff(box, COLOR_PAIR(C_SEP));
            goto next_item;
        case 6: {
            int ll = (int)strlen(m->label);
            int pad = MAX2(1, iw - ll - 5);
            if (sel) wattron(box, COLOR_PAIR(C_SEL)|A_BOLD);
            else     wattron(box, COLOR_PAIR(C_ARROW)|A_BOLD);
            wprintw(box, "    %s%*s --->", m->label, pad, "");
            if (sel) wattroff(box, COLOR_PAIR(C_SEL)|A_BOLD);
            else     wattroff(box, COLOR_PAIR(C_ARROW)|A_BOLD);
            break;
        }
        case 7:
            if (sel) wattroff(box, COLOR_PAIR(C_SEL)|A_BOLD);
            wattron(box, COLOR_PAIR(C_HDR)|A_BOLD);
            mvwprintw(box, row, 4, "%.*s", bw-6, m->label);
            wattroff(box, COLOR_PAIR(C_HDR)|A_BOLD);
            goto next_item;
        }

        if (sel) wattroff(box, COLOR_PAIR(C_SEL)|A_BOLD);
        else     wattroff(box, COLOR_PAIR(C_BASE));
next_item:;
    }

    /* Scroll arrows */
    if (top > 0) {
        wattron(box, COLOR_PAIR(C_ARROW)|A_BOLD);
        mvwaddch(box, 1, bw-2, ACS_UARROW);
        wattroff(box, COLOR_PAIR(C_ARROW)|A_BOLD);
    }
    if (top+vis < n) {
        wattron(box, COLOR_PAIR(C_ARROW)|A_BOLD);
        mvwaddch(box, bh-2, bw-2, ACS_DARROW);
        wattroff(box, COLOR_PAIR(C_ARROW)|A_BOLD);
    }

    refresh();
    wrefresh(box);
}

/* =========================================================================
 * run_menu  — core scrollable list engine
 *
 * direction:  0 = no transition (root)
 *            +1 = slide from right (entering submenu)
 *            -1 = slide from left  (returning, handled by parent)
 * ========================================================================= */
static void run_menu(const char *title, mi_t *items, int n, int direction) {
    int rs, cs;
    getmaxyx(stdscr, rs, cs);

    int by=1, bx=1;
    int bh = rs-by-3; if(bh<4)bh=4;
    int bw = cs-2;
    int vis = bh-2;

    WINDOW *box = newwin(bh, bw, by, bx);
    keypad(box, TRUE);

    int top=0, cur=0, focus=0, btn=0, saved=0;
    while (cur<n && (items[cur].type==4||items[cur].type==7)) cur++;

    char status[128]="";

    for (;;) {
        getmaxyx(stdscr, rs, cs);
        bh = rs-by-3; if(bh<4)bh=4;
        vis = bh-2; bw = cs-2;
        wresize(box, bh, bw);
        mvwin(box, by, bx);

        repaint_full(box, bh, bw, title, items, n, top, vis, cur, focus, btn, rs, cs, status);

        int ch = wgetch(box);
        status[0]=0;

        /* Tab: toggle focus list <-> buttons */
        if (ch=='\t') {
            focus ^= 1;
            if (focus==1) btn=0;
            continue;
        }

        /* ── Button-bar focus ── */
        if (focus==1) {
            if      (ch==KEY_LEFT  || ch==KEY_UP)   btn=(btn-1+N_BTNS)%N_BTNS;
            else if (ch==KEY_RIGHT || ch==KEY_DOWN)  btn=(btn+1)%N_BTNS;
            else if (ch=='\n'||ch==' ') {
                switch (btn) {
                case BTN_SELECT: focus=0; goto activate_item;
                case BTN_EXIT:   goto do_exit;
                case BTN_HELP:
                    show_help(items[cur].help && items[cur].help[0] ? items[cur].label : "Help",
                              items[cur].help && items[cur].help[0] ? items[cur].help  : "No help text.");
                    break;
                case BTN_SAVE:
                    save_kconfig(); saved=1;
                    snprintf(status, sizeof status, "Saved to .kconfig");
                    break;
                case BTN_LOAD:
                    if (confirm("Reload .kconfig?  Unsaved changes will be lost."))
                        { load_kconfig(); saved=0; snprintf(status,sizeof status,"Loaded .kconfig"); }
                    break;
                }
            }
            else if (ch==27) focus=0;
            continue;
        }

        /* ── List focus ── */
        switch (ch) {
        case KEY_UP:
            do { cur=(cur-1+n)%n; }
            while (cur!=0 && (items[cur].type==4||items[cur].type==7));
            break;
        case KEY_DOWN:
            do { cur=(cur+1)%n; }
            while (cur!=n-1 && (items[cur].type==4||items[cur].type==7));
            break;
        case KEY_PPAGE: cur-=vis; if(cur<0)cur=0;       break;
        case KEY_NPAGE: cur+=vis; if(cur>=n)cur=n-1;    break;
        case KEY_HOME:
            cur=0; while(cur<n-1&&(items[cur].type==4||items[cur].type==7))cur++;
            break;
        case KEY_END:
            cur=n-1; while(cur>0&&(items[cur].type==4||items[cur].type==7))cur--;
            break;

        case 'y': case 'Y':
            if (items[cur].type==0 && items[cur].bval) *items[cur].bval=1;
            break;
        case 'n': case 'N':
            if (items[cur].type==0 && items[cur].bval) *items[cur].bval=0;
            break;

        case ' ': case '\n': case KEY_RIGHT:
activate_item: {
            mi_t *m = &items[cur];
            switch (m->type) {
            case 0: if(m->bval)*m->bval^=1; break;
            case 1: {
                int ci=0;
                for(int j=0;j<m->cnum;j++) if(!strcmp(m->copts[j],m->sval)){ci=j;break;}
                int ni = choice_popup(m->label, m->copts, m->cnum, ci);
                strncpy(m->sval, m->copts[ni], m->slen-1);
                break;
            }
            case 2: input_int(m->label, m->ival); break;
            case 3: input_string(m->label, m->sval, m->slen); break;
            case 6:
                if (m->subfn) {
                    delwin(box);
                    m->subfn();
                    getmaxyx(stdscr, rs, cs);
                    bh = rs-by-3; if(bh<4)bh=4; vis=bh-2; bw=cs-2;
                    box = newwin(bh, bw, by, bx);
                    keypad(box, TRUE);
                }
                break;
            }
            break;
        }

        case '?': case KEY_F(1):
            show_help(items[cur].help&&items[cur].help[0] ? items[cur].label : "Help",
                      items[cur].help&&items[cur].help[0] ? items[cur].help  : "No help text.");
            break;

        case KEY_F(2): case 'p': case 'P':
            presets_popup();
            break;

        case 's': case 'S': case KEY_F(5):
            save_kconfig(); saved=1;
            snprintf(status, sizeof status, "Saved to .kconfig");
            break;

        case 'l': case 'L': case KEY_F(6):
            if (confirm("Reload .kconfig?  Unsaved changes will be lost."))
                { load_kconfig(); saved=0; snprintf(status,sizeof status,"Loaded .kconfig"); }
            break;

        case KEY_LEFT:
        case 27:
        case 'q': case 'Q':
do_exit:
            if (!saved) {
                if (confirm("Exit without saving?")) { goto done; }
            } else { goto done; }
            break;
        }
        (void)saved;
    }
done:
    delwin(box);
    touchwin(stdscr); refresh();
}

/* =========================================================================
 * Submenus
 * ========================================================================= */
static void menu_build(void) {
    mi_t items[] = {
        MCOMMENT("Compiler"),
        MI("Optimisation level (0/1/2)", opt_level,     "GCC -O level.  0=off  1=basic  2=standard."),
        MB("Debug build (-g -O0)",       debug,         "Enable debug symbols, disable optimisation."),
        MB("Treat warnings as errors",   werror,        "Pass -Werror to GCC."),
        MSEP(),
        MCOMMENT("Userspace"),
        MB("Build userspace init",       build_init,    "Compile userspace/init.c via ark-gcc."),
        MS("Init binary path",           init_bin,      "Path inside initramfs to the init ELF."),
        MSEP(),
        MCOMMENT("Console"),
        MB("Enable printk",              printk_enable, "Kernel console output."),
        MB("Serial console (COM1)",      serial_enable, "Mirror printk to serial port."),
        MI("Serial base port (hex)",     serial_port,   "0x3F8=COM1, 0x2F8=COM2."),
        MI("Log level (0-7)",            loglevel,      "0=quiet  4=info  7=debug."),
    };
    run_menu("Build & Boot", items, (int)(sizeof items/sizeof items[0]), +1);
}

static void menu_memory(void) {
    mi_t items[] = {
        MB("Physical memory manager",  pmm_enable,    "Frame allocator for physical pages."),
        MB("Virtual memory manager",   vmm_enable,    "Paging / VMM layer."),
        MI("Heap size (KB)",           heap_size_kb,  "Kernel heap size in kilobytes."),
        MI("Stack size (KB)",          stack_size_kb, "Initial kernel stack size."),
    };
    run_menu("Memory", items, (int)(sizeof items/sizeof items[0]), +1);
}

static void menu_io(void) {
    mi_t items[] = {
        MCOMMENT("Port I/O"),
        MB("Port I/O helpers",       pio_enable,    "Enable inb/outb/inw/outw/inl/outl."),
        MB("MMIO helpers",           mmio_enable,   "Memory-mapped I/O helpers."),
        MSEP(),
        MCOMMENT("Interrupt controller"),
        MB("8259A PIC",              pic_enable,    "Legacy Programmable Interrupt Controller."),
        MB("I/O APIC",               ioapic_enable, "Advanced PIC for SMP systems."),
        MSEP(),
        MCOMMENT("IDT"),
        MB("IDT support",            idt_enable,    "Interrupt Descriptor Table — required."),
        MB("Dedicated IRQ stack",    irq_stack,     "Separate kernel stack for IRQ handlers."),
        MB("NMI handler",            nmi_enable,    "Non-Maskable Interrupt handler."),
    };
    run_menu("I/O and Interrupts", items, (int)(sizeof items/sizeof items[0]), +1);
}

static void menu_fb(void) {
    static const char *drv_opts[] = { "bga", "vesa", "vga" };
    mi_t items[] = {
        MB("Framebuffer enable",     fb_enable,  "Enable framebuffer driver."),
        MI("Width  (pixels)",        fb_width,   "Horizontal resolution."),
        MI("Height (pixels)",        fb_height,  "Vertical resolution."),
        MI("Bits per pixel",         fb_bpp,     "Colour depth: 16 or 32."),
        MC("FB driver",              fb_driver, drv_opts, "bga=Bochs VBE, vesa, vga."),
        MSEP(),
        MB("VESA graphics layer",    vesa_enable,"High-level VESA draw API."),
        MB("GPU subsystem",          gpu_enable, "GPU driver subsystem."),
    };
    run_menu("Framebuffer / GPU", items, (int)(sizeof items/sizeof items[0]), +1);
}

static void menu_storage(void) {
    mi_t items[] = {
        MCOMMENT("Block devices"),
        MB("ATA/IDE driver",   ata_enable,   "Classic ATA (PIO/DMA)."),
        MB("ATA DMA mode",     ata_dma,      "Use DMA for ATA (faster than PIO)."),
        MB("SATA/AHCI driver", sata_enable,  "AHCI SATA controller driver."),
        MB("SD card driver",   sd_enable,    "SD/MMC block device."),
        MSEP(),
        MCOMMENT("Filesystems"),
        MB("FAT32",            fat32_enable, "FAT32 read/write support."),
        MB("RAM filesystem",   ramfs_enable, "In-memory filesystem for initramfs."),
        MSEP(),
        MCOMMENT("AFS - Ark File System"),
        MB("AFS enable",       afs_enable,   "Ark File System: custom native FS, supports up to 512 GB disks. Probed at boot before FAT32. Requires VFS layer."),
        MB("AFS GPT support",  afs_gpt,      "Detect AFS on GPT-partitioned disks. Uses type GUID {A0A1A2A3-B0B1-C0C1-D0D1-E0E1E2E3E4E5}. GPT is probed first."),
        MB("AFS MBR support",  afs_mbr,      "Detect AFS on MBR-partitioned disks. Uses partition type byte 0xAF. Fallback when no GPT AFS partition is found."),
        MSEP(),
        MB("VFS layer",        vfs_enable,   "Virtual filesystem abstraction."),
        MB("ZIP initramfs",    zip_enable,   "Decompress ZIP initramfs on boot."),
    };
    run_menu("Storage", items, (int)(sizeof items/sizeof items[0]), +1);
}

static void menu_net(void) {
    mi_t items[] = {
        MB("Networking",       net_enable,   "Master networking enable."),
        MSEP(),
        MCOMMENT("NIC drivers"),
        MB("Intel e1000",      e1000_enable, "Intel Gigabit NIC."),
        MB("Intel e100",       e100_enable,  "Intel Fast Ethernet NIC."),
        MSEP(),
        MCOMMENT("Protocol stack"),
        MB("IP layer",         ip_enable,    "IPv4 packet handling."),
        MB("UDP",              udp_enable,   "UDP datagram sockets."),
        MB("TCP (stub)",       tcp_enable,   "TCP — not yet fully implemented."),
    };
    run_menu("Networking", items, (int)(sizeof items/sizeof items[0]), +1);
}

static void menu_usb(void) {
    mi_t items[] = {
        MB("USB subsystem",       usb_enable, "Master USB enable."),
        MSEP(),
        MCOMMENT("Host controllers"),
        MB("xHCI (USB 3/2)",      usb_xhci,  "xHCI — USB 3.0 + 2.0."),
        MB("EHCI (USB 2)",        usb_ehci,  "EHCI — USB 2.0."),
        MB("UHCI (USB 1.1)",      usb_uhci,  "UHCI — USB 1.1 legacy."),
        MSEP(),
        MB("USB HID (kbd/mouse)", usb_hid,   "USB Human Interface Device class."),
    };
    run_menu("USB", items, (int)(sizeof items/sizeof items[0]), +1);
}

static void menu_audio(void) {
    mi_t items[] = {
        MB("Audio subsystem",  audio_enable, "Master audio enable."),
        MB("AC97 codec",       ac97_enable,  "Intel AC97 audio codec driver."),
        MB("Intel HDA",        hda_enable,   "Intel High Definition Audio (stub)."),
    };
    run_menu("Audio", items, (int)(sizeof items/sizeof items[0]), +1);
}

static void menu_hid(void) {
    mi_t items[] = {
        MB("PS/2 keyboard",    kbd_enable,   "PS/2 / AT keyboard driver."),
        MB("PS/2 mouse",       mouse_enable, "PS/2 mouse driver."),
        MB("Touchscreen",      touch_enable, "Generic touchscreen input."),
    };
    run_menu("HID / Input", items, (int)(sizeof items/sizeof items[0]), +1);
}

static void menu_pci(void) {
    mi_t items[] = {
        MB("PCI subsystem",    pci_enable,    "PCI bus enumeration."),
        MB("Probe all devices",pci_probe_all, "Scan all PCI functions."),
    };
    run_menu("PCI", items, (int)(sizeof items/sizeof items[0]), +1);
}

static void menu_sched(void) {
    mi_t items[] = {
        MB("Enable scheduler",    sched_enable,       "Enable kernel round-robin task scheduler."),
        MB("Preemptive mode",     sched_preempt,      "Preempt tasks on timer IRQ (recommended)."),
        MI("Timeslice (ticks)",   sched_timeslice_ms, "CPU ticks per task before preemption."),
        MI("Max tasks",           sched_max_tasks,    "Maximum simultaneous tasks."),
        MI("Stack size (KB)",     sched_stack_kb,     "Per-task kernel stack size in KB."),
        MB("Job control",         sched_job_control,  "Enable block/unblock/kill job control API."),
    };
    run_menu("Scheduler / Job Control", items, (int)(sizeof items/sizeof items[0]), +1);
}

static void menu_debug(void) {
    mi_t items[] = {
        MB("Verbose debug output", debug_verbose,    "Extra debug messages at boot."),
        MB("Panic register dump",  debug_panic_dump, "Dump registers on kernel panic."),
        MB("KASAN (stub)",         debug_kasan,      "Kernel address sanitiser — stub only."),
        MI("Log level (0-7)",      loglevel,         "0=silent  4=info  7=all."),
    };
    run_menu("Debugging", items, (int)(sizeof items/sizeof items[0]), +1);
}

static void menu_lang(void) {
    mi_t items[] = {
        MCOMMENT("Zig"),
        MB("Enable Zig support",
           zig_enable,
           "Compile zig/src/lib.zig into the kernel.\n"
           "\n"
           "Provides the buddy-system block scanner (ark_bscan_*)\n"
           "on top of the C bitmap PMM.  Requires 'zig' in PATH.\n"
           "\n"
           "Output: compiled/zig/ark_zig.o linked into ArkImage.\n"
           "Source: zig/src/lib.zig\n"
           "Header: zig/lib.h\n"
           "\n"
           "Works on both ARCH=x86 and ARCH=x86_64.\n"
           "Target: x86-freestanding-none / x86_64-freestanding-none."),
        MSEP(),
        MCOMMENT("Rust"),
        MB("Enable Rust support",
           rust_enable,
           "Compile rust/src/lib.rs into the kernel.\n"
           "\n"
           "Provides the USB keyboard HID driver written in Rust\n"
           "(ark-usb-kbd crate).  Requires the Rust nightly toolchain\n"
           "and the i686-ark-none / x86_64-ark-none target JSON.\n"
           "\n"
           "Output: compiled/rust/rust_usb_kbd.o\n"
           "Source: rust/src/lib.rs\n"
           "\n"
           "Note: Rust ELF64 objects cannot link into a 32-bit (x86)\n"
           "kernel.  On ARCH=x86 this option is silently ignored and\n"
           "the C USB keyboard driver is used instead."),
    };
    run_menu("Language Runtimes", items,
             (int)(sizeof items / sizeof items[0]), +1);
}


static const char *arch_opts[] = { "x86", "x86_64" };

static void top_menu(void) {
    mi_t items[] = {
        MCOMMENT("General"),
        MC("Target architecture",       arch,     arch_opts, "x86 = 32-bit  x86_64 = 64-bit."),
        MS("Kernel codename",           codename,            "Human-readable build label."),
        MSEP(),
        MCOMMENT("Subsystems"),
        MSUB("Build & Boot",            menu_build,    "Compiler flags, init binary, serial console."),
        MSUB("Memory",                  menu_memory,   "PMM, VMM, heap and stack sizes."),
        MSUB("I/O and Interrupts",      menu_io,       "Port I/O, APIC, IDT, NMI."),
        MSUB("Framebuffer / GPU",       menu_fb,       "Display driver and resolution."),
        MSUB("Storage",                 menu_storage,  "ATA, SATA, FAT32, AFS, VFS."),
        MSUB("Networking",              menu_net,      "NIC drivers, IP, UDP, TCP."),
        MSUB("USB",                     menu_usb,      "xHCI, EHCI, UHCI, HID."),
        MSUB("Audio",                   menu_audio,    "AC97, Intel HDA."),
        MSUB("HID / Input",             menu_hid,      "Keyboard, mouse, touch."),
        MSUB("PCI",                     menu_pci,      "PCI enumeration."),
        MSUB("Scheduler / Job Control", menu_sched,    "Preemptive scheduler, job control."),
        MSEP(),
        MCOMMENT("Kernel features"),
        MB("Syscall interface",         syscall_enable,"System call entry (int 0x80 / SYSCALL)."),
        MB("ELF loader",                elf_loader,    "Load and execute ELF binaries."),
        MSUB("Debugging",               menu_debug,    "Verbose logs, panic dump, KASAN."),
        MSEP(),
        MCOMMENT("Language Runtimes"),
        MSUB("Language Runtimes",       menu_lang,     "Enable/disable Zig and Rust kernel modules."),
    };
    run_menu("Ark Kernel Configuration", items,
             (int)(sizeof items/sizeof items[0]), 0);
}

/* =========================================================================
 * main
 * ========================================================================= */
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
    cbreak(); noecho();
    keypad(stdscr, TRUE);
    curs_set(0);
    init_colours();

    bkgd(COLOR_PAIR(C_BASE));
    clear(); refresh();

    top_menu();

    if (confirm("Save configuration before exiting?"))
        save_kconfig();

    endwin();
    return 0;
}