/*
 * arch/x86_64/idt.S — 256-entry 64-bit IDT for Ark kernel
 *
 * All 256 vectors are populated so no spurious interrupt can
 * triple-fault the machine.
 *
 * Gate descriptor layout (16 bytes each):
 *   [15:0]   offset[15:0]
 *   [31:16]  segment selector (0x18 = 64-bit kernel code)
 *   [39:32]  IST (bits[2:0]), reserved
 *   [47:40]  type/attr: 0x8E = P|DPL=0|int64, 0x8F = P|DPL=0|trap64
 *   [63:48]  offset[31:16]
 *   [95:64]  offset[63:32]
 *   [127:96] reserved (zero)
 */

.section .text
.code64

/* ── Exception entry stubs ─────────────────────────────────────────────── */

/* No error code pushed by CPU — we push a dummy 0 then the vector */
.macro ISR_NOERR num
.globl isr64_\num
isr64_\num:
    pushq $0        /* dummy error code */
    pushq $\num     /* vector number    */
    jmp   isr64_common
.endm

/* CPU pushes error code before transferring control */
.macro ISR_ERR num
.globl isr64_\num
isr64_\num:
    pushq $\num     /* vector number (error_code already below us) */
    jmp   isr64_common
.endm

/* Generic stub for vectors 48-255 (no error code) */
.macro IRQ_STUB num
.globl irq64_\num
irq64_\num:
    pushq $0
    pushq $\num
    jmp   isr64_common
.endm

/* ── CPU exceptions 0-31 ───────────────────────────────────────────────── */
ISR_NOERR 0     /* #DE  Divide Error             */
ISR_NOERR 1     /* #DB  Debug                    */
ISR_NOERR 2     /*      NMI                      */
ISR_NOERR 3     /* #BP  Breakpoint               */
ISR_NOERR 4     /* #OF  Overflow                 */
ISR_NOERR 5     /* #BR  Bound Range              */
ISR_NOERR 6     /* #UD  Invalid Opcode           */
ISR_NOERR 7     /* #NM  Device Not Available     */
ISR_ERR   8     /* #DF  Double Fault             */
ISR_NOERR 9     /*      Coprocessor Overrun      */
ISR_ERR   10    /* #TS  Invalid TSS              */
ISR_ERR   11    /* #NP  Segment Not Present      */
ISR_ERR   12    /* #SS  Stack Fault              */
ISR_ERR   13    /* #GP  General Protection       */
ISR_ERR   14    /* #PF  Page Fault               */
ISR_NOERR 15    /*      Reserved                 */
ISR_NOERR 16    /* #MF  x87 FP Exception         */
ISR_ERR   17    /* #AC  Alignment Check          */
ISR_NOERR 18    /* #MC  Machine Check            */
ISR_NOERR 19    /* #XM  SIMD FP Exception        */
ISR_NOERR 20    /* #VE  Virtualization Exception */
ISR_NOERR 21
ISR_NOERR 22
ISR_NOERR 23
ISR_NOERR 24
ISR_NOERR 25
ISR_NOERR 26
ISR_NOERR 27
ISR_NOERR 28
ISR_NOERR 29
ISR_ERR   30    /* #SX  Security Exception       */
ISR_NOERR 31

/* ── PIC IRQs 32-47 ────────────────────────────────────────────────────── */
IRQ_STUB 32

/* ── IRQ1 — PS/2 keyboard (vector 33) ──────────────────────────────────────
 * Sets kbd_irq_pending so kbd_poll() / input_poll() know a scancode is
 * waiting in the PS/2 data port, then sends PIC EOI and returns.
 * Does NOT call isr64_common — this is a fast leaf handler.
 */
.globl irq1_handler
.type  irq1_handler, @function
irq1_handler:
    pushq   %rax
    movb    $1, kbd_irq_pending(%rip)   /* volatile u8 flag — RIP-relative */
    movb    $0x20, %al
    outb    %al, $0x20                  /* PIC master EOI                   */
    popq    %rax
    iretq

/* kbd_irq_pending — volatile byte, shared with C (extern volatile u8)      */
.globl kbd_irq_pending
.section .bss
kbd_irq_pending:
    .byte 0
.section .text
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

