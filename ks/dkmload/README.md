# Dynamic Kernel Module (DKM) Loader

The DKM loader allows loading kernel modules at runtime, either from the kernel or via a userspace utility.

## Quick Start

### Building a Module

The simplest way to build a module:

```bash
# Standalone module (no kernel API needed)
ark-gcc -ffreestanding -nostdlib -Wl,-e,module_init -o mymodule.elf mymodule.c
```

### Loading a Module

From the kernel, call:
```c
dkm_load("/path/to/module.elf");
```

From userspace, use the strapper utility:
```bash
strapper load /ramdisk/mymodule.elf
strapper list
strapper unload mymodule
```

## Writing a Module

### Minimal Standalone Module

```c
#include "ark/types.h"

int module_init(void) {
    /* Module initialization code */
    return 0;  /* return 0 for success */
}
```

Compile with:
```bash
ark-gcc -ffreestanding -nostdlib -Wl,-e,module_init -o mymodule.elf mymodule.c
```

### Module with Kernel API Access

For modules that need access to kernel services:

```c
#include "ark/types.h"
#include "ark/init_api.h"

int module_init(const ark_kernel_api_t *api) {
    if (api && api->printk)
        api->printk("Module loaded!\n");
    return 0;
}
```

Compile with:
```bash
ark-gcc -ffreestanding -nostdlib -Wl,-e,module_init \
    -I /path/to/ark/include -o mymodule.elf mymodule.c
```

## Architecture

- **dkm_loader.c**: Core module loading implementation
  - Fixed 1MB pool for loadable modules
  - Supports up to 16 concurrent modules
  - Maps module files from VFS into memory and executes entry point

- **strapper.c**: Userspace loader utility
  - Minimal dependencies (no libc)
  - Commands: load, unload, list

- **sysc.c**: Kernel syscall handlers (future: add SYS_DKM_* syscalls)

- **init_k.c**: Kernel initialization hook

- **sample_module.c**: Example module showing both standalone and API modes

## Design Principles

1. **No relocations**: Modules must be position-independent or pre-linked for their target address
2. **Simple pool allocation**: Fixed-size buffer, no garbage collection
3. **Kernel API**: Modules use `ark_kernel_api_t` pointer like `init.bin`
4. **freestanding**: All modules compiled with `-ffreestanding -nostdlib`
5. **External compilation**: Modules can be built outside the main kernel tree

## Limitations

- No runtime relocations (modules must be position-independent)
- No unload cleanup (memory not reclaimed from pool)
- Limited to 16 modules and 1MB total pool size
- Modules must match 32-bit i386 ABI

## Future Enhancements

- [ ] Add SYS_DKM_LOAD/UNLOAD/LIST syscalls
- [ ] Implement basic relocation support
- [ ] Add module dependency tracking
- [ ] Memory reclamation on unload
- [ ] Module versioning and ABI compatibility checks
