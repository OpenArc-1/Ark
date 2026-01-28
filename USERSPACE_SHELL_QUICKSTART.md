# Userspace Shell - Quick Start Guide

This guide helps you get the Ark shell running in userspace and integrate it with the graphics system.

---

## What You Have

### 1. **Text-Based Shell** (`userspace/shell.c`)
- ✅ Ready to use immediately
- ✅ Works with current kernel API (v2)
- ✅ No graphics dependencies
- Commands: `ls`, `cat`, `echo`, `time`, `cpu`, `clear`, `help`, `exit`

### 2. **Graphics-Aware Shell** (`userspace/graphics_shell.c`)
- 📋 Demonstrates graphics integration
- 📋 Requires extended kernel API (v3)
- 📋 Shows how to use graphics from userspace
- Same commands with visual UI

### 3. **Integration Guide** (`USERSPACE_GRAPHICS_INTEGRATION.md`)
- 📚 Complete guide to extend kernel API
- 📚 Code examples for wrappers
- 📚 Step-by-step implementation

---

## Quick Start: Use Text Shell Now

### Step 1: Update Makefile

Replace `userspace/init.c` with `userspace/shell.c`:

```makefile
# In Makefile, change userspace source:

# OLD:
# USERSPACE_SRC := userspace/init.c

# NEW:
USERSPACE_SRC := userspace/shell.c
```

### Step 2: Build & Run

```bash
make clean
make
make run  # or: qemu-system-x86_64 -m 512 -kernel bzImage ...
```

### Step 3: Test the Shell

```
Ark Shell v1.0
Session: tty0 (ID: 0)
Type 'help' for available commands.
Type 'exit' to quit.

ark@localhost:~$ help
```

---

## Shell Commands

### `help`
Show all available commands with descriptions.

```
ark@localhost:~$ help

╔════════════════════════════════════════════╗
║           AVAILABLE COMMANDS               ║
╠════════════════════════════════════════════╣
║  ls         - List files in directory (/)  ║
║  cat <path> - Display file contents        ║
║  echo <text> - Echo text to screen        ║
║  time       - Show current date and time   ║
║  cpu        - Display CPU information      ║
║  clear      - Clear the screen             ║
║  help       - Show this help message       ║
║  exit       - Exit the shell               ║
╚════════════════════════════════════════════╝
```

### `ls` / `ls /`
List files in ramfs root.

```
ark@localhost:~$ ls

Files in / (3 total):
    📄  init.bin
    📄  demo.init
    📄  test.txt
```

### `cat <filepath>`
Display file contents.

```
ark@localhost:~$ cat /init.bin

Reading '/init.bin' (14208 bytes):
─────────────────────────────────────────
[binary data displayed as readable text...]

```

### `echo <text>`
Echo text to screen.

```
ark@localhost:~$ echo Hello from userspace!

  → Hello from userspace!
```

### `time`
Show current date and time.

```
ark@localhost:~$ time

System Time:
    12:34:56  |  15 Jan 2025
```

### `cpu`
Display CPU vendor and information.

```
ark@localhost:~$ cpu

CPU Information:
    Vendor:   GenuineIntel
    Family:   6
    Model:    42
    Stepping: 7
```

### `clear`
Clear the screen (scroll up).

```
ark@localhost:~$ clear

[40 newlines added - clears view]
```

### `exit`
Cleanly exit the shell and return to kernel.

```
ark@localhost:~$ exit

Goodbye!
```

---

## Architecture Overview

```
Kernel                           Userspace
─────────────────────────────────────────────────

[init.c: kernel_main]
      │
      ├─ fs_mount_root()
      ├─ ramfs_mount()
      ├─ elf_execute(init.bin, ark_kernel_api())
      │       │
      │       └─ ELF Loader calls _start() with api
      │
[Shell (userspace)]
      ├─ printk() → via api->printk
      ├─ VFS access → via api->vfs_*
      ├─ Input → via api->input_*
      ├─ TTY session → via api->tty_*
      │
      └─ Command loop
         ├─ Read user input
         ├─ Parse command
         ├─ Execute (ls, cat, etc.)
         └─ Loop
```

---

## Integration Workflow

### Phase 1: Text Shell (Now)

```
1. Use shell.c as init.bin
   └─ Works with API v2
   └─ No graphics needed
   └─ Good for testing VFS, input, etc.
```

### Phase 2: Graphics Foundation (Next)

```
1. Implement gfx_renderer.c in kernel
2. Implement text_renderer.c in kernel
3. Integrate with printk for graphics output
4. Test: printk appears on screen
```

### Phase 3: Graphics Shell (After Phase 2)

```
1. Extend kernel API (v3) with graphics functions
2. Add graphics wrappers to init_api.c
3. Switch to graphics_shell.c
4. Test: shell renders with GUI
```

### Phase 4: Advanced Features (Beyond)

```
1. Windowing system
2. Mouse cursor
3. Multiple userspace programs
4. TTF fonts
```

---

## Userspace Directory Structure

```
userspace/
├── init.c                    [Original reference shell]
├── linker.ld                 [Userspace linker script]
├── shell.c                   [Text-based shell - USE THIS]
├── graphics_shell.c          [Graphics shell - for Phase 3]
└── [future: additional programs]
```

---

## Using Shell.c vs Graphics_Shell.c

### When to use `shell.c`:

- ✅ Testing VFS integration
- ✅ Testing input handling
- ✅ Testing command parsing
- ✅ Quick debugging
- ✅ Low resource requirements