/* ── Vectors 48-127 (generic) ──────────────────────────────────────────── */
IRQ_STUB 48;  IRQ_STUB 49;  IRQ_STUB 50;  IRQ_STUB 51
IRQ_STUB 52;  IRQ_STUB 53;  IRQ_STUB 54;  IRQ_STUB 55
IRQ_STUB 56;  IRQ_STUB 57;  IRQ_STUB 58;  IRQ_STUB 59
IRQ_STUB 60;  IRQ_STUB 61;  IRQ_STUB 62;  IRQ_STUB 63
IRQ_STUB 64;  IRQ_STUB 65;  IRQ_STUB 66;  IRQ_STUB 67
IRQ_STUB 68;  IRQ_STUB 69;  IRQ_STUB 70;  IRQ_STUB 71
IRQ_STUB 72;  IRQ_STUB 73;  IRQ_STUB 74;  IRQ_STUB 75
IRQ_STUB 76;  IRQ_STUB 77;  IRQ_STUB 78;  IRQ_STUB 79
IRQ_STUB 80;  IRQ_STUB 81;  IRQ_STUB 82;  IRQ_STUB 83
IRQ_STUB 84;  IRQ_STUB 85;  IRQ_STUB 86;  IRQ_STUB 87
IRQ_STUB 88;  IRQ_STUB 89;  IRQ_STUB 90;  IRQ_STUB 91
IRQ_STUB 92;  IRQ_STUB 93;  IRQ_STUB 94;  IRQ_STUB 95
IRQ_STUB 96;  IRQ_STUB 97;  IRQ_STUB 98;  IRQ_STUB 99
IRQ_STUB 100; IRQ_STUB 101; IRQ_STUB 102; IRQ_STUB 103
IRQ_STUB 104; IRQ_STUB 105; IRQ_STUB 106; IRQ_STUB 107
IRQ_STUB 108; IRQ_STUB 109; IRQ_STUB 110; IRQ_STUB 111
IRQ_STUB 112; IRQ_STUB 113; IRQ_STUB 114; IRQ_STUB 115
IRQ_STUB 116; IRQ_STUB 117; IRQ_STUB 118; IRQ_STUB 119
IRQ_STUB 120; IRQ_STUB 121; IRQ_STUB 122; IRQ_STUB 123
IRQ_STUB 124; IRQ_STUB 125; IRQ_STUB 126; IRQ_STUB 127

/* ── int 0x80 syscall gate (DPL=3, userspace-callable) ─────────────────── */
.globl int80_64_stub
.globl int80_syscall_stub
int80_64_stub:
int80_syscall_stub:
    pushq $0
    pushq $0x80
    jmp   isr64_common

/* ── Vectors 129-255 (generic) ─────────────────────────────────────────── */
IRQ_STUB 129; IRQ_STUB 130; IRQ_STUB 131; IRQ_STUB 132
IRQ_STUB 133; IRQ_STUB 134; IRQ_STUB 135; IRQ_STUB 136
IRQ_STUB 137; IRQ_STUB 138; IRQ_STUB 139; IRQ_STUB 140
IRQ_STUB 141; IRQ_STUB 142; IRQ_STUB 143; IRQ_STUB 144
IRQ_STUB 145; IRQ_STUB 146; IRQ_STUB 147; IRQ_STUB 148
IRQ_STUB 149; IRQ_STUB 150; IRQ_STUB 151; IRQ_STUB 152
IRQ_STUB 153; IRQ_STUB 154; IRQ_STUB 155; IRQ_STUB 156
IRQ_STUB 157; IRQ_STUB 158; IRQ_STUB 159; IRQ_STUB 160
IRQ_STUB 161; IRQ_STUB 162; IRQ_STUB 163; IRQ_STUB 164
IRQ_STUB 165; IRQ_STUB 166; IRQ_STUB 167; IRQ_STUB 168
IRQ_STUB 169; IRQ_STUB 170; IRQ_STUB 171; IRQ_STUB 172
IRQ_STUB 173; IRQ_STUB 174; IRQ_STUB 175; IRQ_STUB 176
IRQ_STUB 177; IRQ_STUB 178; IRQ_STUB 179; IRQ_STUB 180
IRQ_STUB 181; IRQ_STUB 182; IRQ_STUB 183; IRQ_STUB 184
IRQ_STUB 185; IRQ_STUB 186; IRQ_STUB 187; IRQ_STUB 188
IRQ_STUB 189; IRQ_STUB 190; IRQ_STUB 191; IRQ_STUB 192
IRQ_STUB 193; IRQ_STUB 194; IRQ_STUB 195; IRQ_STUB 196
IRQ_STUB 197; IRQ_STUB 198; IRQ_STUB 199; IRQ_STUB 200
IRQ_STUB 201; IRQ_STUB 202; IRQ_STUB 203; IRQ_STUB 204
IRQ_STUB 205; IRQ_STUB 206; IRQ_STUB 207; IRQ_STUB 208
IRQ_STUB 209; IRQ_STUB 210; IRQ_STUB 211; IRQ_STUB 212
IRQ_STUB 213; IRQ_STUB 214; IRQ_STUB 215; IRQ_STUB 216
IRQ_STUB 217; IRQ_STUB 218; IRQ_STUB 219; IRQ_STUB 220
IRQ_STUB 221; IRQ_STUB 222; IRQ_STUB 223; IRQ_STUB 224
IRQ_STUB 225; IRQ_STUB 226; IRQ_STUB 227; IRQ_STUB 228
IRQ_STUB 229; IRQ_STUB 230; IRQ_STUB 231; IRQ_STUB 232
IRQ_STUB 233; IRQ_STUB 234; IRQ_STUB 235; IRQ_STUB 236
IRQ_STUB 237; IRQ_STUB 238; IRQ_STUB 239; IRQ_STUB 240
IRQ_STUB 241; IRQ_STUB 242; IRQ_STUB 243; IRQ_STUB 244
IRQ_STUB 245; IRQ_STUB 246; IRQ_STUB 247; IRQ_STUB 248
IRQ_STUB 249; IRQ_STUB 250; IRQ_STUB 251; IRQ_STUB 252
IRQ_STUB 253; IRQ_STUB 254; IRQ_STUB 255

