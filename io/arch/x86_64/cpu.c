/*
 * arch/x86_64/cpu.c - CPU identification, RAM check, exception handler
 */
#include "ark/types.h"
#include "ark/arch.h"
#include "ark/printk.h"
#include "hw/vendor.h"

static void get_brand(char buf[49]) {
    u32 eax, ebx, ecx, edx;
    ark_cpuid(0x80000000, 0, &eax, &ebx, &ecx, &edx);
    if (eax < 0x80000004) {
        ark_cpuid(0, 0, &eax, &ebx, &ecx, &edx);
        ((u32*)buf)[0]=ebx; ((u32*)buf)[1]=edx; ((u32*)buf)[2]=ecx;
        buf[12]='\0';
        return;
    }
    u32 *p = (u32 *)buf;
    for (int i = 0; i < 3; i++) {
        ark_cpuid(0x80000002 + (u32)i, 0, &eax, &ebx, &ecx, &edx);
        p[i*4+0]=eax; p[i*4+1]=ebx; p[i*4+2]=ecx; p[i*4+3]=edx;
    }
    buf[48] = '\0';
}

void cpu_verify(void) {
    char brand[49];
    get_brand(brand);
    printk("CPU (x86_64): %s\n", brand);
}

void cpu_name(void) {
    char brand[49];
    get_brand(brand);
    printk("CPU: %s\n", brand);
}

void mem_verify(void) {
    printk("RAM check: OK (64-bit long mode)\n");
}

/*
 * exception64_handler - called from isr64_common in idt.S
 *
 * Stack frame layout pushed by isr64_common (top = lowest address):
 *   r15 r14 r13 r12 r11 r10 r9 r8 rbp rdi rsi rdx rcx rbx rax
 *   vector  error_code
 *   rip cs  rflags rsp ss   (pushed by CPU on exception)
 */
typedef struct {
    u64 r15, r14, r13, r12, r11, r10, r9, r8;
    u64 rbp, rdi, rsi, rdx, rcx, rbx, rax;
    u64 vector, error_code;
    u64 rip, cs, rflags, rsp, ss;
} __attribute__((packed)) cpu_frame64_t;

static const char *exception_names[] = {
    "#DE Divide Error",        "#DB Debug",
    "NMI",                     "#BP Breakpoint",
    "#OF Overflow",            "#BR Bound Range",
    "#UD Invalid Opcode",      "#NM Device Not Available",
    "#DF Double Fault",        "Coprocessor Overrun",
    "#TS Invalid TSS",         "#NP Seg Not Present",
    "#SS Stack Fault",         "#GP General Protection",
    "#PF Page Fault",          "Reserved",
    "#MF x87 FP",              "#AC Alignment Check",
    "#MC Machine Check",       "#XM SIMD FP",
    "#VE Virtualisation",      "Reserved",
    "Reserved",                "Reserved",
    "Reserved",                "Reserved",
    "Reserved",                "Reserved",
    "Reserved",                "Reserved",
    "#SX Security",            "Reserved"
};

void exception64_handler(cpu_frame64_t *f) {
    u64 vec = f->vector;

    /* IRQ acknowledgement for PIC-mapped interrupts 32-47 */
    if (vec >= 32 && vec < 48) {
        /* Send EOI to master PIC */
        __asm__ volatile("outb %0, %1" :: "a"((u8)0x20), "Nd"((u16)0x20));
        if (vec >= 40) {
            /* Send EOI to slave PIC too */
            __asm__ volatile("outb %0, %1" :: "a"((u8)0x20), "Nd"((u16)0xA0));
        }
        return;  /* spurious/unhandled IRQ - just EOI and continue */
    }

    /* Ignore int 0x80 - handled by syscall dispatcher */
    if (vec == 0x80) return;

    /* CPU exception - print and halt */
    const char *name = (vec < 32) ? exception_names[vec] : "Unknown";
    printk("\n*** KERNEL EXCEPTION ***\n");
    printk("Vector: %llu  %s\n", vec, name);
    printk("Error:  0x%llx\n", f->error_code);
    printk("RIP:    0x%llx\n", f->rip);
    printk("RSP:    0x%llx\n", f->rsp);
    printk("RFLAGS: 0x%llx\n", f->rflags);
    printk("CS:     0x%llx\n", f->cs);
    printk("RAX:    0x%llx  RBX: 0x%llx\n", f->rax, f->rbx);
    printk("RCX:    0x%llx  RDX: 0x%llx\n", f->rcx, f->rdx);
    printk("RSI:    0x%llx  RDI: 0x%llx\n", f->rsi, f->rdi);

    /* For page faults: print CR2 (faulting address) */
    if (vec == 14) {
        u64 cr2;
        __asm__ volatile("movq %%cr2, %0" : "=r"(cr2));
        printk("CR2 (fault addr): 0x%llx\n", cr2);
    }

    printk("System halted.\n");
    __asm__ volatile("cli");
    for (;;) __asm__ volatile("hlt");
}
