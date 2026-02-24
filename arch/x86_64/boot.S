.section .text
.code64
.global _start
.type _start,@funtions

.extern kernel_main ;get kernel main

_start:
    cli
    lea stack_top(%rip),%rsp
    xor %rbp, %rbp
    cld ;sysv ABI
    call kernel_main ;kernel main call

.hang:
    hlt
    jmp .hang

.section .bss
.align 16
stack_bottom:
    .skip 16386 ;16kb stack
stack_top: