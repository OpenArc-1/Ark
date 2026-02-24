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
        kernel_page_directory[i] = 0x00000000; // Not present, no permissions
    }

    // 2. Identity Map 0MB to 4MB (Index 0)
    // We need this because the BIOS/Bootloader info and often the Stack are here
    for(uint32_t i = 0; i < 1024; i++) {
        low_identity_table[i] = (i * 4096) | PAGE_PRESENT | PAGE_RW; // Present, R/W
    }
    kernel_page_directory[0] = ((uint32_t)low_identity_table & ~0xFFF) | PAGE_PRESENT | PAGE_RW;

    // 3. Identity Map 4MB to 8MB (Index 1) - KERNEL LOCATION
    // Maps virtual 0x400000-0x7FFFFF to physical 0x400000-0x7FFFFF
    uint32_t kernel_base = (uint32_t)0x400000; 
    for(uint32_t i = 0; i < 1024; i++) {
        kernel_identity_table[i] = (kernel_base + (i * 4096)) | PAGE_PRESENT | PAGE_RW;
    }
    kernel_page_directory[1] = ((uint32_t)kernel_identity_table & ~0xFFF) | PAGE_PRESENT | PAGE_RW;

    // 4. Setup Recursive Mapping (The "Power" feature)
    // This maps the directory to itself at the very end of virtual memory at 0xFFFFF000
    // PDE[1023] points to the page directory itself, allowing us to modify page tables at runtime
    uint32_t pd_phys = ((uint32_t)kernel_page_directory) & ~0xFFF; // Mask to page boundary
    kernel_page_directory[1023] = pd_phys | PAGE_PRESENT | PAGE_RW;

    // 5. Load CR3 with physical address of the page directory (MUST be page-aligned)
    uint32_t cr3_val = ((uint32_t)kernel_page_directory) & ~0xFFF;
    asm volatile("mov %0, %%cr3" :: "r"(cr3_val) : "memory");

    // Flush the TLB just in case
    asm volatile("invlpg (0)" :: : "memory");

    // 6. The "Moment of Truth" - Enable Paging (Set PG bit in CR0)
    uint32_t cr0;
    asm volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= 0x80000000; // Set PG bit
    asm volatile("mov %0, %%cr0" :: "r"(cr0) : "memory");
    
    // After this line, paging is active!
    // Execute a serializing instruction to ensure paging is fully enabled
    asm volatile("jmp 1f; 1:" ::: "memory");
}

/**
 * map_page: Map a single virtual page to a physical page
 * virt: Virtual address (will be aligned to page boundary)
 * phys: Physical address (will be aligned to page boundary)
 * flags: Page table entry flags (PAGE_PRESENT, PAGE_RW, PAGE_USER, etc.)
 */
void map_page(uint32_t virt, uint32_t phys, uint32_t flags) {
    uint32_t virt_aligned = virt & ~0xFFF;  // Align to page boundary
    uint32_t phys_aligned = phys & ~0xFFF;  // Align to page boundary

    uint32_t pd_idx = virt_aligned >> 22;   // PDE index
    uint32_t pt_idx = (virt_aligned >> 12) & 0x3FF;  // PTE index within page table

    // Access the page directory using recursive mapping at 0xFFFFF000
    uint32_t* pd_virt = (uint32_t*)VIRT_PAGE_DIR;
    
    // Check if page table exists for this page directory entry
    if (!(pd_virt[pd_idx] & PAGE_PRESENT)) {
        // Page table doesn't exist - this is a limitation of current implementation
        // In a real kernel, we'd allocate a new page table here
        return;  // Silently fail for now
    }

    // Access the page table using recursive mapping
    // Each page table is accessible at 0xFFC00000 + (pd_idx * 0x1000)
    uint32_t* pt_virt = (uint32_t*)(VIRT_PAGE_TABLES + (pd_idx * PAGE_SIZE));
    
    // Update the page table entry
    pt_virt[pt_idx] = phys_aligned | (flags | PAGE_PRESENT);
    
    // Flush the TLB entry for this virtual address
    asm volatile("invlpg (%0)" :: "r"(virt_aligned) : "memory");
}

/**
 * map_region: Map a contiguous memory region
 * virt: Virtual address (will be aligned down to page boundary)
 * phys: Physical address (will be aligned down to page boundary)
 * size: Size in bytes
 * flags: Page table entry flags
 */
void map_region(uint32_t virt, uint32_t phys, uint32_t size, uint32_t flags) {
    uint32_t virt_aligned = virt & ~0xFFF;
    uint32_t phys_aligned = phys & ~0xFFF;
    uint32_t end = virt + size;
    
    for (uint32_t v = virt_aligned; v < end; v += PAGE_SIZE) {
        map_page(v, phys_aligned + (v - virt_aligned), flags);
    }
}

/**
 * get_phys_addr: Translate virtual address to physical address
 * Returns 0 if the virtual address is not mapped
 */
uint32_t get_phys_addr(uint32_t virt) {
    uint32_t pd_idx = virt >> 22;
    uint32_t pt_idx = (virt >> 12) & 0x3FF;
    
    uint32_t* pd_virt = (uint32_t*)VIRT_PAGE_DIR;
    
    if (!(pd_virt[pd_idx] & PAGE_PRESENT)) {
        return 0;  // Page directory entry not present
    }
    
    uint32_t* pt_virt = (uint32_t*)(VIRT_PAGE_TABLES + (pd_idx * PAGE_SIZE));
    uint32_t pte = pt_virt[pt_idx];
    
    if (!(pte & PAGE_PRESENT)) {
        return 0;  // Page table entry not present
    }
    
    return (pte & ~0xFFF) | (virt & 0xFFF);  // Physical address + page offset
}