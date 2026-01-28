/* Display Server Entry Point (x86 32-bit, ring 3) */

.global _start

.section .text
.code32

_start:
    /* Set up stack - load address of stack_top into esp */
    movl $stack_top, %esp
    
    /* Call compositor main */
    call compositor_main
    
    /* Exit syscall */
    movl $2, %eax          /* SYS_EXIT */
    movl $0, %ebx          /* exit code = 0 */
    int $0x80
    
    hlt                    /* Should not reach here */

/* Stack space */
.section .bss
.align 4096
stack_bottom:
    .space 8192
stack_top:
