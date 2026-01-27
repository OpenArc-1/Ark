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

PHDRS {
  text PT_LOAD FLAGS(4);     /* R | X */
  rodata PT_LOAD FLAGS(4);   /* R     */
  data PT_LOAD FLAGS(6);     /* R | W */
}

