# Building with ark-gcc

arkgcc is the Ark kernel C compiler, built on GCC with kernel-specific configurations.

## Installation

```bash
# ark-gcc should be in your PATH
which ark-gcc
```

## Compilation Modes

### 1. Freestanding Kernels & Bootloaders

For code that runs without OS support (kernel, bootloader, modules):

```bash
ark-gcc -ffreestanding -nostdlib -nostdinc -o kernel kernel.c
```

**Flags explained:**
- `-ffreestanding`: Don't assume stdlib is available
- `-nostdlib`: Don't link against libc/libgcc
- `-nostdinc`: Don't search standard include directories

### 2. Userspace Programs (with init_api)

For programs that run via `init.bin` and receive kernel API:

```bash
ark-gcc -o myapp myapp.c
```

These can use parts of stdlib (malloc, string functions, etc.) automatically provided by the kernel.

### 3. Dynamic Kernel Modules (DKM)

For runtime-loadable kernel modules:

```bash
# Standalone module (no kernel API)
ark-gcc -ffreestanding -nostdlib -Wl,-e,module_init \
    -o mymodule.elf mymodule.c

# Module with kernel API access
ark-gcc -ffreestanding -nostdlib -Wl,-e,module_init \
    -I include -o mymodule.elf mymodule.c
```

### 4. Scripts and Init Programs

For shell scripts and init interpreters:

```bash
ark-gcc -ffreestanding -nostdlib -o /bin/sh shell.c
```

## Common Compilation Examples

### Hello World Kernel Module

```c
#include "ark/types.h"
#include "ark/init_api.h"

int module_init(const ark_kernel_api_t *api) {
    api->printk("Hello from module!\n");
    return 0;
}
```

Compile:
```bash
ark-gcc -ffreestanding -nostdlib -Wl,-e,module_init \
    -I include -o hello.elf hello.c
```

### Userspace Application with I/O

```c
#include "ark/types.h"
#include "ark/init_api.h"

int main(int argc, char **argv) {
    const ark_kernel_api_t *api = ark_kernel_api();
    api->printk("Arguments: %d\n", argc);
    return 0;
}
```

Compile:
```bash
ark-gcc -o myapp myapp.c
```

### Minimal C Library Replacement

For modules that need string operations:

```c
#include "ark/types.h"

static void my_strcmp(const char *a, const char *b) {
    while (*a && *b && *a == *b) { a++; b++; }
    return *a - *b;
}

int module_init(void) {
    if (my_strcmp(arg, "test") == 0) {
        /* do something */
    }
    return 0;
}
```

## Troubleshooting

### "undefined reference to `memcpy`"
You're using libc functions. Add `-ffreestanding` or provide your own:

```c
static void *my_memcpy(void *dst, const void *src, int n) {
    char *d = dst;
    const char *s = src;
    while (n--) *d++ = *s++;
    return dst;
}
```

### "undefined reference to `printk`"
Modules need kernel API:

```c
#include "ark/init_api.h"
int module_init(const ark_kernel_api_t *api) {
    api->printk("Hello\n");
    return 0;
}
```

### "relocation R_386_RELATIVE not allowed"
Your module needs PIC (Position-Independent Code). Ensure:
- `-fPIC` is used
- Entry point is at known offset
- All references are relative

## Building & Testing Modules

1. **Build**: `ark-gcc -ffreestanding -nostdlib -Wl,-e,module_init -o mod.elf mod.c`
2. **Add to initramfs**: Copy to staging directory
3. **Load**: `strapper load /mod.elf`
4. **Check logs**: `dmesg` or kernel console output

## Performance Flags

For production modules:

```bash
ark-gcc -O2 -ffreestanding -nostdlib -Wl,-e,module_init \
    -Wall -Wextra -o module.elf module.c
```

Available optimizations:
- `-O0`: No optimization (debugging)
- `-O1`: Basic optimization
- `-O2`: Moderate optimization
- `-O3`: Aggressive optimization (may increase code size)
- `-Os`: Optimize for size

## See Also

- `ks/dkmload/README.md` - DKM loader guide
- `ks/dkmload/Makefile` - Example build recipes
- `START-HERE.md` - Ark kernel quick start
