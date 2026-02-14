/**
 * IDT Setup for int 0x80 Syscall Handler
 */

#include "ark/types.h"
#include "ark/printk.h"

/**
 * IDT Entry structure (8 bytes)
 */
typedef struct {
    u16 offset_low;    /* Low 16 bits of handler address */
    u16 selector;      /* Kernel code segment selector (0x08) */
    u8  reserved;      /* Reserved, set to 0 */
    u8  type_attr;     /* Type=0xE (interrupt), DPL=3 (all), Present=1 = 0xEE */
    u16 offset_high;   /* High 16 bits of handler address */
} __attribute__((packed)) idt_entry_t;

/**
 * IDT Descriptor (for LIDT instruction)
 */
typedef struct {
    u16 limit;
    u32 base;
} __attribute__((packed)) idt_descriptor_t;

/* IDT table */
extern idt_entry_t g_idt[];  /* Array of IDT entries */
extern idt_descriptor_t g_idt_desc;
extern void int80_syscall_stub(void);

/**
 * Initialize IDT with int 0x80 syscall handler
 */
void idt_init(void) {
    printk("[idt_init] Initializing IDT for int 0x80 syscalls\n");
    
    idt_entry_t *entry = &g_idt[0x80];
    u32 handler = (u32)&int80_syscall_stub;
    
    printk("[idt_init] Handler: 0x%x, Entry: 0x%x\n", handler, (u32)entry);
    
    /* Set IDT entry for int 0x80 */
    entry->offset_low = (u16)(handler & 0xFFFF);
    entry->offset_high = (u16)((handler >> 16) & 0xFFFF);
    entry->selector = 0x08;  /* Kernel code segment */
    entry->reserved = 0;
    entry->type_attr = 0xEE; /* Interrupt gate, ring 3 (all privileges) */
    
    printk("[idt_init] IDT entry: low=0x%x high=0x%x sel=0x%x attr=0x%x\n",
           entry->offset_low, entry->offset_high, entry->selector, entry->type_attr);
    
    /* Load IDT */
    __asm__ __volatile__("lidt %0" :: "m" (g_idt_desc));
    
    printk("[idt_init] IDT loaded, ready for int 0x80\n");
}