/* ── Common handler ─────────────────────────────────────────────────────── */
/*
 * On entry: stack has  ...| rip | cs | rflags | rsp | ss  (CPU frame)
 *                          | error_code | vector            (our pushes)
 *
 * We save all GPRs, pass rsp as cpu_frame64_t* to exception64_handler,
 * then restore and iretq.
 *
 * cpu_frame64_t layout (from low addr at rsp):
 *   r15 r14 r13 r12 r11 r10 r9 r8 rbp rdi rsi rdx rcx rbx rax
 *   vector  error_code
 *   rip  cs  rflags  rsp  ss
 */
.globl isr64_common
isr64_common:
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

    movq  %rsp, %rdi        /* arg1 = pointer to cpu_frame64_t */
    cld
    call  exception64_handler

    popq  %r15
    popq  %r14
    popq  %r13
    popq  %r12
    popq  %r11
    popq  %r10
    popq  %r9
    popq  %r8
    popq  %rbp
    popq  %rdi
    popq  %rsi
    popq  %rdx
    popq  %rcx
    popq  %rbx
    popq  %rax

    addq  $16, %rsp         /* discard vector + error_code */
    iretq

/* ── idt64_init ─────────────────────────────────────────────────────────── */
/*
 * Build the IDT in-place and load the IDTR.
 * Called from arch_x86_64_entry() as the very first thing after BSS zero.
 */
.globl idt64_init
.type  idt64_init, @function
idt64_init:
    pushq %rbx
    pushq %r12
    pushq %r13

    /* Disable all hardware breakpoints immediately.
     * QEMU may boot with DR7 non-zero or TF set, causing a spurious #DB
     * on the very first instruction after we load our IDT.
     * Clearing DR7 here prevents that race entirely. */
    xorq  %rax, %rax
    mov   %rax, %dr7
    mov   %rax, %dr6

    /* ── Remap 8259 PIC: IRQ0-7 → INT 32-39, IRQ8-15 → INT 40-47 ── */
    movb  $0x11, %al
    outb  %al, $0x20        /* ICW1 master init */
    outb  %al, $0xA0        /* ICW1 slave  init */
    movb  $0x20, %al
    outb  %al, $0x21        /* ICW2 master base = 32 */
    movb  $0x28, %al
    outb  %al, $0xA1        /* ICW2 slave  base = 40 */
    movb  $0x04, %al
    outb  %al, $0x21        /* ICW3 master: slave on IRQ2 */
    movb  $0x02, %al
    outb  %al, $0xA1        /* ICW3 slave:  cascade id   */
    movb  $0x01, %al
    outb  %al, $0x21        /* ICW4 8086 mode */
    outb  %al, $0xA1
    /* Mask all IRQs except timer (IRQ0) and keyboard (IRQ1) */
    movb  $0xFC, %al
    outb  %al, $0x21
    movb  $0xFF, %al
    outb  %al, $0xA1

    /* ── Fill all 256 IDT slots ── */
    /*
     * We walk a table of (handler_addr) values generated from the stubs
     * above.  Rather than 256 separate calls, use a small loop.
     * r12 = current vector, r13 = &idt_handler_table
     */
    xorq  %r12, %r12                     /* vector = 0 */
    leaq  idt_handler_table(%rip), %r13

