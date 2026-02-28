/*
 * arch/x86_64/linker64.ld — Ark kernel x86_64 linker script
 */

OUTPUT_FORMAT("elf64-x86-64")
OUTPUT_ARCH(i386:x86-64)
ENTRY(_start)

SECTIONS
{
    /*
     * Load at 1 MiB — standard physical address for multiboot / QEMU -kernel.
     * The 32-bit bootstrap uses absolute physical addresses before paging
     * is active, so everything must actually reside where it is linked.
     */
    . = 1M;

    _kernel_start = .;

    /* PVH note — QEMU -kernel scans PT_NOTE for this */
    .note.Xen : {
        KEEP(*(.note.Xen))
    }

    /* Multiboot2 header — must be within first 32 KiB of the image */
    .multiboot : {
        . = ALIGN(8);
        KEEP(*(.multiboot))
    }

    /* 32-bit boot trampoline */
    .text.boot : {
        *(.text.boot)
    }

    /* 64-bit kernel code */
    .text : {
        *(.text*)
    }

    . = ALIGN(4096);
    .rodata : { *(.rodata*) }

    . = ALIGN(4096);
    .data   : { *(.data*)   }

    . = ALIGN(4096);
    .bss : {
        *(.bss*)
        *(COMMON)
    }

    . = ALIGN(4096);
    _kernel_end = .;

    /DISCARD/ : {
        *(.comment)
        *(.note.gnu.build-id)
        *(.note.GNU-stack)
    }
}
