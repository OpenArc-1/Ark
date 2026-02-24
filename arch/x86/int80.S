/**
 * x86 int 0x80 Syscall Handler (Assembly)
 * Minimal IDT setup for syscall support
 */

.section .text
.align 4

/**
 * Syscall dispatcher function
 * Arguments passed in eax/ebx/ecx/edx
 */
.extern syscall_dispatch

/**
 * int 0x80 syscall handler
 * Called with userspace:
 *   eax = syscall number
 *   ebx = arg1 (fd)
 *   ecx = arg2 (buffer ptr)
 *   edx = arg3 (count)
 *
 * CPU has already pushed: EIP, CS, EFLAGS
 * Must return via iret
 */
.globl int80_syscall_stub
.type int80_syscall_stub, @function
int80_syscall_stub:
    /* Save registers that we'll use */
    pushl %ebp
    pushl %esi
    pushl %edi
    
    /* Arguments already in: eax=number, ebx=arg1, ecx=arg2, edx=arg3 */
    /* Simply call syscall_dispatch with these registers as arguments */
    pushl %edx              /* arg3 - count */
    pushl %ecx              /* arg2 - buffer ptr */
    pushl %ebx              /* arg1 - fd */
    pushl %eax              /* number */
    
    call syscall_dispatch
    
    /* Clean up arguments from stack */
    addl $16, %esp
    
    /* eax now has return value - good, that's what we want */
    
    /* Restore registers */
    popl %edi
    popl %esi
    popl %ebp
    
    /* Return from interrupt - CPU will pop EIP, CS, EFLAGS */
    iret

/**
 * Run ELF entry with a new stack (argc, argv, envp layout for libc _start).
 * void run_elf_entry(uint32_t entry_addr, uint32_t new_esp);
 * Loads new_esp into ESP and jumps to entry_addr.
 */
.globl run_elf_entry
.type run_elf_entry, @function
run_elf_entry:
    movl 4(%esp), %eax    /* entry_addr */
    movl 8(%esp), %esp    /* new_esp */
    jmp *%eax

/**
 * IDT table - used by idt_init() to set up syscall handling
 */
.section .data
.align 8

.globl g_idt
.globl g_idt_desc
g_idt:
    .space 2048     /* 256 IDT entries * 8 bytes each */

g_idt_desc:
    .word 2047      /* Limit: 256 entries - 1 */
    .long g_idt     /* Base address */
