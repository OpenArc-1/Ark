ENTRY(_start)

SECTIONS
{
    /* Load kernel at 4 MiB. */
    . = 4M;

    /* Create a symbol at the very start of the kernel */
    _kernel_start = .;

    /* Read + Execute section */
    .multiboot :
    {
        *(.multiboot)
    }

    .text :
    {
        *(.text*)
    }

    /* Align to page boundary before transitioning to read-only section */
    . = ALIGN(4096);

    /* Read-only section */
    .rodata :
    {
        *(.rodata*)
    }

    /* Align to page boundary before transitioning to read-write section */
    . = ALIGN(4096);

    /* Read + Write sections */
    .data :
    {
        *(.data*)
    }

    .bss :
    {
        *(.bss*)
        *(COMMON)
    }

    /* Ensure the end is aligned to a page boundary (4KB) */
    . = ALIGN(4096);
    _kernel_end = .;
}