.Lidt_loop:
    movq  (%r13, %r12, 8), %rsi         /* handler = table[vector] */
    movq  %r12, %rdi                    /* vector number           */
    cmpq  $0x80, %r12
    je    .Lidt_user_gate               /* int 0x80: DPL=3         */
    cmpq  $32, %r12
    jb    .Lidt_trap_gate               /* 0-31: trap gates        */
    call  _idt64_set_gate               /* 32+:  interrupt gates   */
    jmp   .Lidt_next
.Lidt_trap_gate:
    call  _idt64_set_trap
    jmp   .Lidt_next
.Lidt_user_gate:
    call  _idt64_set_user
.Lidt_next:
    incq  %r12
    cmpq  $256, %r12
    jb    .Lidt_loop

    /* ── Write IDTR base and load ── */
    leaq  g_idt(%rip),     %rax
    movq  %rax,            g_idt_desc + 2(%rip)
    movw  $(256*16 - 1),   g_idt_desc(%rip)
    lidt  g_idt_desc(%rip)

    popq  %r13
    popq  %r12
    popq  %rbx
    ret

/* ── Handler address table (one 8-byte entry per vector 0-255) ─────────── */
.section .rodata
.align 8
idt_handler_table:
    .quad isr64_0,   isr64_1,   isr64_2,   isr64_3
    .quad isr64_4,   isr64_5,   isr64_6,   isr64_7
    .quad isr64_8,   isr64_9,   isr64_10,  isr64_11
    .quad isr64_12,  isr64_13,  isr64_14,  isr64_15
    .quad isr64_16,  isr64_17,  isr64_18,  isr64_19
    .quad isr64_20,  isr64_21,  isr64_22,  isr64_23
    .quad isr64_24,  isr64_25,  isr64_26,  isr64_27
    .quad isr64_28,  isr64_29,  isr64_30,  isr64_31
    .quad irq64_32,  irq1_handler, irq64_34,  irq64_35
    .quad irq64_36,  irq64_37,  irq64_38,  irq64_39
    .quad irq64_40,  irq64_41,  irq64_42,  irq64_43
    .quad irq64_44,  irq64_45,  irq64_46,  irq64_47
    .quad irq64_48,  irq64_49,  irq64_50,  irq64_51
    .quad irq64_52,  irq64_53,  irq64_54,  irq64_55
    .quad irq64_56,  irq64_57,  irq64_58,  irq64_59
    .quad irq64_60,  irq64_61,  irq64_62,  irq64_63
    .quad irq64_64,  irq64_65,  irq64_66,  irq64_67
    .quad irq64_68,  irq64_69,  irq64_70,  irq64_71
    .quad irq64_72,  irq64_73,  irq64_74,  irq64_75
    .quad irq64_76,  irq64_77,  irq64_78,  irq64_79
    .quad irq64_80,  irq64_81,  irq64_82,  irq64_83
    .quad irq64_84,  irq64_85,  irq64_86,  irq64_87
    .quad irq64_88,  irq64_89,  irq64_90,  irq64_91
    .quad irq64_92,  irq64_93,  irq64_94,  irq64_95
    .quad irq64_96,  irq64_97,  irq64_98,  irq64_99
    .quad irq64_100, irq64_101, irq64_102, irq64_103
    .quad irq64_104, irq64_105, irq64_106, irq64_107
    .quad irq64_108, irq64_109, irq64_110, irq64_111
    .quad irq64_112, irq64_113, irq64_114, irq64_115
    .quad irq64_116, irq64_117, irq64_118, irq64_119
    .quad irq64_120, irq64_121, irq64_122, irq64_123
    .quad irq64_124, irq64_125, irq64_126, irq64_127
    .quad int80_64_stub                          /* 0x80 = 128 */
    .quad irq64_129, irq64_130, irq64_131, irq64_132
    .quad irq64_133, irq64_134, irq64_135, irq64_136
    .quad irq64_137, irq64_138, irq64_139, irq64_140
    .quad irq64_141, irq64_142, irq64_143, irq64_144
    .quad irq64_145, irq64_146, irq64_147, irq64_148
    .quad irq64_149, irq64_150, irq64_151, irq64_152
    .quad irq64_153, irq64_154, irq64_155, irq64_156
    .quad irq64_157, irq64_158, irq64_159, irq64_160
    .quad irq64_161, irq64_162, irq64_163, irq64_164
    .quad irq64_165, irq64_166, irq64_167, irq64_168
    .quad irq64_169, irq64_170, irq64_171, irq64_172
    .quad irq64_173, irq64_174, irq64_175, irq64_176
    .quad irq64_177, irq64_178, irq64_179, irq64_180
    .quad irq64_181, irq64_182, irq64_183, irq64_184
    .quad irq64_185, irq64_186, irq64_187, irq64_188
    .quad irq64_189, irq64_190, irq64_191, irq64_192
    .quad irq64_193, irq64_194, irq64_195, irq64_196
    .quad irq64_197, irq64_198, irq64_199, irq64_200
    .quad irq64_201, irq64_202, irq64_203, irq64_204
    .quad irq64_205, irq64_206, irq64_207, irq64_208
    .quad irq64_209, irq64_210, irq64_211, irq64_212
    .quad irq64_213, irq64_214, irq64_215, irq64_216
    .quad irq64_217, irq64_218, irq64_219, irq64_220
    .quad irq64_221, irq64_222, irq64_223, irq64_224
    .quad irq64_225, irq64_226, irq64_227, irq64_228
    .quad irq64_229, irq64_230, irq64_231, irq64_232
    .quad irq64_233, irq64_234, irq64_235, irq64_236
    .quad irq64_237, irq64_238, irq64_239, irq64_240
    .quad irq64_241, irq64_242, irq64_243, irq64_244
    .quad irq64_245, irq64_246, irq64_247, irq64_248
    .quad irq64_249, irq64_250, irq64_251, irq64_252
    .quad irq64_253, irq64_254, irq64_255

