# Init.bin Kernel API

This document describes the **kernel API** exposed to `init.bin` (and other binaries launched via the init loader). The kernel passes a versioned **service table** (`ark_kernel_api_t`) to your entry point. Use it for `printk`, input, RTC time, CPU info, **VFS** (real filesystem), and **TTY** (sessions, Linux-like debug)—no direct kernel symbol access required.

---

## Overview

- **Entry point**: The kernel calls your ELF `e_entry` as  
  `int entry(const ark_kernel_api_t *api)`  
  and passes a pointer to the API table. Your return value is treated as an exit code.

- **API table**: `ark_kernel_api_t` holds function pointers for logging, input, time, CPU, **VFS**, and **TTY**. Check `api->version >= ARK_INIT_API_VERSION` before use. **Version 2** adds VFS and TTY.

- **Include**: Use `#include "ark/init_api.h"`. You need the `include/` path and `ark/types.h`, `ark/time.h` (for `rtc_time_t`).

---

## Entry Point Signature

```c
#include "ark/init_api.h"

int _start(const ark_kernel_api_t *api) {
    if (!api || api->version != ARK_INIT_API_VERSION)
        return 1;

    /* use api->printk, api->input_*, api->read_rtc, api->cpuid, api->get_cpu_vendor */
    return 0;
}
```

Use `_start` (or whatever your linker script sets as `ENTRY`) so the ELF `e_entry` matches this function.

---

## Version Check

```c
#define ARK_INIT_API_VERSION 2
```

Always verify `api && api->version >= ARK_INIT_API_VERSION` before calling any API members. Use `>= 2` if you rely on VFS or TTY. If the check fails, return a non‑zero exit code.

---

## API Reference

### Logging — `printk`

```c
int (*printk)(const char *fmt, ...);
```

Same contract as the kernel `printk`: format string plus variadic args. Supports `%s`, `%d`, `%u`, `%x`, `%X`, `%c`, `%%`. Output goes to both VGA and serial.

**Example:**

```c
api->printk("[init] hello, api v%u\n", api->version);
api->printk("hex: 0x%08x\n", 0x1234abcd);
```

---

### Input — `input_has_key`

```c
bool (*input_has_key)(void);
```

Returns `true` if a key is available (keyboard or serial) **without** blocking.

**Example:**

```c
if (api->input_has_key())
    /* key ready */
```

---

### Input — `input_getc`

```c
char (*input_getc)(void);
```

**Blocks** until a key is pressed. Returns the character or a special code:

- Printable ASCII
- `'\n'` (0x0A) — Enter
- `'\b'` (0x08) — Backspace
- `ARROW_UP` (1), `ARROW_DOWN` (2), `ARROW_LEFT` (3), `ARROW_RIGHT` (4) — from `ark/input.h` if you include it

**Example:**

```c
char c = api->input_getc();
if (c == '\n')
    /* enter pressed */
```

---

### Input — `input_read`

```c
void (*input_read)(char *buffer, int max_len, bool hide_input);
```

Reads a **line** of input (until Enter). `buffer` is null‑terminated. `max_len` is the buffer size including the `'\0'`. If `hide_input` is `true`, input is echoed as `*` (e.g. passwords).

**Example:**

```c
char line[128];
api->input_read(line, sizeof(line), false);
api->printk("You typed: %s\n", line);
```

---

### Time — `read_rtc`

```c
rtc_time_t (*read_rtc)(void);
```

Returns the current RTC date/time. `rtc_time_t` is defined in `ark/time.h`:

```c
typedef struct {
    uint8_t sec, min, hour, day, month;
    uint16_t year;   /* e.g. 2025 */
} rtc_time_t;
```

**Example:**

```c
rtc_time_t t = api->read_rtc();
api->printk("%02u:%02u:%02u %02u/%02u/%u\n",
            (unsigned)t.hour, (unsigned)t.min, (unsigned)t.sec,
            (unsigned)t.day, (unsigned)t.month, (unsigned)t.year);
```

---

### CPU — `cpuid`

```c
void (*cpuid)(u32 leaf, u32 subleaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx);
```

Executes `CPUID` with the given `leaf` and `subleaf`. Results are written into the non‑NULL pointers. Pass `NULL` for any register you don’t need.

**Example:**

```c
u32 eax = 0, edx = 0;
api->cpuid(1, 0, &eax, NULL, NULL, &edx);
api->printk("cpuid(1).eax=0x%08x edx=0x%08x\n", eax, edx);
```

---

