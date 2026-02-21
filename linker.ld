ENTRY(_start)

SECTIONS
{
    /* Load kernel at 4 MiB. */
    . = 2M;

    /* Create a symbol at the very start of the kernel */
    _kernel_start = .;

    .multiboot :
    {
        *(.multiboot)
    }

    .text :
    {
        *(.text*)
    }

    .rodata :
    {
        *(.rodata*)
    }

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