# Ark Kernel Script System

The Ark kernel supports a script-based init system that scans ramfs for scripts tagged with `#!init`.

## Script Format

Scripts must follow this format:

```
#!init
file:/path/to/binary
```

### Example Script (`ks/demo.init`):

```
#!init
# Demo init script for Ark kernel
# This script tells the kernel to load and execute /init.bin

file:/init.bin
```

## How to Load Scripts

Scripts are loaded into ramfs via Multiboot modules. You can pass them to QEMU using the `-initrd` option or as multiboot modules.

### Method 1: Using QEMU with Multiboot Modules

```bash
# Build the kernel and init.bin
make bzImage init.bin

# Run with script and binary as modules
qemu-system-i386 -kernel bzImage \
    -initrd init,init.bin \
    -m 256M \
    -nographic
```

**Note:** The module names determine the filename in ramfs:
- `init` → `/init` (the script file)
- `init.bin` → `/init.bin` (the binary to execute)

### Method 2: Using a Custom Makefile Target

Add this to your Makefile:

```makefile
run-with-script: bzImage init.bin ks/demo.init
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage \
		-initrd ks/demo.init,init.bin \
		-nographic -m 256M
```

Then run:
```bash
make run-with-script
```

### Method 3: Using GRUB Multiboot Modules

If using GRUB, add modules to your `grub.cfg`:

```
menuentry 'Ark OS' {
    multiboot /bzImage
    module /init /init
    module /init.bin /init.bin
}
```

## How It Works

1. **Bootloader loads modules** → Files are loaded into ramfs with their specified paths
2. **Kernel scans ramfs** → The script scanner (`ks/script.c`) iterates through all files
3. **Script detection** → Files starting with `#!` are checked for `#!init` tag
4. **Directive parsing** → The script parser looks for `file:/path` directives
5. **Binary execution** → The specified binary is loaded from ramfs and executed using `elf_execute()`

## Script Features

- **Shebang detection**: Scripts must start with `#!init`
- **File directives**: Use `file:/path` to specify which binary to execute
- **Comments**: Lines starting with `#` (after shebang) are ignored
- **Multiple files**: You can load multiple scripts, but only the first `#!init` script found will be executed

## Example Workflow

1. Create your script file:
   ```bash
   echo "#!init" > my-init.script
   echo "file:/init.bin" >> my-init.script
   ```

2. Build your kernel and binary:
   ```bash
   make bzImage init.bin
   ```

3. Run with QEMU:
   ```bash
   qemu-system-i386 -kernel bzImage \
       -initrd my-init.script,init.bin \
       -m 256M -nographic
   ```

4. The kernel will:
   - Load both files into ramfs
   - Scan for `#!init` scripts
   - Find `my-init.script`
   - Parse the `file:/init.bin` directive
   - Execute `/init.bin` from ramfs

## Troubleshooting

- **Script not found**: Make sure the script file is passed as a multiboot module
- **Binary not found**: Ensure the binary path in the script matches the module name
- **Script format error**: Check that the script starts with `#!init` and has a `file:/` directive
- **Execution fails**: Verify the binary is a valid ELF file

## Files

- `ks/script.c` - Script scanner implementation
- `ks/demo.init` - Example script file
- `include/ark/script.h` - Script scanner API
