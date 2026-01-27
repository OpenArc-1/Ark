/**
* Core kernel boot flow for Ark.
*
 * This is architecture-agnostic high-level logic that is entered
 * from arch-specific code (e.g. arch_x86_64_entry).
 */

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
<<<<<<< HEAD
=======
#include "ark/elf_loader.h"
#include "ark/userspacebuf.h"
#include "ark/script.h"
#include "ark/time.h"
#include "ark/pci.h"
#include "ark/init_api.h"
>>>>>>> 1a209df (Removed unnecessary userspace files and added current project)
#include "../mp/built-in.h"

extern void show_sysinfo_bios(void);
extern void clear_screen(void);
extern void usb_init(void);
extern void e1000_init(void);
extern void ip_init(void);
extern void ip_poll(void);
extern void fs_built_in_init(void);
extern void fb_init(const ark_fb_info_t *info);
extern void serial_init(void);
<<<<<<< HEAD
extern void ramfs_init(void);
extern void ramfs_mount(void);
extern u8 ramfs_has_init(void);
extern ark_fb_info_t g_fb_info;  /* Framebuffer info from bootloader */
=======
extern void idt_init(void);  /* IDT initialization for syscalls */
extern void ramfs_init(void);
extern void ramfs_mount(void);
extern u8 ramfs_has_init(void);
extern u8 *ramfs_get_init(u32 *out_size);
extern ark_fb_info_t g_fb_info;  /* Framebuffer info from bootloader */
extern uspace_buffer_t g_uspace_buffer;  /* Shared userspace output buffer */
>>>>>>> 1a209df (Removed unnecessary userspace files and added current project)
/* Forward declarations for future subsystems. */
u8 fs_has_init(void);
void fs_mount_root(void);
void input_init(void);  /* Input subsystem manager */
void input_poll(void);  /* Poll input devices */
<<<<<<< HEAD
=======
void scanAll(void); /*this is for the pci devices*/

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
>>>>>>> 1a209df (Removed unnecessary userspace files and added current project)

static void busy_delay(u32 loops) {
    for (volatile u32 i = 0; i < loops; ++i) {
        __asm__ __volatile__("");
    }
}

static void wait_for_init_bin(void) {
    /* Check if init.bin was provided by bootloader as a module.
<<<<<<< HEAD
     * If modules were loaded into ramfs, fs_has_init() will return true immediately.
     */
    if (fs_has_init()) {
        printk("  [    0.100000] ark-init: found /init.bin in ramfs\n");
        return;
    }

    printk("[    0.100000] ark-init: /init.bin not found in ramfs\n");
    printk("[    0.110000] ark-init: waiting for init.bin...\n");
    
    /* Try waiting a bit in case modules are still being loaded */
    for (int attempt = 1; attempt <= 3; ++attempt) {
        printk("[    0.120000] ark-init: retrying (%d/3)...\n", attempt);
        busy_delay(50000000);
        if (fs_has_init()) {
            printk("[    0.150000] ark-init: found /init.bin, starting userspace\n");
            return;
        }
    }

    printk("[    0.200000] ark-init: /init.bin not found after retries\n");
    printk("[    0.210000] ark-init: To load init.bin, run: make run-with-init\n");
    kernel_panic("init.bin not found");
}

void kernel_main(void) {
=======
     * The modules should have been loaded into ramfs by modules_load_from_multiboot()
     * in arch_x86_entry before kernel_main was called.
     */
    if (fs_has_init()) {
        printk("[    0.000310] ark-init: Found /init.bin in ramfs (loaded by bootloader)\n");
        return;
    }

    printk("[    0.000310] ark-init: /init.bin not found in ramfs\n");
    printk("[    0.000320] ark-init: Attempting to load from FAT32 filesystem...\n");
    
    /* Try reading from FAT32 disk image - would need FAT32 driver integration */
    /* For now, this is a placeholder for filesystem-based loading */
}

