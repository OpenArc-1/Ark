/**
 * x86 Multiboot entry glue for Ark.
 *
 * Multiboot header and entry point are defined in C so we do not
 * depend on any assembly for boot. This keeps the bring-up simple.
 */

__attribute__((section(".multiboot"), used))
static const unsigned int multiboot_header[] = {
    0x1BADB002,                 /* magic */
    0x00000000,                 /* flags: none (no extra requirements) */
    -(0x1BADB002 + 0x00000000)  /* checksum */
};

#include "ark/types.h"
#include "ark/multiboot.h"
#include "ark/printk.h"
#include "ark/panic.h"

void kernel_main(void);

/* ELF entrypoint for the kernel. A Multiboot loader will still set
 * EAX/EBX per spec before jumping here, but we currently ignore them
 * and just run the generic kernel main.
 */
void _start(void) {
    kernel_main();
    /* If kernel_main ever returns, halt the CPU. */
    for (;;) {
        __asm__ __volatile__("hlt");
    }
}

static void arch_setup_framebuffer(multiboot_info_t *mbi) {
    (void)mbi; /* Framebuffer setup is not used in the VGA-only path. */
}

void arch_x86_entry(u32 magic, u32 mb_info) {
    if (magic != MULTIBOOT_BOOTLOADER_MAGIC) {
        /* We were not booted by a Multiboot-compliant loader. */
        kernel_panic("Bad multiboot magic");
    }

    multiboot_info_t *mbi = (multiboot_info_t *)(u32)mb_info;

    arch_setup_framebuffer(mbi);

    printk("Ark kernel starting (x86, linear framebuffer)...\n");
    printk("Framebuffer: %ux%u @ 0x%x, %u bpp, pitch %u\n",
           (unsigned)mbi->framebuffer_width,
           (unsigned)mbi->framebuffer_height,
           (unsigned)mbi->framebuffer_addr,
           (unsigned)mbi->framebuffer_bpp,
           (unsigned)mbi->framebuffer_pitch);

    kernel_main();

    kernel_panic("kernel_main returned");
}

