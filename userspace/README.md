# Ark Userspace Shell

A working shell program that uses existing kernel modules via syscalls. The shell works with both VGA and serial output (handled automatically by the kernel's `printk` function).

## Architecture

### Kernel Syscalls
The kernel provides the following syscalls via `int 0x80`:
- **SYS_READ (0)**: Read from stdin (keyboard/serial)
- **SYS_WRITE (1)**: Write to stdout/stderr (VGA + serial)
- **SYS_EXIT (60)**: Exit program

### Userspace Components

1. **`syscall.h`**: Wrapper functions for making syscalls
   - `read()`, `write()`, `exit()` functions
   - Uses inline assembly to call `int 0x80`

2. **`shell.c`**: Main shell program
   - Command parser and executor
   - Built-in commands: `help`, `echo`, `clear`, `exit`
   - Uses kernel modules via syscalls

3. **`start.S`**: Entry point
   - Sets up stack
   - Calls `shell_main()`
   - Handles program exit

## How It Works

1. **Output (VGA + Serial)**: 
   - Shell calls `write()` syscall
   - Kernel's `syscall_write()` uses `printk()`
   - `printk()` automatically outputs to both VGA and serial

2. **Input (Keyboard + Serial)**:
   - Shell calls `read()` syscall
   - Kernel's `syscall_read()` uses `input_getc()`
   - `input_getc()` reads from both keyboard and serial

3. **No New Modules**:
   - Uses existing `printk` for output
   - Uses existing `input` subsystem for input
   - All communication via syscalls

## Building

The shell is built as part of `init.bin`:

```bash
make init.bin
```

This compiles:
- `userspace/shell.c` → shell program
- `userspace/start.S` → entry point
- Links with `userspace/linker.ld` → creates ELF binary

## Running

### Method 1: Direct execution
```bash
make run-with-init
```

### Method 2: With script system
```bash
make run-with-script
```

The script (`init`) contains:
```
#!init
file:/init.bin
```

## Shell Commands

- `help` or `?` - Show available commands
- `echo <args>` - Echo arguments
- `clear` - Clear screen
- `exit` - Exit shell

## Example Session

```
ark> help
Ark Shell - Available commands:
  help     - Show this help message
  echo     - Echo arguments
  clear    - Clear screen
  exit     - Exit shell

ark> echo Hello, Ark!
Hello, Ark!

ark> clear
[Screen clears]

ark> exit
Goodbye!
```

## Technical Details

### Syscall Interface

```c
// Read from stdin
int read(int fd, char *buf, unsigned int count);

// Write to stdout/stderr  
int write(int fd, const char *buf, unsigned int count);

// Exit program
void exit(int code);
```

### Memory Layout

- **Text**: 0x1000 (code)
- **Data**: 0x2000 (data/bss)
- **Stack**: 0x5000 (grows downward)

### Output Channels

The kernel's `printk()` function automatically handles:
- **VGA**: Text mode at 0xB8000
- **Serial**: COM1 at 0x3F8

Both are active simultaneously, so the shell works with either display method.

## Extending the Shell

To add new commands, edit `userspace/shell.c` and add a new command handler:

```c
static int cmd_mycommand(int argc, char **argv) {
    print("My command executed!\n");
    return 0;
}
```

Then add it to `execute_command()`:

```c
} else if (strcmp(argv[0], "mycommand") == 0) {
    return cmd_mycommand(argc, argv);
```