/* ── Gate write helpers ─────────────────────────────────────────────────── */
/*
 * _idt64_set_gate(vector=rdi, handler=rsi)  — DPL=0 interrupt gate (0x8E)
 * _idt64_set_trap(vector=rdi, handler=rsi)  — DPL=0 trap gate     (0x8F)
 * _idt64_set_user(vector=rdi, handler=rsi)  — DPL=3 interrupt gate (0xEE)
 */
.section .text

_idt64_set_gate:
    pushq %rbx
    leaq  g_idt(%rip), %rbx
    shlq  $4,  %rdi
    addq  %rdi, %rbx
    movw  %si,  0(%rbx)         /* offset[15:0]  */
    movw  $0x18, 2(%rbx)        /* CS = 64-bit kernel code */
    movb  $0,   4(%rbx)         /* IST = 0       */
    movb  $0x8E, 5(%rbx)        /* P=1 DPL=0 64-bit interrupt gate */
    shrq  $16, %rsi
    movw  %si,  6(%rbx)         /* offset[31:16] */
    shrq  $16, %rsi
    movl  %esi, 8(%rbx)         /* offset[63:32] */
    movl  $0,  12(%rbx)
    popq  %rbx
    ret

_idt64_set_trap:
    pushq %rbx
    leaq  g_idt(%rip), %rbx
    shlq  $4,  %rdi
    addq  %rdi, %rbx
    movw  %si,  0(%rbx)
    movw  $0x18, 2(%rbx)
    movb  $0,   4(%rbx)
    movb  $0x8F, 5(%rbx)        /* P=1 DPL=0 64-bit trap gate */
    shrq  $16, %rsi
    movw  %si,  6(%rbx)
    shrq  $16, %rsi
    movl  %esi, 8(%rbx)
    movl  $0,  12(%rbx)
    popq  %rbx
    ret

_idt64_set_user:
    pushq %rbx
    leaq  g_idt(%rip), %rbx
    shlq  $4,  %rdi
    addq  %rdi, %rbx
    movw  %si,  0(%rbx)
    movw  $0x18, 2(%rbx)
    movb  $0,   4(%rbx)
    movb  $0xEE, 5(%rbx)        /* P=1 DPL=3 64-bit interrupt gate */
    shrq  $16, %rsi
    movw  %si,  6(%rbx)
    shrq  $16, %rsi
    movl  %esi, 8(%rbx)
    movl  $0,  12(%rbx)
    popq  %rbx
    ret

/* Public C-callable alias */
.globl idt64_set_gate
idt64_set_gate:
    jmp _idt64_set_gate

/* ── BSS: IDT storage ───────────────────────────────────────────────────── */
.section .bss
.align 16
.globl g_idt
g_idt:
    .skip 256 * 16              /* 4096 bytes */

.globl g_idt_desc
g_idt_desc:
    .skip 10                    /* 2-byte limit + 8-byte base */

.section .note.GNU-stack, "", @progbits
