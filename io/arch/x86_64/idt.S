/*
 * arch/x86_64/idt.S - Full 64-bit IDT for Ark kernel
 *
 * Sets up a proper 256-entry IDT with handlers for all CPU exceptions
 * (vectors 0-31) and int 0x80 syscall gate. Without a real IDT any
 * fault causes a triple-fault reset - the bootloop symptom.
 */

.section .text
.code64

/* ------------------------------------------------------------------ */
/* Exception entry macro - no error code pushed by CPU                */
.macro ISR_NOERR num
.globl isr64_\num
isr64_\num:
    pushq $0            /* dummy error code */
    pushq $\num         /* vector number    */
    jmp   isr64_common
.endm

/* Exception entry macro - CPU pushes error code automatically        */
.macro ISR_ERR num
.globl isr64_\num
isr64_\num:
    pushq $\num         /* vector number (error code already on stack) */
    jmp   isr64_common
.endm

/* CPU exceptions 0-31 */
ISR_NOERR 0    /* #DE Divide Error        */
ISR_NOERR 1    /* #DB Debug               */
ISR_NOERR 2    /* NMI                     */
ISR_NOERR 3    /* #BP Breakpoint          */
ISR_NOERR 4    /* #OF Overflow            */
ISR_NOERR 5    /* #BR Bound Range         */
ISR_NOERR 6    /* #UD Invalid Opcode      */
ISR_NOERR 7    /* #NM Device Not Avail    */
ISR_ERR   8    /* #DF Double Fault        */
ISR_NOERR 9    /* Coprocessor Overrun     */
ISR_ERR   10   /* #TS Invalid TSS         */
ISR_ERR   11   /* #NP Seg Not Present     */
ISR_ERR   12   /* #SS Stack Fault         */
ISR_ERR   13   /* #GP General Protection  */
ISR_ERR   14   /* #PF Page Fault          */
ISR_NOERR 15   /* Reserved                */
ISR_NOERR 16   /* #MF x87 FP Exception    */
ISR_ERR   17   /* #AC Alignment Check     */
ISR_NOERR 18   /* #MC Machine Check       */
ISR_NOERR 19   /* #XM SIMD FP             */
ISR_NOERR 20   /* #VE Virtualisation      */
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30   /* #SX Security Exception  */
ISR_NOERR 31

/* IRQ stubs 32-47 (PIC remapped) */
.macro IRQ_STUB num
.globl irq64_\num
irq64_\num:
    pushq $0
    pushq $\num
    jmp   isr64_common
.endm

IRQ_STUB 32
IRQ_STUB 33
IRQ_STUB 34
IRQ_STUB 35
IRQ_STUB 36
IRQ_STUB 37
IRQ_STUB 38
IRQ_STUB 39
IRQ_STUB 40
IRQ_STUB 41
IRQ_STUB 42
IRQ_STUB 43
IRQ_STUB 44
IRQ_STUB 45
IRQ_STUB 46
IRQ_STUB 47

/* int 0x80 syscall */
.globl int80_64_stub
int80_64_stub:
    pushq $0
    pushq $0x80
    jmp   isr64_common

/* ------------------------------------------------------------------ */
/* Common handler - save all regs, call C handler, restore, iret      */
isr64_common:
    /* Save caller-saved + extra regs */
    pushq %rax
    pushq %rbx
    pushq %rcx
    pushq %rdx
    pushq %rsi
    pushq %rdi
    pushq %rbp
    pushq %r8
    pushq %r9
    pushq %r10
    pushq %r11
    pushq %r12
    pushq %r13
    pushq %r14
    pushq %r15

    movq %rsp, %rdi     /* arg1: pointer to saved register frame */
    call exception64_handler

    popq %r15
    popq %r14
    popq %r13
    popq %r12
    popq %r11
    popq %r10
    popq %r9
    popq %r8
    popq %rbp
    popq %rdi
    popq %rsi
    popq %rdx
    popq %rcx
    popq %rbx
    popq %rax

    addq $16, %rsp      /* pop vector + error code */
    iretq

