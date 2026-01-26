/* Linker script for userspace init.bin
 * 
 * This binary runs as a module loaded by the bootloader/kernel.
 * It should NOT assume a specific load address.
 * We link starting at address 0x0, so entry point will be 0x0 + offset of init_usp
 */

ENTRY(init_usp)

SECTIONS
{
    /* Start at offset 0 within the loaded binary */
    . = 0x0;
    
    .text : {
        *(.text.init_usp)
        *(.text*)
    }
    
    .rodata : {
        *(.rodata*)
    }
    
    .data : {
        *(.data*)
    }
    
    .bss : {
        *(.bss*)
        *(COMMON)
    }
}

