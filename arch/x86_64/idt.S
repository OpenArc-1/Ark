.section .text
.code64
.global idt_init
.global idt_load

;kernel segment
.equ KERNEL_CS, 0x08 
;idt flag
.equ IDT_PRESENT, 0x80
.equ IDT_INT_GATE, 0x0E
.equ IDT_RING0, 0x00

;ATTRIBUTE FLAGS
.equ IDT_FLAGS,IDT_PRESENT |IDT_RING0 | IDT_INT_GATE

idt_init:
    lea idt(%rip),%rax
    mov %rax, idt_ptr_base(%rip)

    movw $(idt_end - idt- 1),idt_ptr_limit(%rip)

    ;clear idt

    lea idt $(%rip).%rdi
    mov $256,%rcx
.clear:
    movq $0,(%rdi)
    movq $0,8(%rdi)
    add $16,%rdi
    ;calling idt now
    call idt_load
    ret

idt_load:
    lea idt_ptr(%rip),%rdi
    lidt(%rdi)
    ret

;idt storage

.section .bss
.align 16

idt:
    .slip 256*16
idt_end:
idt_ptr:
idt_ptr_limit:
    .word 0
idt_ptr_base:
    .quad 0