/* ------------------------------------------------------------------ */
/* idt64_init - build and load the IDT                                 */
/* Called from arch_x86_64_entry() before anything else               */
.globl idt64_init
.type  idt64_init, @function
idt64_init:
    /* -- Remap PIC: IRQ0-7 -> INT 32-39, IRQ8-15 -> INT 40-47 -- */
    movb $0x11, %al
    outb %al, $0x20     /* ICW1: init master PIC */
    outb %al, $0xA0     /* ICW1: init slave  PIC */
    movb $0x20, %al
    outb %al, $0x21     /* ICW2: master vector offset 32 */
    movb $0x28, %al
    outb %al, $0xA1     /* ICW2: slave  vector offset 40 */
    movb $0x04, %al
    outb %al, $0x21     /* ICW3: master - slave at IRQ2 */
    movb $0x02, %al
    outb %al, $0xA1     /* ICW3: slave cascade identity */
    movb $0x01, %al
    outb %al, $0x21     /* ICW4: 8086 mode */
    outb %al, $0xA1
    /* Mask all IRQs for now except IRQ1 (keyboard) and IRQ0 (timer) */
    movb $0xFC, %al     /* 11111100 - unmask IRQ0+IRQ1 */
    outb %al, $0x21
    movb $0xFF, %al
    outb %al, $0xA1

    /* -- Fill IDT -- */
    leaq g_idt(%rip),  %rdi

    /* Helper: install gate for vector %rax, handler in %rsi */
    /* We'll use a loop - handlers are sequential in memory  */

    /* Exceptions 0-31 */
    leaq isr64_0(%rip),  %rsi;  movq $0,  %rdi; call _idt64_set_trap
    leaq isr64_1(%rip),  %rsi;  movq $1,  %rdi; call _idt64_set_trap
    leaq isr64_2(%rip),  %rsi;  movq $2,  %rdi; call _idt64_set_trap
    leaq isr64_3(%rip),  %rsi;  movq $3,  %rdi; call _idt64_set_trap
    leaq isr64_4(%rip),  %rsi;  movq $4,  %rdi; call _idt64_set_trap
    leaq isr64_5(%rip),  %rsi;  movq $5,  %rdi; call _idt64_set_trap
    leaq isr64_6(%rip),  %rsi;  movq $6,  %rdi; call _idt64_set_trap
    leaq isr64_7(%rip),  %rsi;  movq $7,  %rdi; call _idt64_set_trap
    leaq isr64_8(%rip),  %rsi;  movq $8,  %rdi; call _idt64_set_trap
    leaq isr64_9(%rip),  %rsi;  movq $9,  %rdi; call _idt64_set_trap
    leaq isr64_10(%rip), %rsi;  movq $10, %rdi; call _idt64_set_trap
    leaq isr64_11(%rip), %rsi;  movq $11, %rdi; call _idt64_set_trap
    leaq isr64_12(%rip), %rsi;  movq $12, %rdi; call _idt64_set_trap
    leaq isr64_13(%rip), %rsi;  movq $13, %rdi; call _idt64_set_trap
    leaq isr64_14(%rip), %rsi;  movq $14, %rdi; call _idt64_set_trap
    leaq isr64_15(%rip), %rsi;  movq $15, %rdi; call _idt64_set_trap
    leaq isr64_16(%rip), %rsi;  movq $16, %rdi; call _idt64_set_trap
    leaq isr64_17(%rip), %rsi;  movq $17, %rdi; call _idt64_set_trap
    leaq isr64_18(%rip), %rsi;  movq $18, %rdi; call _idt64_set_trap
    leaq isr64_19(%rip), %rsi;  movq $19, %rdi; call _idt64_set_trap
    leaq isr64_20(%rip), %rsi;  movq $20, %rdi; call _idt64_set_trap
    leaq isr64_21(%rip), %rsi;  movq $21, %rdi; call _idt64_set_trap
    leaq isr64_22(%rip), %rsi;  movq $22, %rdi; call _idt64_set_trap
    leaq isr64_23(%rip), %rsi;  movq $23, %rdi; call _idt64_set_trap
    leaq isr64_24(%rip), %rsi;  movq $24, %rdi; call _idt64_set_trap
    leaq isr64_25(%rip), %rsi;  movq $25, %rdi; call _idt64_set_trap
    leaq isr64_26(%rip), %rsi;  movq $26, %rdi; call _idt64_set_trap
    leaq isr64_27(%rip), %rsi;  movq $27, %rdi; call _idt64_set_trap
    leaq isr64_28(%rip), %rsi;  movq $28, %rdi; call _idt64_set_trap
    leaq isr64_29(%rip), %rsi;  movq $29, %rdi; call _idt64_set_trap
    leaq isr64_30(%rip), %rsi;  movq $30, %rdi; call _idt64_set_trap
    leaq isr64_31(%rip), %rsi;  movq $31, %rdi; call _idt64_set_trap

    /* IRQ stubs 32-47 */
    leaq irq64_32(%rip), %rsi;  movq $32, %rdi; call _idt64_set_gate
    leaq irq64_33(%rip), %rsi;  movq $33, %rdi; call _idt64_set_gate
    leaq irq64_34(%rip), %rsi;  movq $34, %rdi; call _idt64_set_gate
    leaq irq64_35(%rip), %rsi;  movq $35, %rdi; call _idt64_set_gate
    leaq irq64_36(%rip), %rsi;  movq $36, %rdi; call _idt64_set_gate
    leaq irq64_37(%rip), %rsi;  movq $37, %rdi; call _idt64_set_gate
    leaq irq64_38(%rip), %rsi;  movq $38, %rdi; call _idt64_set_gate
    leaq irq64_39(%rip), %rsi;  movq $39, %rdi; call _idt64_set_gate
    leaq irq64_40(%rip), %rsi;  movq $40, %rdi; call _idt64_set_gate
    leaq irq64_41(%rip), %rsi;  movq $41, %rdi; call _idt64_set_gate
    leaq irq64_42(%rip), %rsi;  movq $42, %rdi; call _idt64_set_gate
    leaq irq64_43(%rip), %rsi;  movq $43, %rdi; call _idt64_set_gate
    leaq irq64_44(%rip), %rsi;  movq $44, %rdi; call _idt64_set_gate
    leaq irq64_45(%rip), %rsi;  movq $45, %rdi; call _idt64_set_gate
    leaq irq64_46(%rip), %rsi;  movq $46, %rdi; call _idt64_set_gate
    leaq irq64_47(%rip), %rsi;  movq $47, %rdi; call _idt64_set_gate

    /* int 0x80 syscall - DPL=3 so userspace can call it */
    leaq int80_64_stub(%rip), %rsi
    movq $0x80, %rdi
    call _idt64_set_user

    /* -- Write descriptor and load IDTR -- */
    leaq g_idt(%rip),    %rax
    movq %rax,           g_idt_desc + 2(%rip)
    movw $(256*16 - 1),  g_idt_desc(%rip)
    lidt g_idt_desc(%rip)

    ret

