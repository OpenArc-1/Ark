ENTRY(_start)

SECTIONS
{
  /* Load low; kernel ELF loader copies segments to paddr */
  . = 0x1000;

  .text : ALIGN(16) {
    *(.text*)
  }

  .rodata : ALIGN(16) {
    *(.rodata*)
  }

  .data : ALIGN(16) {
    *(.data*)
  }

  .bss : ALIGN(16) {
    *(COMMON)
    *(.bss*)
  }
}

