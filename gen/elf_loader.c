/**
 * Simple ELF binary loader for executing userspace binaries
 * 
 * Parses ELF headers and executes loaded binaries from memory
 */

#include "ark/types.h"
#include "ark/printk.h"

/* Forward declaration for busy delay */
static void busy_delay(u32 loops) {
    for (volatile u32 i = 0; i < loops; ++i) {
        __asm__ __volatile__("");
    }
}

/* Minimal ELF structures */
#define ELF_MAGIC 0x464c457f  /* "\x7fELF" */

typedef struct {
    u32 magic;           /* ELF magic number */
    u8 class;            /* 32-bit or 64-bit */
    u8 data;             /* Endianness */
    u8 version;
    u8 os_abi;
    u8 abi_version;
    u8 pad[7];
    u16 type;
    u16 machine;
    u32 version2;
    u32 entry;           /* Entry point address */
    u32 phoff;           /* Program header offset */
    u32 shoff;
    u32 flags;
    u16 ehsize;
    u16 phentsize;       /* Program header entry size */
    u16 phnum;           /* Number of program headers */
    u16 shentsize;
    u16 shnum;
    u16 shstrndx;
} elf_header_t;

/**
 * Execute an ELF binary loaded in memory
 * 
 * This loader can handle:
 * 1. Raw binary blobs (jumps to offset 0)
 * 2. ELF binaries (parses headers and finds entry point)
 * 
 * @param binary Pointer to binary data in memory
 * @param size Total size of the binary
 * @return Exit code from the binary (if it returns)
 */
int elf_execute(u8 *binary, u32 size) {
    if (!binary || size < 4) {
        printk("[elf] Error: Invalid binary or too small (%u bytes)\n", size);
        return -1;
    }
    
    elf_header_t *elf = (elf_header_t *)binary;
    
    /* Check if this is an ELF binary */
    if (elf->magic == ELF_MAGIC) {
        printk("[elf] Found ELF binary\n");
        printk("[elf]   Entry: 0x%x, Binary: 0x%x\n", elf->entry, (u32)binary);
        
        /* Entry point might be:
         * - Linked at 0x1000 (typical for freestanding)
         * - Or other fixed address
         * We need to calculate: actual_entry = binary_load_addr + (entry - linked_text_start)
         */
        
        u8 *entry_ptr = binary;
        u32 entry = elf->entry;
        
        /* If entry < 0x10000, it's likely a relative small offset */
        if (entry < 0x10000) {
            entry_ptr = binary + entry;
        } else if (entry >= 0x1000 && entry < 0x100000) {
            /* Likely linked with -Ttext address, calculate offset */
            /* entry = 0x1000 + offset, so offset = entry - 0x1000 */
            u32 offset = entry - 0x1000;
            entry_ptr = binary + offset;
        } else {
            /* Fallback: use directly */
            entry_ptr = (u8 *)entry;
        }
        
        printk("[elf] Calculated entry: 0x%x\n", (u32)entry_ptr);
        printk("[elf] Executing ELF...\n\n");
        busy_delay(100000);
        
        typedef void (*entry_func_t)(void);
        entry_func_t entry_func = (entry_func_t)entry_ptr;
        entry_func();
        
        return 0;
    } else {
        /* Treat as raw binary */
        printk("[elf] Found raw binary\n");
        printk("[elf] Executing at 0x%x...\n\n", (u32)binary);
        busy_delay(100000);
        
        typedef void (*entry_func_t)(void);
        entry_func_t entry_func = (entry_func_t)binary;
        entry_func();
        
        return 0;
    }
}

