
   ___         __
  /   |  _____/ /__
 / /| | / ___/ //_/
/ ___ |/ /  / ,<
/_/  |_/_/  /_/|_|

Ark Kernel — Version 1.0  |  Codename: "affectionate cat"
Architecture: x86 / x86_64  |  License: GPLv3
================================================================


CONTENTS
--------
  1.  What is Ark?
  2.  Features
  3.  Source Tree
  4.  Requirements
  5.  Building the Kernel
  6.  Configuration
  7.  Running (QEMU)
  8.  The Init System & Scripting
  9.  Dynamic Kernel Modules (DKMs)
  10. ark-gcc — The Ark Toolchain
  11. Userspace & Init Programs
  12. Supported Hardware
  13. Kernel Internals
  14. Attribution & Naming Policy  ← READ THIS
  15. License
  16. Contributing


1. WHAT IS ARK?
---------------
Ark is an independent, from-scratch 32/64-bit x86 operating system kernel
written in C.  It is not a Linux fork, not a POSIX clone, and not a hobby
toy — it is a real kernel with its own boot flow, memory manager, VFS,
device drivers, ELF loader, network stack, audio subsystem, and a complete
userspace toolchain called ark-gcc.

Ark boots via Multiboot, initialises its own GDT/IDT, sets up paging,
mounts an in-memory filesystem (ramfs), loads an init script or ELF binary,
and hands control to userspace — all without depending on any third-party
runtime, libc, or bootloader beyond GRUB.

Codename convention follows an alphabetical animal series.
Current release: "affectionate cat".


2. FEATURES
-----------
Kernel core
  - Multiboot-compliant boot (GRUB2 compatible)
  - x86 32-bit and x86_64 64-bit build targets
  - GDT, IDT, ISR, IRQ handling
  - Physical memory manager (PMM) + virtual memory manager (VMM)
  - 4 KB paged heap allocator (HEAP_SIZE configurable via .kconfig)
  - Kernel panic with register dump
  - Software tick counter / boot clock
  - SMBIOS table parsing
  - CPU identification (CPUID vendor + brand string)
  - Serial console output (COM1) for headless/QEMU -nographic use

Filesystem
  - ramfs  — in-memory root filesystem, loaded from initramfs image
  - VFS    — virtual filesystem layer (open/read/close/mkdir/mknod)
  - FAT32  — read support for ATA/SATA block devices
  - ATA    — PIO-mode disk driver
  - SATA   — AHCI SATA controller support
  - ZIP    — compressed initramfs unpacking (tinflate)
  - Module loading from VFS paths

Display
  - VESA BIOS Extensions (BGA / Bochs Graphics Adapter)
  - 32bpp linear framebuffer, 1024×768 default
  - Bitmap font renderer (8×16 PSF-style, 256-glyph table)
  - printk routes to both VGA text buffer and framebuffer
  - Colour printk (printc / printc_rgb)
  - Cursor support (blinking, colour, movement)
  - TTY session multiplexer (tty_alloc / tty_switch)

Input
  - PS/2 keyboard (scancode set 1, IRQ1)
  - PS/2 mouse (3-byte packet protocol, IRQ12)
  - USB HID keyboard and mouse (UHCI / OHCI / EHCI / xHCI)
  - USB gamepad, USB audio, USB CDC (serial), USB printer, USB video
  - USB hub enumeration

Networking
  - Intel e1000 NIC driver
  - Realtek RTL8139 driver
  - Intel e100 driver
  - IPv4 stack (ip.c) with loopback
  - ARP, ICMP ping

Audio
  - AC'97 audio controller (ac97.c)
  - USB audio class driver
  - Generic audio device abstraction (aud-dev)

Dynamic Kernel Modules (DKMs)
  - Runtime ELF module loading into a 1 MB kernel pool
  - Vendor metadata tags (__vendor_mod / __vendor_ver)
  - Kernel prints "=> <name> <version>" on successful load
  - hook::dkm: init-script integration
  - dkm_load / dkm_unload / dkm_list syscalls accessible from userspace