### CPU — `get_cpu_vendor`

```c
void (*get_cpu_vendor)(char out_13[13]);
```

Writes the 12‑byte CPU vendor string (e.g. `"GenuineIntel"`, `"AuthenticAMD"`) into `out_13`, followed by `'\0'`. The buffer must be at least 13 bytes.

**Example:**

```c
char vendor[13];
api->get_cpu_vendor(vendor);
api->printk("CPU: %s\n", vendor);
```

---

## VFS (real FS) — API v2

| Member | Purpose |
|--------|---------|
| `vfs_open` | Open file; returns fd or -1 |
| `vfs_read` | Read from fd into buffer |
| `vfs_close` | Close fd |
| `vfs_file_size` | Size of open file |
| `vfs_file_exists` | 1 if path exists |
| `vfs_list_count` | Number of files at path (only `"/"` supported) |
| `vfs_list_at` | Copy filename at index into buffer |

Use these for **real filesystem** access (ramfs root, and FAT32 when mounted). Example: `ls` uses `vfs_list_count("/")` and `vfs_list_at`; `cat` uses `vfs_open` / `vfs_read` / `vfs_close`.

---

## TTY / sessions — API v2

| Member | Purpose |
|--------|---------|
| `tty_alloc` | Allocate new session; returns session ID or (u32)-1 |
| `tty_free` | Release session |
| `tty_current` | Current session ID |
| `tty_switch` | Set current session |
| `tty_get_name` | Write e.g. `"tty0"` into buffer |
| `tty_valid` | 1 if session ID is allocated |
| `tty_debug` | Linux‑like debug print: `[ 0.XXXXXX] ttyN: fmt...` |

Each **session** has a unique ID (tty0, tty1, …). Use `tty_alloc` when starting a shell, `tty_debug` for dmesg‑style logs, and `tty_free` on exit.

---

## Building init.bin

- **Compiler**: `-m32 -ffreestanding -std=gnu99 -Iinclude` (or similar). No standard library.
- **Linker**: Use a custom linker script (e.g. `userspace/linker.ld`) with `ENTRY(_start)`. The kernel loader uses ELF program headers and `e_entry`.
- **Output**: ELF executable. The Makefile builds `init.bin` from `userspace/*.c` and `userspace/*.S`.

**Minimal linker script idea:**

```ld
ENTRY(_start)
SECTIONS {
  . = 0x1000;
  .text : { *(.text*) }
  .rodata : { *(.rodata*) }
  .data : { *(.data*) }
  .bss : { *(COMMON) *(.bss*) }
}
```

---

## Minimal Example

```c
#include "ark/init_api.h"

int _start(const ark_kernel_api_t *api) {
    if (!api || api->version < ARK_INIT_API_VERSION)
        return 1;

    api->printk("[init] Hello from init.bin\n");

    char vendor[13];
    api->get_cpu_vendor(vendor);
    api->printk("[init] CPU: %s\n", vendor);

    rtc_time_t t = api->read_rtc();
    api->printk("[init] RTC: %02u:%02u:%02u\n",
                (unsigned)t.hour, (unsigned)t.min, (unsigned)t.sec);

    api->printk("[init] Type something: ");
    char line[64];
    api->input_read(line, sizeof(line), false);
    api->printk("[init] Got: %s\n", line);

    return 0;
}
```

---

## How the Kernel Uses It

- **Direct init.bin**: When `/init.bin` is present in ramfs (e.g. from multiboot modules), the kernel loads it, resolves `e_entry`, and calls `entry(api)` with `ark_kernel_api()`. The return value is reported as the init exit code.
- **Script‑launched binaries**: Binaries started via `#!init` scripts (see `README_SCRIPT.md` / `README_SCRIPTS.md`) also receive the same `api` when executed through `elf_execute(..., ark_kernel_api())`.

---

## Summary Table

| Member | Purpose |
|--------|---------|
| `version` | API version; use `>= ARK_INIT_API_VERSION` |
| `printk` | Kernel log (VGA + serial) |
| `input_has_key` | Non‑blocking “key available?” |
| `input_getc` | Blocking read one key |
| `input_read` | Blocking read line |
| `read_rtc` | RTC date/time |
| `cpuid` | CPUID(leaf, subleaf) |
| `get_cpu_vendor` | 12‑char vendor string |
| `vfs_*` | VFS open/read/close/list (v2) |
| `tty_*` | TTY sessions and debug (v2) |

Use only what your init needs. Ensure `api` and `version` are valid before any use.
