/**
* Core kernel boot flow for Ark.
*
 * This is architecture-agnostic high-level logic that is entered
 * from arch-specific code (e.g. arch_x86_64_entry).
 */
#include "hw/vendor.h"//this is the new cpu vendor check
#include "ark/types.h"
#include "ark/printk.h"
#include "ark/panic.h"
#include "ark/clear.h"
#include "ark/usb.h"
#include "ark/e1000.h"
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

extern void ac97_test();
extern void ac97_init();// this is the new sound card
extern void show_sysinfo_bios(void);
extern void clear_screen(void);
extern void usb_init(void);
extern void e1000_init(void);
extern void ip_init(void);
extern void ip_poll(void);
extern void fs_built_in_init(void);
extern void fb_init(const ark_fb_info_t *info);
extern void serial_init(void);
extern void idt_init(void);  /* IDT initialization for syscalls */
extern void ramfs_init(void);
extern void ramfs_mount(void);
extern u8 ramfs_has_init(void);
extern u8 *ramfs_get_init(u32 *out_size);
extern ark_fb_info_t g_fb_info;  /* Framebuffer info from bootloader */
extern uspace_buffer_t g_uspace_buffer;  /* Shared userspace output buffer */
/* Forward declarations for future subsystems. */
u8 fs_has_init(void);
void fs_mount_root(void);
void input_init(void);  /* Input subsystem manager */
void input_poll(void);  /* Poll input devices */
void scanAll(void); /*this is for the pci devices*/
void cpu_verify();//cpu check
void mem_verify();//memory check
/* Kernel API table is provided by gen/init_api.c */


/**
 * Read and display any output from userspace buffer
 */
static void print_userspace_output(void) {
    while (g_uspace_buffer.read_pos < g_uspace_buffer.write_pos) {
        u32 pos = g_uspace_buffer.read_pos % USP_BUFFER_SIZE;
        printk("%c", g_uspace_buffer.buffer[pos]);
        g_uspace_buffer.read_pos++;
    }
}

static void busy_delay(u32 loops) {
    for (volatile u32 i = 0; i < loops; ++i) {
        __asm__ __volatile__("");
    }
}

static void wait_for_init_bin(void) {
    /* Check if init.bin was provided by bootloader as a module.
     * The modules should have been loaded into ramfs by modules_load_from_multiboot()
     * in arch_x86_entry before kernel_main was called.
     */
    if (fs_has_init()) {
        printk(":: ark-init: Found /init.bin in ramfs (loaded by bootloader)\n");
        return;
    }

    printk(":: ark-init: /init.bin not found in ramfs\n");
    printk(":: ark-init: Attempting to load from FAT32 filesystem...\n");
    
    /* Try reading from FAT32 disk image - would need FAT32 driver integration */
    /* For now, this is a placeholder for filesystem-based loading */
}