void kernel_main(void) {
    printk("[    0.000000][boot] Ark kernel booting on x86\n");
    busy_delay(40000000);
    u8 script_found = 0;  /* Track if script was found and executed */
    
>>>>>>> 1a209df (Removed unnecessary userspace files and added current project)
    fb_init(&g_fb_info);
    serial_init();
    clear_screen();
    busy_delay(20000000);
<<<<<<< HEAD
    printk("\n");
    printk("[    0.000000] ========================================\n");
    printk("[    0.000000] Ark kernel booting on x86\n");
    printk("[    0.000000] ========================================\n");
=======
    
    /* Initialize IDT for int 0x80 syscalls */
    idt_init();
    scanAll();
    
>>>>>>> 1a209df (Removed unnecessary userspace files and added current project)
    printk("[    0.000001] Boot params: stub (no cmdline yet)\n");
    printk("[    0.000010] Framebuffer: initialized\n");
    printk("[    0.000020] Serial console: initialized\n");
    
    printk("[    0.000100] Initialising core subsystems...\n");
    /* Input subsystem */
    printk("[    0.000110] Initializing input subsystem...\n");
    input_init();
    printk("[    0.000120] Input subsystem: OK\n");
    busy_delay(20000000);
    /*bios subsystem*/
    printk("[    0.000125] Initializing BIOS subsystem...\n");
    show_sysinfo_bios();
    busy_delay(20000000);
<<<<<<< HEAD
    /* USB subsystem */
    printk("[    0.000130] Initializing USB subsystem...\n");
    usb_init();
=======
    printk("[time] ");
    rtc_time_t t = read_rtc();
    printk("%02d:%02d:%02d\n", t.hour, t.min, t.sec);
    busy_delay(20000000);
    /* USB subsystem */
    printk("[    0.000130] Initializing USB subsystem...\n");
    usb_init();
    busy_delay(20000000);
    scan_usb_controllers();
>>>>>>> 1a209df (Removed unnecessary userspace files and added current project)
    printk("[    0.000140] USB subsystem: OK\n");
    busy_delay(20000000);
    
    /* Network driver */
    printk("[    0.000150] Probing network device (e1000)...\n");
    e1000_init();
    printk("[    0.000155] Network driver: OK\n");
    busy_delay(20000000);
    
    /* IP stack */
    printk("[    0.000160] Initializing IP networking stack...\n");
    ip_init();
    printk("[    0.000165] IP networking: OK\n");
    busy_delay(20000000);
    
    /* Filesystem and storage */
    printk("[    0.000170] Initializing storage drivers...\n");
    fs_built_in_init();
    printk("[    0.000180] Storage drivers: OK\n");
    busy_delay(20000000);
    
    /* RAM filesystem */
    printk("[    0.000190] Mounting root filesystem (ramfs)...\n");
<<<<<<< HEAD
    ramfs_init();
    printk("[    0.000195] RAM filesystem: initialized\n");
    fs_mount_root();
    printk("[    0.000200] Root filesystem: mounted\n");
    busy_delay(20000000);

    /* Probe for init binary */
    printk("[    0.000300] Probing for /init.bin\n");
    wait_for_init_bin();

    /* If fs_has_init ever returns true, we would "launch" init here. */
    printk("[    0.600000] Launching init (stub)...\n");
    printk("[    0.670000] Launching init (blob)...\n");
=======
    /* NOTE: ramfs is already initialized by modules_load_from_multiboot() in arch_x86_entry
     * Do NOT call ramfs_init() here as it would clear the loaded modules! */
    fs_mount_root();
    printk("[    0.000200] Root filesystem: mounted with loaded modules\n");
    
    /* List files for debugging */
    extern void ramfs_list_files(void);
    ramfs_list_files();
    busy_delay(20000000);

    /* Scan for #!init scripts first (new script-based init system) */
    printk("[    0.000250] Scanning for #!init scripts in ramfs...\n");
    script_found = script_scan_and_execute();
    busy_delay(20000000);
    
    if (script_found) {
        printk("[    0.000260] Init script executed successfully\n");
        /* Script execution completed, continue to normal flow or halt */
        goto script_done;
    } else {
        printk("[    0.000260] No #!init scripts found, falling back to /init.bin\n");
    }

    /* Probe for init binary (fallback to traditional method) */
    printk("[    0.000300] Probing for /init.bin in ramfs\n");
    wait_for_init_bin();

    /* Check if init.bin was found */
    if (!fs_has_init()) {
        printk("[    0.500000] No init.bin loaded - continuing to kernel idle loop\n");
        printk("[    0.600000] Kernel will now poll input devices and network\n");
    } else {
        printk("[    0.500000] init.bin found in ramfs!\n");
        printk("[    0.510000] Ready to execute userspace init\n");
        /* In a full implementation, we would execute init.bin here:
         * - Switch to ring 3 (user mode)
         * - Jump to init.bin entry point (_entry or init_usp function)
         * - Pass control to userspace
         * When init.bin exits, return here and panic
         */
    }
>>>>>>> 1a209df (Removed unnecessary userspace files and added current project)
    
    /* Poll input devices while waiting */
    printk("[    0.700000] Polling input devices...\n");
    input_poll();
    
    /* Poll network for incoming packets */
    printk("[    0.710000] Polling network for packets...\n");
    for (int i = 0; i < 100; i++) {
        ip_poll();
        busy_delay(1000000);
    }
    
<<<<<<< HEAD
    busy_delay(20000000);
    printk("[    0.800000] FATAL: Reached end of kernel_main without userspace init\n");
    kernel_panic("init.bin not loaded");
=======
    /* Now attempt to execute init.bin if it was found */
    if (fs_has_init()) {
        u32 init_size = 0;
        u8 *init_data = ramfs_get_init(&init_size);
        
        if (init_data && init_size > 0) {
            printk("[    0.800000] Executing /init.bin from ramfs\n");
            printk("[    0.810000] Binary size: %u bytes\n", init_size);
            printk("[    0.820000] \n");
            
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
            printk("[    2.000000] init.bin returned with exit code: %d\n", exit_code);
            printk("[    2.100000] System shutting down...\n");
            busy_delay(10000000);
        } else {
            printk("[    0.800000] ERROR: init.bin found but data is invalid\n");
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
        printk("[    1.000000] Kernel panic - init.bin execution failed or not loaded\n");
        printk("[    1.000000] init.bin execution failed or not loaded\n");
        
        if (fs_has_init()) {
            printk("[    1.100000] init.bin was found but execution failed\n");
            printk("[    1.100000] OR init.bin exited unexpectedly\n");
        } else {
            printk("[    1.100000] init.bin was NOT loaded into ramfs\n");
            printk("[    1.110000] Use: make run-with-init\n");
            printk("[    1.110000] OR: make run-disk-with-fs\n");
            printk("[    1.120000] OR: Load a script with #!init tag\n");
        }
        
        printk("[    1.200000] System halting\n");
        printk("[    1.300000] \n");
        busy_delay(20000000);
        kernel_panic("init.bin execution failed or not loaded");
    } else {
        printk("\n");
        printk("[    1.000000] Init script execution complete\n");
        printk("[    1.100000] System will continue in idle loop\n");
        busy_delay(20000000);
    }
>>>>>>> 1a209df (Removed unnecessary userspace files and added current project)
}

/* Filesystem implementation using ramfs for loading init.bin */
u8 fs_has_init(void) {
    return ramfs_has_init();
}

void fs_mount_root(void) {
    ramfs_mount();
}

