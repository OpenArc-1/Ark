/**
 * Userspace entry point (_start)
 * Sets up stack and calls shell_main
 */

.section .text
.globl _start
.type _start, @function

.extern shell_main

_start:
    /* Set up stack pointer (higher up to avoid conflicts with data) */
    movl $0x5000, %esp
    
    /* Clear direction flag */
    cld
    
    /* Call shell main function */
    call shell_main
    
    /* If main returns, exit */
    movl $60, %eax  /* SYS_EXIT */
    movl $0, %ebx   /* exit code */
    int $0x80
    
    /* Should not reach here, but loop if it does */
1:
    hlt
    jmp 1b