void kernel_main(void) {
    clear_screen();
    cpu_verify();
    mem_verify();
    printk("::[boot] Ark kernel booting on x86\n");
    busy_delay(40000000);
    u8 script_found = 0;  /* Track if script was found and executed */
    
    fb_init(&g_fb_info);
    serial_init();
    busy_delay(20000000);
    sys_info(); 
    /* Initialize IDT for int 0x80 syscalls */
    idt_init();
    scanAll();
    ac97_init();//init in kernel.no degug text fro now 
    ac97_test();
    printk(":: Boot params: stub (no cmdline yet)\n");
    printk(":: Framebuffer: initialized\n");
    printk(":: Serial console: initialized\n");
    
    printk(":: Initialising core subsystems...\n");
    /* Input subsystem */
    printk(":: Initializing input subsystem...\n");
    input_init();
    printk(":: Input subsystem: OK\n");
    busy_delay(20000000);
    printk("::[bios] Initializing BIOS subsystem...\n");
    show_sysinfo_bios();
    busy_delay(20000000);
    printk("[time] ");
    rtc_time_t t = read_rtc();
    printk("%02d:%02d:%02d\n", t.hour, t.min, t.sec);
    busy_delay(20000000);
    /* USB subsystem */
    printk("::[usb] Initializing USB subsystem...\n");
    usb_init();
    busy_delay(20000000);
    scan_usb_controllers();
    printk("[usb] USB subsystem: OK\n");
    busy_delay(20000000);
    
    /* Network driver */
    printk("::[e1x] Probing network device (e1000)...\n");
    e1000_init();
    printk(" [+] Network driver: OK\n");
    busy_delay(20000000);
    
    /* IP stack */
    printk("::[ip] Initializing IP networking stack...\n");
    ip_init();
    printk(" [+] IP networking: OK\n");
    busy_delay(20000000);
    
    /* Filesystem and storage */
    printk("::[sata/ata] Initializing storage drivers...\n");
    fs_built_in_init();
    printk(" [+] Storage drivers: OK\n");
    busy_delay(20000000);
    
    /* RAM filesystem */
    printk("::[rootfs] Mounting root filesystem (ramfs)...\n");
    /* NOTE: ramfs is already initialized by modules_load_from_multiboot() in arch_x86_entry
     * Do NOT call ramfs_init() here as it would clear the loaded modules! */
    fs_mount_root();
    printk(" [+] Root filesystem: mounted with loaded modules\n");
    
    /* List files for debugging */
    extern void ramfs_list_files(void);
    ramfs_list_files();
    busy_delay(20000000);

    /* Scan for #!init scripts first (new script-based init system) */
    printk("[fs][script] Scanning for #!init scripts in ramfs...\n");
    script_found = script_scan_and_execute();
    busy_delay(20000000);
    
    if (script_found) {
        printk("[script][+] Init script executed successfully\n");
        /* Script execution completed, continue to normal flow or halt */
        goto script_done;
    } else {
        printk(" [+] No #!init scripts found, falling back to /init\n");
    }

    /* Probe for init binary (fallback to traditional method) */
    printk("[...] Probing for /init in ramfs\n");
    wait_for_init_bin();

    /* Check if init.bin was found */
    if (!fs_has_init()) {
        printk(" [!] No init loaded - continuing to kernel idle loop\n");
        printk(" [!] Kernel will now poll input devices and network\n");
    } else {
        printk(" [+] init.bin found in ramfs!\n");
        printk("[ok] Ready to execute userspace init\n");
        /* In a full implementation, we would execute init.bin here:
         * - Switch to ring 3 (user mode)
         * - Jump to init.bin entry point (_entry or init_usp function)
         * - Pass control to userspace
         * When init.bin exits, return here and panic
         */
    }
    
    /* Poll input devices while waiting */
    printk(" [+] Polling input devices...\n");
    input_poll();
    
    /* Poll network for incoming packets */
    printk(" [-] Polling network for packets...\n");
    for (int i = 0; i < 100; i++) {
        ip_poll();
        busy_delay(1000000);
    }
    
    /* Now attempt to execute init.bin if it was found */
    if (fs_has_init()) {
        u32 init_size = 0;
        u8 *init_data = ramfs_get_init(&init_size);
        
        if (init_data && init_size > 0) {
            printk("[elf] Executing /init from ramfs\n");
            printk(" [+] Binary size: %u bytes\n", init_size);
            printk("\n");
            
            /* Reset output buffer before execution */
            g_uspace_buffer.read_pos = 0;
            g_uspace_buffer.write_pos = 0;
            g_uspace_buffer.activity_flag = 0;
            
            /* Execute the ELF binary (init.bin expects an API table) */
            int exit_code = elf_execute(init_data, init_size, ark_kernel_api());
            
            /* Check if shell loop ran  */
            if (g_uspace_buffer.activity_flag || g_uspace_buffer.write_pos > 0) {
                printk("[userspace] userspace executed and wrote output\n");
                print_userspace_output();
            } else {
                printk("[userspace] userspace executed successfully (no output capability yet)\n");
            }
            
            printk("\n");
            printk("[userspace] init.bin returned with exit code: %d\n", exit_code);
            printk(":: System shutting down...\n");
            busy_delay(10000000);
        } else {
            printk("[elf-read] ERROR: init.bin found but data is invalid\n");
        }
    }
    
script_done:
    /* If we reach here, init.bin either:
     * 1. Was not found
     * 2. Was not executable 
     * 3. Returned/exited
     * 4. Or a script was executed successfully
     * Either way, this is a system failure condition (unless script succeeded)
     */
    if (!script_found) {
        printk("\n");
        printk(":: Kernel panic - init execution failed or not loaded\n");
        printk("[init] init execution failed or not loaded\n");
        
        if (fs_has_init()) {
            printk("[elf-read] init was found but execution failed\n");
            printk("[elf-read] OR init.bin exited unexpectedly\n");
        } else {
            printk("[elf-read] init was NOT loaded into ramfs\n");
            printk("[elf-read] Use: make run-with-init\n");
            printk("[elf-read] OR: make run-disk-with-fs\n");
            printk("[elf-read] OR: Load a script with #!init tag\n");
        }
        
        printk(":: System halting\n");
        printk(":: \n");
        busy_delay(20000000);
        kernel_panic("init.bin execution failed or not loaded");
    } else {
        printk("\n");
        printk("[script] Init script execution complete\n");
        printk("[script] System will continue in idle loop\n");
        busy_delay(20000000);
    }
}

/* Filesystem implementation using ramfs for loading init.bin */
u8 fs_has_init(void) {
    return ramfs_has_init();
}

void fs_mount_root(void) {
    ramfs_mount();
}

void sys_info(){
	printk("kernel: %s\n", K_ID);
	//printk();
	printk("build: %s\n", BUILD);

}
