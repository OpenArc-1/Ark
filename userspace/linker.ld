/* Userspace linker script for init.bin
 * Links userspace binaries at 0x1000 (text) and 0x2000 (data)
 * as specified in the Makefile
 */

ENTRY(_start)

SECTIONS
{
  /* Text section at 0x1000 */
  . = 0x1000;
  
  .text :
  {
    *(.text*)
  }
  
  /* Data section at 0x2000 */
  . = 0x2000;
  
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
