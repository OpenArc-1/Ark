# Script-Based Init System

The Ark kernel now supports a script-based init system that scans ramfs for scripts tagged with `#!init` and executes them.

## How It Works

1. **Script Format**: Create a script file starting with `#!init`
2. **File Directive**: Use `file:/path/to/binary` to specify which binary to execute
3. **Loading**: Load both the script and binary as multiboot modules
4. **Execution**: The kernel scanner finds the script and executes the specified binary

## Example Script

Create a file named `init` with this content:

```
#!init

file:/init.bin
```

## Running with Script

### Method 1: Using Makefile (Recommended)

```bash
make run-with-script
```

This will:
- Build `bzImage` and `init.bin` if needed
- Load both `init` (script) and `init.bin` (binary) as multiboot modules
- The kernel scanner will find the `#!init` script and execute `/init.bin`

### Method 2: Manual QEMU Command

```bash
qemu-system-i386 -m 256M -kernel bzImage -initrd init,init.bin -nographic
```

## How Modules Are Loaded

When QEMU loads modules via `-initrd`, they are:
1. Loaded into memory by the bootloader
2. Registered in ramfs by `modules_load_from_multiboot()`
3. Scanned by `script_scan_and_execute()` for `#!init` tags
4. The script parser extracts `file:/` directives
5. The specified binary is loaded and executed via `elf_execute()`

## Module Names

QEMU may assign default names to modules. The script scanner iterates through **all** files in ramfs, so it will find your script regardless of its name. However, for clarity, you can ensure your script file is named `init` or placed in a predictable location.

## Troubleshooting

- **Script not found**: Check that the script file starts with `#!init` (exactly)
- **Binary not found**: Ensure the `file:/` path matches the module name in ramfs
- **Module loading**: Check kernel logs for `[modules]` and `[ramfs]` messages
- **Script scanning**: Look for `[script]` messages in kernel output

## Example Output

When working correctly, you should see:

```
[modules] Found 2 module(s) from bootloader
[modules] Loading module 1: /init (X bytes @ 0x...)
[modules] Loading module 2: /init.bin (Y bytes @ 0x...)
[ramfs] Files in ramfs (2 total):
[ramfs]   - /init (X bytes)
[ramfs]   - /init.bin (Y bytes)
[script] Scanning ramfs for #!init scripts...
[script] Checking file: /init (X bytes)
[script] Found #!init tag
[script] Found file:/ directive: /init.bin
[script] Loading binary: /init.bin (Y bytes)
[script] Executing binary via elf_execute...
```
