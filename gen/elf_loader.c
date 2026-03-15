#include "ark/types.h"
#include "ark/printk.h"
#include "ark/init_api.h"

#define ELF_MAGIC 0x464c457f
#define ELFCLASS32 1
#define ELFCLASS64 2
#define ELFDATA2LSB 1

#define EM_386 3
#define EM_X86_64 62

#define PT_LOAD 1
#define USER_LOAD_BASE 0x1000



typedef struct {
    u32 magic;
    u8  class;
    u8  data;
    u8  version;
    u8  os_abi;
    u8  abi_version;
    u8  pad[7];
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
} elf32_header_t;

typedef struct {
    u32 type;
    u32 offset;
    u32 vaddr;
    u32 paddr;
    u32 filesz;
    u32 memsz;
    u32 flags;
    u32 align;
} elf32_phdr_t;



typedef struct {
    u32 magic;
    u8  class;
    u8  data;
    u8  version;
    u8  os_abi;
    u8  abi_version;
    u8  pad[7];
    u16 type;
    u16 machine;
    u32 version2;
    u64 entry;
    u64 phoff;
    u64 shoff;
    u32 flags;
    u16 ehsize;
    u16 phentsize;
    u16 phnum;
    u16 shentsize;
    u16 shnum;
    u16 shstrndx;
} elf64_header_t;

typedef struct {
    u32 type;
    u32 flags;
    u64 offset;
    u64 vaddr;
    u64 paddr;
    u64 filesz;
    u64 memsz;
    u64 align;
} elf64_phdr_t;


/* =====================================================
   Internal loader
   ===================================================== */

static u64 load_elf(u8 *binary, u32 size)
{
    if (!binary || size < 64)
        return 0;

    u32 magic = *(u32*)binary;

    if (magic != ELF_MAGIC)
        return (u64)binary;

    u8 elf_class = binary[4];

    

    if (elf_class == ELFCLASS32)
    {
        elf32_header_t *elf = (elf32_header_t*)binary;

        printk(T,"elf32: entry=0x%x phnum=%u\n",
            elf->entry, elf->phnum);

        for (u16 i=0;i<elf->phnum;i++)
        {
            elf32_phdr_t *ph =
                (elf32_phdr_t*)(binary + elf->phoff + i*elf->phentsize);

            if (ph->type != PT_LOAD)
                continue;

            u8 *src  = binary + ph->offset;
            u8 *dest = (u8*)(u64)ph->vaddr;

            for (u32 j=0;j<ph->filesz;j++)
                dest[j] = src[j];

            for (u32 j=ph->filesz;j<ph->memsz;j++)
                dest[j] = 0;

            printk(T,
                "elf32: seg %u loaded 0x%x-0x%x\n",
                i,
                ph->vaddr,
                ph->vaddr + ph->memsz
            );
        }

        return elf->entry;
    }

    

    if (elf_class == ELFCLASS64)
    {
        elf64_header_t *elf = (elf64_header_t*)binary;

        printk(T,"elf64: entry=%lx phnum=%u\n",
            elf->entry, elf->phnum);

        for (u16 i=0;i<elf->phnum;i++)
        {
            elf64_phdr_t *ph =
                (elf64_phdr_t*)(binary + elf->phoff + i*elf->phentsize);

            if (ph->type != PT_LOAD)
                continue;

            u8 *src  = binary + ph->offset;
            u8 *dest = (u8*)ph->vaddr;

            for (u64 j=0;j<ph->filesz;j++)
                dest[j] = src[j];

            for (u64 j=ph->filesz;j<ph->memsz;j++)
                dest[j] = 0;

            printk(T,
                "elf64: seg %u loaded %lx-%lx\n",
                i,
                ph->vaddr,
                ph->vaddr + ph->memsz
            );
        }

        return elf->entry;
    }

    return 0;
}


/* =====================================================
   Kernel API loader
   ===================================================== */

int elf_execute(u8 *binary, u32 size, const ark_kernel_api_t *api)
{
    u64 entry = load_elf(binary, size);

    if (!entry)
        return -1;

    if (api)
    {
        ark_init_entry_t fn = (ark_init_entry_t)entry;
        int rc = fn(api);

        printk(T,"elf: program exited %d\n",rc);
        return rc;
    }

    typedef int (*entry_t)(int,char**);
    return ((entry_t)entry)(0,NULL);
}


/* =====================================================
   argv loader
   ===================================================== */

int elf_execute_argv(u8 *binary, u32 size, int argc, char **argv)
{
    u64 entry = load_elf(binary, size);

    if (!entry)
        return -1;

    typedef int (*entry_t)(int,char**);

    int rc = ((entry_t)entry)(argc,argv);

    printk(T,"elf: argv program exited %d\n",rc);

    return rc;
}