/* ------------------------------------------------------------------ */
/* _idt64_set_gate(vector=rdi, handler=rsi)  DPL=0 interrupt gate     */
_idt64_set_gate:
    pushq %rbx
    leaq  g_idt(%rip), %rbx
    shlq  $4,  %rdi
    addq  %rdi, %rbx
    movw  %si,  0(%rbx)          /* offset[15:0]  */
    movw  $0x18, 2(%rbx)         /* selector: 64-bit code seg */
    movb  $0,   4(%rbx)          /* IST = 0       */
    movb  $0x8E, 5(%rbx)         /* P=1 DPL=0 type=0xE (64-bit int gate) */
    shrq  $16,  %rsi
    movw  %si,  6(%rbx)          /* offset[31:16] */
    shrq  $16,  %rsi
    movl  %esi, 8(%rbx)          /* offset[63:32] */
    movl  $0,   12(%rbx)         /* reserved      */
    popq  %rbx
    ret

/* _idt64_set_trap - same but type=0xF (trap gate, IF not cleared)    */
_idt64_set_trap:
    pushq %rbx
    leaq  g_idt(%rip), %rbx
    shlq  $4,  %rdi
    addq  %rdi, %rbx
    movw  %si,  0(%rbx)
    movw  $0x18, 2(%rbx)
    movb  $0,   4(%rbx)
    movb  $0x8F, 5(%rbx)         /* P=1 DPL=0 type=0xF (64-bit trap gate) */
    shrq  $16,  %rsi
    movw  %si,  6(%rbx)
    shrq  $16,  %rsi
    movl  %esi, 8(%rbx)
    movl  $0,   12(%rbx)
    popq  %rbx
    ret

/* _idt64_set_user - DPL=3 (userspace-callable)                       */
_idt64_set_user:
    pushq %rbx
    leaq  g_idt(%rip), %rbx
    shlq  $4,  %rdi
    addq  %rdi, %rbx
    movw  %si,  0(%rbx)
    movw  $0x18, 2(%rbx)
    movb  $0,   4(%rbx)
    movb  $0xEE, 5(%rbx)         /* P=1 DPL=3 type=0xE (int gate, ring 3) */
    shrq  $16,  %rsi
    movw  %si,  6(%rbx)
    shrq  $16,  %rsi
    movl  %esi, 8(%rbx)
    movl  $0,   12(%rbx)
    popq  %rbx
    ret

/* ------------------------------------------------------------------ */
/* idt64_set_gate - public C-callable version                         */
.globl idt64_set_gate
idt64_set_gate:
    jmp _idt64_set_gate

/* ------------------------------------------------------------------ */
/* int80_syscall_stub - legacy symbol used by syscall.c               */
.globl int80_syscall_stub
int80_syscall_stub:
    pushq $0
    pushq $0x80
    jmp   isr64_common

/* ------------------------------------------------------------------ */
.section .bss
.align 16
.globl g_idt
g_idt:
    .skip 256 * 16

.globl g_idt_desc
g_idt_desc:
    .skip 10          /* 2-byte limit + 8-byte base */

.section .note.GNU-stack, "", @progbits
