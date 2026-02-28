/**
 * Simple ELF binary loader for userspace binaries
 * Supports 32-bit ELF, program headers, and raw binaries
 */

#include "ark/types.h"
#include "ark/printk.h"
#include "ark/init_api.h"

/* ELF structures */
#define ELF_MAGIC 0x464c457f  /* "\x7fELF" */

typedef struct {
    u32 magic;
    u8 class;
    u8 data;
    u8 version;
    u8 os_abi;
    u8 abi_version;
    u8 pad[7];
    u16 type;
    u16 machine;
    u32 version2;
    u32 entry;
    u32 phoff;
    u32 shoff;
    u32 flags;
    u16 ehsize;
    u16 phentsize;
    u16 phnum;
    u16 shentsize;
    u16 shnum;
    u16 shstrndx;
} elf_header_t;

typedef struct {
    u32 type;
    u32 offset;
    u32 vaddr;
    u32 paddr;
    u32 filesz;
    u32 memsz;
    u32 flags;
    u32 align;
} elf_phdr_t;

/* Load and execute ELF binary in memory */
int elf_execute(u8 *binary, u32 size, const ark_kernel_api_t *api) {
    if (!binary || size < 4) {
        printk(T,"Invalid binary or too small (%u bytes)\n", size);
        return -1;
    }

    elf_header_t *elf = (elf_header_t *)binary;

    /* Check ELF magic */
    if (elf->magic != ELF_MAGIC) {
        /* Treat as raw binary */
        printk(T, "Raw binary, executing at 0x%x\n", (u32)binary);
        if (api) {
            ark_init_entry_t entry_func = (ark_init_entry_t)binary;
            return entry_func(api);
        } else {
            typedef void (*entry_func_t)(void);
            entry_func_t entry_func = (entry_func_t)binary;
            entry_func();
        }
        return 0;
    }

    printk(T, "ELF binary found, entry: 0x%x, phnum=%d\n", elf->entry, elf->phnum);

    /* Load program headers into memory and zero BSS */
    elf_phdr_t *ph = (elf_phdr_t *)(binary + elf->phoff);
    for (u16 i = 0; i < elf->phnum; ++i) {
        if (ph[i].type != 1) continue;  // PT_LOAD only

        /* destination address is the paddr (physical); this matches the
           simple identity mapping used by the kernel. */
        u8 *src  = binary + ph[i].offset;
        u8 *dest = (u8 *)ph[i].paddr;

        /* copy file data */
        for (u32 j = 0; j < ph[i].filesz; ++j)
            dest[j] = src[j];

        /* zero any remaining memory (BSS) */
        if (ph[i].memsz > ph[i].filesz) {
            for (u32 j = ph[i].filesz; j < ph[i].memsz; ++j)
                dest[j] = 0;
            printk(T, "Zeroed BSS for segment %d: %u bytes\n", i,
                   ph[i].memsz - ph[i].filesz);
        }

        printk(T, "Loaded segment %d: mem=0x%x..0x%x\n", i,
               (u32)dest, (u32)(dest + ph[i].memsz));
    }

    /* entry point is just the ELF header entry field (physical address) */
    u32 actual_entry = elf->entry;
    printk(T, "Jumping to entry: 0x%x\n", actual_entry);
    
    /* Enable IOPL=3 to allow I/O port access (PS/2 mouse) */
    __asm__ __volatile__(
        "pushfl\n\t"           /* Save current EFLAGS */
        "popl %%eax\n\t"       /* Load into eax */
        "orl $0x3000, %%eax\n\t"  /* Set IOPL bits (12-13) to 11 = level 3 */
        "pushl %%eax\n\t"      /* Push modified EFLAGS */
        "popfl\n\t"            /* Load back into EFLAGS */
        ::: "eax", "memory"
    );
    
    /* Pass kernel API pointer as first argument (ark_init_entry_t convention)
       The kernel API is available, so use it */
    if (api) {
        ark_init_entry_t entry_func = (ark_init_entry_t)actual_entry;
        int result = entry_func(api);
        printk(T, "Program returned: %d\n", result);
        return result;
    } else {
        /* Fallback: call with standard argc/argv if no API */
        typedef int (*entry_func_t)(int, char **);
        entry_func_t entry_func = (entry_func_t)actual_entry;
        int result = entry_func(0, NULL);
        printk(T, "Program returned: %d\n", result);
        return result;
    }
}