Init system
  - Script interpreter for .init files (Ark init scripting language)
  - Shebang (#!init) support
  - Directives: file:, hook::dkm:, exec:, echo:, sleep:, if:, endif:
  - Loads ELF userspace binaries via the ELF loader

Userspace toolchain (ark-gcc)
  - GCC-wrapper compiler driver targeting the Ark ABI (int 0x80)
  - Full custom libc: stdio, stdlib, string, ctype, math stubs
  - VESA framebuffer library (libark vesa)
  - PS/2 mouse and keyboard libraries
  - File system helpers (ark/fs.h)
  - Process/CPU helpers, I/O port access (ark/proc.h)
  - DKM build target with auto-detection (--target=dkm)
  - Three linker scripts: user, dkm, init


3. SOURCE TREE
--------------
Ark-main/
├── Makefile               Top-level build system (GNU make)
├── linker.ld              Kernel ELF linker script
├── .kconfig               menuconfig-generated kernel configuration
├── config.mk.example      Local build overrides template
│
├── arch/                  Architecture-specific code
│   ├── x86/               32-bit x86 (GDT, IDT, CPU, paging)
│   └── x86_64/            64-bit x86_64 support
│
├── gen/                   Core kernel (architecture-independent)
│   ├── init.c             Kernel boot flow (kernel_main)
│   ├── init_api.c         Kernel API table (ark_kernel_api_t)
│   ├── syscall.c          Syscall dispatcher (int 0x80)
│   ├── elf_loader.c       ELF32 loader
│   ├── printk.c           Kernel printf / serial / framebuffer output
│   ├── pci.c              PCI bus enumeration
│   ├── cpu.c              CPU feature detection
│   ├── idt_init.c         IDT setup and exception handlers
│   ├── display.c          VESA display initialisation
│   ├── input.c            Keyboard/mouse input layer
│   ├── tty.c              TTY session management
│   ├── panic.c            Kernel panic handler
│   └── shutdown.c         Shutdown / reboot
│
├── fs/                    Filesystem drivers
│   ├── ramfs.c            In-memory root filesystem
│   ├── vfs.c              Virtual filesystem layer
│   ├── fat32.c            FAT32 read driver
│   ├── ata.c              ATA PIO disk driver
│   ├── sata.c             AHCI SATA driver
│   ├── zip.c + tinflate.c Compressed initramfs support
│   └── modules.c          Filesystem module registry
│
├── ks/                    Kernel services
│   ├── script.c           Init script interpreter
│   └── dkmload/           Dynamic Kernel Module loader
│       ├── dkm_loader.c   ELF module loader, vendor tag scanner
│       ├── strapper.c      DKM management utility
│       └── sysc.c         DKM syscall handlers
│
├── mem/                   Memory management
├── mm/                    Memory mapping helpers
├── mp/                    Multiprocessor / SMP stubs
│
├── hid/                   Human input devices
│   ├── kbd100.c           PS/2 keyboard driver
│   ├── usb_kbd.c          USB HID keyboard
│   └── touch.c            Touchscreen input
│
├── usb/                   USB host controller drivers
│   ├── uhci.c / ohci.c / ehci.c / xhci.c
│   ├── usb_hid_mouse.c    USB mouse
│   ├── usb_audio.c        USB audio class
│   ├── usb_msd.c          USB mass storage
│   └── usb_hub.c          USB hub
│
├── wf/                    Wifi / wireless stubs
├── gpu/                   GPU abstraction (VESA BGA driver)
├── pci/                   PCI device drivers (e1000, eth-dev)
├── io/                    Port I/O helpers
├── aud/                   Audio subsystem (AC'97, aud-dev)
│
├── include/               Kernel header files
│   └── ark/               Ark-specific headers
│       ├── types.h         Primitive types (u8, u16, u32, u64, bool)
│       ├── printk.h        Kernel printk / printc / serial
│       ├── init_api.h      ark_kernel_api_t — API passed to init/DKMs
│       ├── syscall.h       Syscall numbers and inline wrappers
│       ├── dkm.h           DKM loader API and vendor macros
│       ├── vfs.h           VFS API
│       ├── fb.h            Framebuffer API
│       ├── elf_loader.h    ELF loader API
│       ├── pci.h           PCI bus API
│       ├── ramfs.h         ramfs API
│       └── ...
│
├── ark-gcc/               Ark userspace toolchain
│   ├── ark-gcc            Compiler driver shell script
│   ├── Makefile           Toolchain build system
│   ├── linker.ld          User program linker script
│   ├── linker-dkm.ld      DKM module linker script
│   ├── linker-init.ld     Init binary linker script
│   ├── libark/            Custom C library source
│   │   ├── crt0.S         C runtime startup (_start)
│   │   ├── stdio.c        printf/sprintf/vprintf family
│   │   ├── stdlib.c       malloc/free/qsort/strtol
│   │   ├── string.c       strlen/strcat/strtok/strdup/memcpy
│   │   ├── ctype.c        isdigit/isalpha/toupper/tolower
│   │   ├── vesa.c         Framebuffer drawing library
│   │   ├── mouse.c        PS/2 mouse driver
│   │   ├── kbd.c          PS/2 keyboard driver
│   │   ├── proc.c         Process/CPU/I/O port helpers
│   │   └── fs.c           File I/O helpers
│   ├── include/           Userspace headers (stdio.h, stdlib.h ...)
│   │   └── ark/           Ark-specific userspace headers
│   └── examples/          Example programs and DKMs
│
├── user/                  Userspace programs (installer, shell, etc.)
├── userspace/             Additional userspace utilities
├── scripts/               Build and utility scripts
└── initramfs/             Files packed into the boot initramfs image


4. REQUIREMENTS
---------------
Build host (Linux recommended, WSL2 fully supported):

  gcc          >= 9.0   (or cross-compiler i686-elf-gcc for cleaner builds)
  binutils     >= 2.34  (ld, as, objcopy, nm)
  nasm         >= 2.14
  make         >= 4.0
  python3      >= 3.8   (for scripts and menuconfig)
  qemu         >= 4.0   (qemu-system-i386 or qemu-system-x86_64)
  grub2        (grub-mkrescue, xorriso — for ISO builds)
  ncurses-dev  (for menuconfig: make menuconfig)

Optional:
  clang        Alternative C compiler (set CC=clang)
  gdb          Kernel debugging (qemu -s -S)
  bochs        Alternative emulator (bochsrc.txt provided)

Install on Ubuntu/Debian:
  sudo apt install gcc gcc-multilib binutils nasm make python3 \
       qemu-system-x86 grub-pc-bin xorriso libncurses-dev


5. BUILDING THE KERNEL
----------------------
Clone and build:

  git clone https://github.com/your-org/Ark-main.git
  cd Ark-main
  make

This produces:
  bzImage          — compressed kernel image
  initramfs        — packed root filesystem
  ArkImage         — bootable disk image (for QEMU)

Useful make targets:

  make              Build kernel + initramfs
  make clean        Remove compiled objects
  make run          Build and launch in QEMU
  make run-nographic  Run with serial output (no window)
  make iso          Build a bootable ISO image
  make menuconfig   Interactive kernel configuration (ncurses)
  make info         Print build configuration

Build variables (pass on command line or set in config.mk):

  ARCH=x86          Target architecture: x86 (default) or x86_64
  BITS=32           Word size: 32 (default) or 64
  DEBUG=1           Debug build (-O0 -g)
  OPT=2             Optimisation level 0–3 or s (default: 2)
  WERROR=1          Treat warnings as errors
  CC=clang          Use a different compiler
  QEMU_RAM_MB=512   QEMU memory in MB (default: 256)

Example — 64-bit debug build:

  make ARCH=x86_64 BITS=64 DEBUG=1


6. CONFIGURATION
----------------
Ark uses a .kconfig file (similar to Linux's .config).  Generate it
with menuconfig or edit it by hand:

  make menuconfig

Key configuration options:

  ARCH               x86 | x86_64
  BITS               32 | 64
  DEBUG              0 | 1
  OPT_LEVEL          0 | 1 | 2 | 3 | s
  PRINTK_ENABLE      1 — enable kernel log output
  SERIAL_ENABLE      1 — enable COM1 serial output
  SERIAL_PORT        1016 — COM1 I/O port (0x3F8)
  LOGLEVEL           0–7 (4 = info)
  FB_ENABLE          1 — enable VESA framebuffer
  FB_WIDTH           1024
  FB_HEIGHT          768
  FB_BPP             32
  FB_DRIVER          bga — Bochs/VirtualBox graphics adapter
  PMM_ENABLE         1 — physical memory manager
  VMM_ENABLE         1 — virtual memory manager
  HEAP_SIZE_KB       4096 — kernel heap size
  STACK_SIZE_KB      64
  INIT_BIN           /init.bin — path to init binary in ramfs
  BUILD_INIT         0 | 1 — build init.bin from source

A config.mk.example is provided.  Copy it to config.mk for local overrides
(config.mk is gitignored):

  cp config.mk.example config.mk
  # edit config.mk as needed
  make


7. RUNNING (QEMU)
-----------------
  make run

QEMU launches with:
  - 256 MB RAM  (QEMU_RAM_MB=256)
  - VGA display (1024×768 VESA)
  - PS/2 keyboard and mouse
  - USB controller (EHCI)
  - e1000 network card

Headless / serial mode (no GUI window):

  make run-nographic

This redirects all printk output to stdout via COM1.  Useful for CI
and automated testing.

Manual QEMU invocation:

  qemu-system-i386 \
      -kernel bzImage \
      -initrd initramfs \
      -m 256 \
      -vga std \
      -serial stdio \
      -device usb-ehci \
      -device usb-mouse \
      -device usb-kbd

Debug with GDB:

  # Terminal 1
  qemu-system-i386 -kernel bzImage -initrd initramfs -s -S -m 256

  # Terminal 2
  gdb vmark
  (gdb) target remote :1234
  (gdb) break kernel_main
  (gdb) continue


8. THE INIT SYSTEM & SCRIPTING
------------------------------
When the kernel boots it looks for /init (or the path in INIT_BIN) in
the ramfs.  If the file starts with #!init it is treated as an Ark init
script; otherwise it is executed as an ELF binary.

Init script syntax (.init files):

  #!init
  # comment

  echo: <message>              — print to kernel console
  file:<path>                  — execute ELF binary from ramfs
  exec:<path> [args...]        — execute ELF with arguments
  hook::dkm:<config_path>      — load DKM modules from config file
  sleep:<ms>                   — busy-wait ms milliseconds
  if:<condition>               — conditional block (endif:)
  endif:

Example init.init:

  #!init
  echo: Ark kernel booting...
  hook::dkm:file:/usr/mods/load.txt
  file:/usr/bin/shell

DKM load config (load.txt):

  [Trigger]
  mydriver

  [Action]
  /usr/mods/mydriver.elf
  /usr/mods/netdrv.elf


9. DYNAMIC KERNEL MODULES (DKMs)
---------------------------------
DKMs are 32-bit ELF binaries loaded into the kernel at runtime.
They are NOT kernel modules in the Linux sense — they run in the same
address space as the kernel and receive a pointer to the kernel API table.

Writing a DKM:

  #include <ark/dkm.h>

  __vendor_mod("mydrv");     /* shown by kernel: => mydrv 2.0  */
  __vendor_ver("2.0");

  int module_init(const ark_kernel_api_t *api) {
      api->printk("mydrv: hello from DKM\n");
      return 0;    /* non-zero -> "=> ...failed to init the dkm" */
  }

Building a DKM (ark-gcc auto-detects from __vendor_mod / module_init):

  ark-gcc mydrv.c -o mydrv.elf
  # or explicitly:
  ark-gcc --target=dkm mydrv.c -o mydrv.elf

Loading from userspace:

  #include <ark/dkm.h>
  dkm_load("/usr/mods/mydrv.elf");
  dkm_list();
  dkm_unload("mydrv");

The kernel API table (ark_kernel_api_t) exposes:
  api->printk()           kernel printf
  api->vfs_open/read/close() file access
  api->input_getc()       blocking keyboard read
  api->cpuid()            CPUID wrapper
  api->tty_alloc/switch() TTY session management
  api->read_rtc()         real-time clock


10. ARK-GCC — THE ARK TOOLCHAIN
---------------------------------
ark-gcc is a GCC-wrapper that cross-compiles C programs for the Ark
kernel's ABI (int 0x80, i686, -ffreestanding -nostdlib).

Install:

  cd ark-gcc
  make
  sudo make install         # installs to /bin/ark-gcc-32/
  export PATH=$PATH:/bin/ark-gcc-32/bin

Quick usage:

  ark-gcc hello.c -o hello                # userspace binary
  ark-gcc --target=dkm driver.c -o drv.elf  # DKM module
  ark-gcc --target=init init.c -o init.bin   # init binary
  ark-gcc --info                             # show toolchain details

libark — the Ark standard library:

  Header          Contents
  ─────────────────────────────────────────────────────────────────
  stdio.h         printf/vprintf/fprintf/sprintf/snprintf (full)
  stdlib.h        malloc/free/calloc/realloc (free-list allocator)
                  atoi/atol/strtol/strtoul/qsort/bsearch/rand
  string.h        strlen/strcpy/strcat/strtok/strdup/memcpy/memcmp
  ctype.h         isdigit/isalpha/isspace/toupper/tolower
  stdarg.h        va_list (GCC builtins)
  stdint.h        uint8_t … uint64_t, uintptr_t, SIZE_MAX
  stdbool.h       bool / true / false
  ark/vesa.h      framebuffer drawing, 8×16 font, alpha blend
  ark/mouse.h     PS/2 mouse polling, absolute coordinates
  ark/kbd.h       PS/2 keyboard polling, scancode → ASCII
  ark/proc.h      getpid, exit, sleep_ms, rdtsc, cpuid, inb/outb
  ark/fs.h        open/read/write/close, readfile_alloc, writefile
  ark/dkm.h       DKM build macros + dkm_load/unload/list
  ark/syscall.h   Raw int 0x80 wrappers


11. USERSPACE & INIT PROGRAMS
------------------------------
Userspace programs must be compiled with ark-gcc.  They run in the same
ring as the kernel today (flat model) — proper ring-3 separation is
planned for a future release.

A minimal "Hello, Ark" program:

  #include <stdio.h>

  int main(int argc, char **argv) {
      (void)argc; (void)argv;
      printf("Hello from Ark userspace!\n");
      return 0;
  }

Compile and add to initramfs:

  ark-gcc hello.c -o hello
  cp hello initramfs/usr/bin/hello

Then call it from init.init:

  file:/usr/bin/hello


12. SUPPORTED HARDWARE
-----------------------
The following hardware is supported by in-tree drivers:

  Processor     x86 (i386+), x86_64; CPUID-detected features
  Memory        Any BIOS-reported e820 memory map
  Display       VESA BGA (VirtualBox, QEMU -vga std, Bochs)
  Disk          ATA PIO (IDE), SATA AHCI
  Keyboard      PS/2 (i8042), USB HID
  Mouse         PS/2 (3-byte protocol), USB HID
  USB           UHCI, OHCI, EHCI, xHCI host controllers
                HID, MSD (mass storage), CDC (serial), audio,
                gamepad, printer, video class drivers
  Network       Intel e1000, Realtek RTL8139, Intel e100
  Audio         Intel AC'97, USB audio class
  PCI           Configuration space enumeration (Type 1 I/O)
  Firmware      SMBIOS 2.x table parsing

Tested emulators:

  QEMU          qemu-system-i386 / qemu-system-x86_64  ✓
  Bochs         2.7+  ✓
  VirtualBox    6.1+  ✓ (BGA graphics)


13. KERNEL INTERNALS
---------------------
Boot flow (abbreviated):

  GRUB multiboot → arch/x86/boot.S (GDT, paging setup)
    → kernel_main() in gen/init.c
      → serial_init()           COM1 at 115200 baud
      → idt_init()              exception and IRQ handlers
      → pmm_init()              physical memory bitmap
      → display_init()          VESA framebuffer
      → pci_scan()              enumerate PCI bus
      → usb_init()              host controller setup
      → ramfs_init/mount()      mount initramfs
      → dkm_init_kernel()       initialise DKM pool
      → disk_load_init()        ATA/SATA probe
      → ip_init()               network stack
      → load_and_run_init()     parse /init, exec ELF or script

Syscall ABI (int 0x80):

  eax   syscall number
  ebx   arg1
  ecx   arg2
  edx   arg3
  esi   arg4 (extended calls)
  eax ← return value

Syscall table (selected):

  0   SYS_READ
  1   SYS_WRITE
  2   SYS_EXIT
  3   SYS_PRINTK
  4   SYS_GET_FRAMEBUFFER
  300 SYS_FB_INFO
  301 SYS_MOUSE_READ
  310 SYS_DKM_LOAD
  311 SYS_DKM_UNLOAD
  312 SYS_DKM_LIST

Memory layout (32-bit, approximate):

  0x00001000 – 0x000FFFFF   Low memory / BIOS area
  0x00100000 – kernel end   Kernel image (loaded by GRUB)
  kernel end – heap end     Kernel heap (HEAP_SIZE_KB in .kconfig)
  0xE0000000                Framebuffer (userspace mapping)
  0x00400000                DKM module pool (1 MB)
  0x00400000                Init binary / userspace ELF load VA


14. ATTRIBUTION & NAMING POLICY
---------------------------------
                    *** REQUIRED — PLEASE READ ***

If you use, fork, embed, extend, or build upon the Ark kernel — in any
project, product, research paper, blog post, operating system, or
distribution — you MUST credit the Ark kernel using the following format:

    Ark/<YourProjectName>

Examples:

    Ark/NovOS        — a new OS built on top of Ark
    Ark/EduKernel    — an educational fork of Ark
    Ark/CloudNode    — a server appliance using Ark
    Ark/GameOS       — a gaming-focused distribution

This attribution must appear in at least one of:

  • Your project's README or documentation (top-level preferred)
  • Your boot screen or splash text
  • Your about/version output (e.g. "Built on Ark/YourName")
  • Your license header block

This is not a legal restriction — it is a community norm.  Ark is
published under GPLv3 (see Section 15), which grants broad freedoms.
The Ark/<name> convention simply ensures the lineage of work is
visible and that contributors receive appropriate recognition.

Correct examples:

    # README.md
    This project is NovOS (Ark/NovOS), based on the Ark kernel.

    // boot.c
    printk("NovOS 1.0 — Ark/NovOS\n");

    $ uname -r
    1.0.0-Ark/NovOS

If you are unsure how to attribute, using "Powered by Ark/YourName"
anywhere prominent is sufficient and appreciated.


15. LICENSE
-----------
The Ark kernel is released under the GNU General Public License,
version 3 (GPLv3).  See the LICENSE file in the root of this
repository for the full license text.

Key terms:
  - You are free to use, study, modify, and distribute this software.
  - Any distribution of modified versions must also be under GPLv3.
  - Source code must be made available to recipients of binaries.
  - No warranty is provided.

The ark-gcc toolchain and libark are also GPLv3.
Programs compiled with ark-gcc and linked against libark are subject
to GPLv3 only if they incorporate libark source or object code.
See the "system library" exception in GPLv3 §1 for details.


16. CONTRIBUTING
----------------
Contributions are welcome.  Please follow these guidelines:

Code style:
  - C99 / gnu99 (kernel uses -std=gnu99)
  - 4-space indentation, no tabs
  - Snake_case for functions and variables
  - ALL_CAPS for macros and constants
  - Every .c file must have a matching header if it exports symbols
  - New drivers go in the appropriate subdirectory (fs/, hid/, wf/, etc.)
  - Kernel code must not use any host libc — only ark/types.h primitives

Pull request checklist:
  [ ] make (kernel builds without errors or new warnings)
  [ ] make run (boots to init prompt in QEMU)
  [ ] New hardware driver: add to the supported hardware list in README
  [ ] New syscall: update syscall table in gen/syscall.c and include/ark/syscall.h
  [ ] New DKM API: update include/ark/init_api.h and bump ARK_INIT_API_VERSION
  [ ] New ark-gcc library: add to libark/ and update ark-gcc/Makefile

Reporting bugs:
  Please include:
    - Exact make command and configuration
    - Full terminal output from make and/or QEMU serial
    - Host OS, gcc version, QEMU version
    - Whether the bug reproduces under -nographic (serial-only) mode


================================================================
Ark Kernel  |  GPLv3  |  "affectionate cat"
If you build on Ark, call it  Ark/<YourName>  — thank you.
================================================================
