#include <stdint.h>
#include "hw/pager.h"

// Defined in your linker script
extern uint8_t _kernel_start[];
extern uint8_t _kernel_end[];

// Aligning these is non-negotiable for x86 hardware
__attribute__((aligned(4096))) uint32_t kernel_page_directory[1024];
__attribute__((aligned(4096))) uint32_t low_identity_table[1024]; // 0-4MB
__attribute__((aligned(4096))) uint32_t kernel_identity_table[1024]; // 4-8MB

void init_paging() {
    // 1. Clear Directory
    for(int i = 0; i < 1024; i++) {
        kernel_page_directory[i] = 0x00000002; // Not present, Read/Write
    }

    // 2. Identity Map 0MB to 4MB (Index 0)
    // We need this because the BIOS/Bootloader info and often the Stack are here
    for(uint32_t i = 0; i < 1024; i++) {
        low_identity_table[i] = (i * 4096) | 3; // Present, R/W
    }
    kernel_page_directory[0] = ((uint32_t)low_identity_table) | 3;

    // 3. Identity Map 4MB to 8MB (Index 1) - YOUR KERNEL LOCATION
    // This prevents the reboot when the EIP (Instruction Pointer) is at 4MB+
    uint32_t kernel_base = (uint32_t)0x400000; 
    for(uint32_t i = 0; i < 1024; i++) {
        kernel_identity_table[i] = (kernel_base + (i * 4096)) | 3;
    }
    kernel_page_directory[1] = ((uint32_t)kernel_identity_table) | 3;

    // 4. Setup Recursive Mapping (The "Power" feature)
    // This maps the directory to itself at the very end of virtual memory
    kernel_page_directory[1023] = ((uint32_t)kernel_page_directory) | 3;

    // 5. Load CR3 (Physical address of the directory)
    asm volatile("mov %0, %%cr3" :: "r"(kernel_page_directory));

    // 6. The "Moment of Truth" - Enable Paging
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; // Set PG bit
    asm volatile("mov %0, %%cr0" :: "r"(cr0));
    
    // After this line, paging is active!
}

/**
 * map_page: Use this for USB drivers later.
 * Because of recursive mapping, we can access any page table via 0xFFC00000
 */
void map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t pd_idx = virt >> 22;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;

    // Access the page table using the recursive mapping trick
    uint32_t* pt_virt = (uint32_t*)(0xFFC00000 + (pd_idx * 4096));

    // If the table isn't there, you'll need to allocate one and add to kernel_page_directory
    // For now, we assume the tables for 0-8MB are already there.
    
    pt_virt[pt_idx] = (phys & ~0xFFF) | flags | 0x01; // Present
    asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
}