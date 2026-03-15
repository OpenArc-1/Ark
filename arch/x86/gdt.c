#include <stdint.h>

// Minimal GDT entry
struct gdt_entry {
    uint16_t limit_low;
    uint16_t base_low;
    uint8_t  base_middle;
    uint8_t  access;
    uint8_t  granularity;
    uint8_t  base_high;
} __attribute__((packed));

struct gdt_ptr {
    uint16_t limit;
    uint32_t base;
} __attribute__((packed));

/* 5 entries: null, kernel code (0x08), kernel data (0x10),
 *            user code  (0x18), user data   (0x20).
 * boot.S loads selectors 0x08 and 0x10 immediately after the far jump,
 * and userspace uses 0x18/0x20.  A 3-entry table leaves those absent,
 * causing a #GP the moment any user segment register is loaded. */
struct gdt_entry gdt[5];
struct gdt_ptr gp;

void set_gdt_gate(int num, uint32_t base, uint32_t limit, uint8_t access, uint8_t gran) {
    gdt[num].base_low    = (base & 0xFFFF);
    gdt[num].base_middle = (base >> 16) & 0xFF;
    gdt[num].base_high   = (base >> 24) & 0xFF;
    gdt[num].limit_low   = (limit & 0xFFFF);
    gdt[num].granularity = ((limit >> 16) & 0x0F) | (gran & 0xF0);
    gdt[num].access      = access;
}

void init_gdt() {
    gp.limit = (sizeof(struct gdt_entry) * 5) - 1;
    gp.base = (uint32_t)&gdt;

    set_gdt_gate(0, 0, 0,          0x00, 0x00); /* Null               */
    set_gdt_gate(1, 0, 0xFFFFFFFF, 0x9A, 0xCF); /* Kernel Code  0x08  */
    set_gdt_gate(2, 0, 0xFFFFFFFF, 0x92, 0xCF); /* Kernel Data  0x10  */
    set_gdt_gate(3, 0, 0xFFFFFFFF, 0xFA, 0xCF); /* User Code    0x18  */
    set_gdt_gate(4, 0, 0xFFFFFFFF, 0xF2, 0xCF); /* User Data    0x20  */

    __asm__ volatile("lgdt (%0)" : : "r" (&gp));
    /* Reload segment registers with the new descriptors */
    __asm__ volatile(
        "ljmp $0x08, $1f\n"
        "1:\n"
        " mov $0x10, %ax\n"
        " mov %ax, %ds\n"
        " mov %ax, %es\n"
        " mov %ax, %fs\n"
        " mov %ax, %gs\n"
        " mov %ax, %ss\n"
    );
}
