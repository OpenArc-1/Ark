#ifndef PAGING_H
#define PAGING_H

#include <stdint.h>
#include <stddef.h>

#define PAGE_SIZE          4096
#define PT_COUNT           1024
#define PD_COUNT           1024

// Page Table Entry Flags
#define PAGE_PRESENT       0x01
#define PAGE_RW            0x02
#define PAGE_USER          0x04
#define PAGE_PWT           0x08  // Write-through (Good for USB status rings)
#define PAGE_PCD           0x10  // Cache Disable (Essential for USB MMIO)
#define PAGE_ACCESSED      0x20
#define PAGE_DIRTY         0x40

// Recursive Mapping: The last PDE points to the PD itself.
// This allows us to access Page Tables at a fixed virtual address.
#define RECURSIVE_SLOT     1023
#define VIRT_PAGE_TABLES   (0xFFC00000)
#define VIRT_PAGE_DIR      (0xFFFFF000)

typedef uint32_t pte_t;
typedef uint32_t pde_t;

void init_paging();
void map_page(uint32_t virt, uint32_t phys, uint32_t flags);
void map_region(uint32_t virt, uint32_t phys, uint32_t size, uint32_t flags);
uint32_t get_phys_addr(uint32_t virt);

#endif