ENTRY(_start)

SECTIONS
{
  /* Load kernel at 1 MiB. */
  . = 1M;

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
}