```makefile
USERSPACE_SRC := userspace/shell.c
```

### When to use `graphics_shell.c`:

- ⏳ After implementing graphics renderer
- ⏳ Need visual UI
- ⏳ Want to showcase graphics system
- ⏳ Higher resource needs

```makefile
USERSPACE_SRC := userspace/graphics_shell.c
```

---

## Extending the Shell

### Add a New Command

To add a new shell command (e.g., `whoami`):

```c
/* In shell.c */

/* 1. Create command function */
static void cmd_whoami(char *args) {
    (void)args;  /* Unused */
    g_api->printk("  user: root (kernel context)\n");
    g_api->printk("  session: %s\n", current_tty_name);
    g_api->printk("\n");
}

/* 2. Add to execute_command() */
static int execute_command(char *cmd, char *args) {
    if (str_eq(cmd, "whoami")) {
        cmd_whoami(args);
        return 0;
    }
    
    /* ... rest of commands ... */
}

/* 3. Update help */
static void cmd_help(void) {
    g_api->printk("  whoami      - Display current user\n");
    /* ... other commands ... */
}
```

### Add Command History

```c
/* In shell.c - already has basic history support */

static history_entry_t g_history[HISTORY_SIZE];
static u32 g_history_count = 0;

static void history_add(const char *line) {
    if (g_history_count >= HISTORY_SIZE) {
        /* Shift history */
        for (u32 i = 0; i < HISTORY_SIZE - 1; i++) {
            str_copy(g_history[i].line, g_history[i + 1].line, LINE_MAX);
        }
        g_history_count = HISTORY_SIZE - 1;
    }
    
    str_copy(g_history[g_history_count].line, line, LINE_MAX);
    g_history_count++;
}
```

### Add More File Listing Features

```c
/* Extend cmd_ls to show file sizes */
static void cmd_ls_with_size(char *args) {
    const char *path = (args && args[0]) ? args : "/";
    
    u32 count = g_api->vfs_list_count(path);
    for (u32 i = 0; i < count; i++) {
        char name[NAME_MAX];
        if (g_api->vfs_list_at(path, i, name, sizeof(name))) {
            int fd = g_api->vfs_open(name);
            if (fd >= 0) {
                u32 size = g_api->vfs_file_size(fd);
                g_api->printk("    %s (%u bytes)\n", name, (unsigned)size);
                g_api->vfs_close(fd);
            }
        }
    }
}
```

---

## Debugging Tips

### View Shell Input/Output

Run with serial output enabled:

```bash
make run QEMU_FLAGS="-serial stdio"
```

All shell output goes to terminal.

### Test Specific Commands

Create test files in ramfs and test against them:

```bash
# Before Ark boots, place test files in ramfs
# Then in shell:
ark@localhost:~$ ls /
ark@localhost:~$ cat /testfile.txt
```

### Check Memory Usage

Add memory tracking to kernel and monitor from shell.

### Trace VFS Calls

Enable debug printing in kernel VFS layer to see what shell is accessing.

---

## Common Issues & Solutions

| Issue | Solution |
|-------|----------|
| **Shell won't start** | Check kernel calls `elf_execute(init.bin, ...)` |
| | Verify init.bin is in ramfs |
| | Check API version ≥ 2 |
| **Commands not found** | Shell may be compiled with old code - rebuild |
| **File listing fails** | VFS not mounted - check `fs_mount_root()` in kernel |
| **Input doesn't work** | Input subsystem not initialized |
| **Shell crashes** | Check stack size in linker.ld |
| **Graphics_shell doesn't compile** | Need API v3 - implement graphics wrapper first |

---

## Performance Characteristics

| Operation | Time (Approx) |
|-----------|---------------|
| Shell startup | ~100 ms |
| Command parsing | <1 ms |
| ls (10 files) | ~5 ms |
| cat (1KB file) | ~10 ms |
| User input read | Blocking (waits for key) |

---

## Next Steps

### Immediate (Works Now):
1. Copy code: `shell.c` to `userspace/`
2. Update Makefile to use `shell.c`
3. Build and run
4. Test shell commands

### Next Phase (Add Graphics):
1. Implement graphics renderer
2. Extend kernel API to v3
3. Switch to `graphics_shell.c`
4. Render graphical UI

### Advanced (After Graphics Works):
1. Add window manager
2. Add mouse support
3. Create additional userspace programs
4. Build application launcher

---

## Files You Have

```
userspace/
├── shell.c [NEW]                    ← Text shell (USE THIS)
├── graphics_shell.c [NEW]           ← Graphics shell (for later)
└── existing files...

Root:
├── USERSPACE_GRAPHICS_INTEGRATION.md [NEW]
├── GRAPHICS_ARCHITECTURE.md
├── GRAPHICS_IMPLEMENTATION_GUIDE.md
├── GRAPHICS_IMAGE_PIPELINE.md
└── existing docs...
```

---

## Summary

**Right Now:**
- ✅ Use `shell.c` directly
- ✅ Works with existing kernel
- ✅ Test VFS, input, commands
- ✅ Great for testing!

**When Graphics Ready:**
- 📋 Follow `USERSPACE_GRAPHICS_INTEGRATION.md`
- 📋 Extend kernel API
- 📋 Use `graphics_shell.c`
- 📋 Full GUI shell!

Start with text shell, add graphics later. Both are provided! 🚀

