/**
 * Ark init.bin C runtime startup.
 *
 * The kernel's elf_execute() calls the ELF entry point as:
 *   entry_func(api)   -- passes ark_kernel_api_t* as first arg
 *
 * On x86 (cdecl), the caller pushes args right-to-left then calls.
 * So on entry here, [esp+4] = api pointer.
 *
 * We just call main(api) and hlt when it returns.
 * No argc/argv, no exit() syscall needed.
 */
.section .text
.globl _start
.type _start, @function
_start:
    /* [esp+4] = ark_kernel_api_t *api (set by elf_execute before call) */
    movl 4(%esp), %eax     /* api pointer */
    pushl %eax             /* pass as first arg to main */
    call main
    addl $4, %esp
    /* halt — kernel will reclaim control after init returns */
    cli
1:  hlt
    jmp 1b

.section .note.GNU-stack,"",@progbits
