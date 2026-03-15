================================================================================
                           ARK KERNEL DOCUMENTATION
================================================================================

CONTENTS
--------
1. Overview
2. Building
3. Configuration
4. Architecture
5. Features
6. Directory Structure
7. Boot Process
8. Development

================================================================================
1. OVERVIEW
================================================================================

Ark is a monolithic kernel written in C for x86 and x86_64 architectures.
It supports modern hardware including USB, networking, storage, and graphics.

Key Features:
- Multiboot2 compliant
- x86 (32-bit) and x86_64 (64-bit) support
- Preemptive multitasking scheduler
- Virtual memory management
- USB stack (xhci, ehci, uhci)
- Network drivers (e1000, rtl8139, virtio, etc.)
- SATA/ATA storage drivers
- VESA graphics support
- AC97/HDA audio support

================================================================================
2. BUILDING
================================================================================

Prerequisites:
- GCC (GNU Compiler Collection)
- NASM (Netwide Assembler)
- Python3
- make

Basic Build:
    make              Build kernel for x86 (32-bit)
    make ARCH=x86_64 Build kernel for x86_64 (64-bit)

Output: compiled/x86/compressed/ArkImage or compiled/x86_64/compressed/ArkImage

Cleaning:
    make clean        Remove all build artifacts

Configuration:
    make menuconfig  Interactive kernel configuration
    make defconfig   Apply default configuration
    make tinyconfig  Minimal configuration
    make allyes      Enable all features
    make allno       Disable all optional features

Build Options:
    make DEBUG=1     Enable debug symbols (-O0 -g)
    make OPT=0       Set optimization level (0,1,2,s)
    make WERROR=1    Treat warnings as errors

================================================================================
3. CONFIGURATION
================================================================================

The kernel uses a Kconfig-based configuration system. Key options:

Architecture:
  ARCH              - x86 or x86_64

Build:
  OPT_LEVEL         - Optimization level (0,1,2,s)
  DEBUG             - Enable debug symbols
  WERROR            - Treat warnings as errors
  CODENAME          - Build identifier

Memory:
  PMM_ENABLE        - Physical memory manager
  VMM_ENABLE        - Virtual memory manager
  HEAP_SIZE_KB      - Kernel heap size in KB
  STACK_SIZE_KB     - Kernel stack size per thread

Framebuffer:
  FB_ENABLE         - Framebuffer support
  FB_WIDTH/HEIGHT   - Resolution
  FB_BPP            - Bits per pixel (32)
  FB_DRIVER        - Driver (bga, etc.)

I/O:
  PIO_ENABLE        - Port-mapped I/O
  MMIO_ENABLE       - Memory-mapped I/O
  IOAPIC_ENABLE     - I/O APIC support
  PIC_ENABLE        - Legacy PIC support

Interrupts:
  IDT_ENABLE        - Interrupt descriptor table
  IRQ_STACK        - Per-IRQ stack switching
  NMI_ENABLE       - Non-maskable interrupt support

USB:
  USB_ENABLE       - USB subsystem
  USB_XHCI         - xHCI (USB 3.0) controller
  USB_EHCI         - EHCI (USB 2.0) controller
  USB_UHCI         - UHCI (USB 1.x) controller
  USB_HID          - HID driver support

Storage:
  ATA_ENABLE       - PATA/IDE support
  SATA_ENABLE      - SATA support
  SD_ENABLE        - SD card support
  FAT32_ENABLE     - FAT32 filesystem
  RAMFS_ENABLE     - RAM filesystem
  AFS_ENABLE       - Ark Filesystem
  VFS_ENABLE       - Virtual filesystem layer
  ZIP_ENABLE       - ZIP archive support

Networking:
  NET_ENABLE       - Networking subsystem
  E1000_ENABLE    - Intel e1000 driver
  E100_ENABLE     - Intel e100 driver
  IP_ENABLE       - IP protocol
  UDP_ENABLE      - UDP protocol
  TCP_ENABLE      - TCP protocol

Audio:
  AUDIO_ENABLE     - Audio subsystem
  AC97_ENABLE     - AC97 audio driver
  HDA_ENABLE      - HDA audio driver

HID:
  KBD_ENABLE       - Keyboard support
  MOUSE_ENABLE     - Mouse support
  TOUCH_ENABLE    - Touchscreen support

Graphics:
  GPU_ENABLE       - GPU subsystem
  VESA_ENABLE     - VESA mode support

PCI:
  PCI_ENABLE       - PCI bus support
  PCI_PROBE_ALL   - Probe all PCI devices

Scheduler:
  SCHED_ENABLE     - Preemptive scheduler
  SCHED_PREEMPT   - Preemptive multitasking
  SCHED_TIMESLICE_MS - Time slice in ms
  SCHED_MAX_TASKS - Maximum concurrent tasks
  SCHED_STACK_KB  - Per-task stack size

Syscalls:
  SYSCALL_ENABLE   - System call interface
  ELF_LOADER       - ELF binary loader

Debugging:
  DEBUG_VERBOSE    - Verbose debug output
  DEBUG_KASAN      - Kernel address sanitizer
  DEBUG_PANIC_DUMP - Dump registers on panic

================================================================================
4. ARCHITECTURE
================================================================================

Memory Layout (x86):
  0x00000000 - 0x00400000   Identity mapped (4 MB)
  0x00400000 - ...          Kernel load address
  0xC0000000 - ...          Kernel virtual address space

Memory Layout (x86_64):
  0x0000000000000000 - 0x0000000000400000  Lower identity (2 MB)
  0xFFFFFFFF80000000 - ...                  Kernel direct mapping

Boot:
  - Multiboot2 compliant
  - Loaded at 4 MB physical address
  - GDT, IDT initialized before kernel_main

Kernel Sections:
  .multiboot    - Multiboot header
  .text         - Executable code
  .rodata       - Read-only data
  .data         - Initialized read-write data
  .bss          - Zero-initialized data

================================================================================
5. FEATURES
================================================================================

Memory Management:
- Physical Memory Manager (PMM) - alloc/free physical pages
- Virtual Memory Manager (VMM) - page tables, paging
- Slab allocator for kernel objects
- Kernel heap with malloc/free

Interrupt Handling:
- IDT with interrupt gates
- IRQ routing through PIC and IOAPIC
- Per-CPU IRQ stacks (optional)
- Syscall interface (int 0x80 on x86, syscall on x86_64)

Process Management:
- Task structures for each thread
- Round-robin scheduling
- Preemptive multitasking
- Kernel thread creation

Filesystem:
- VFS abstraction layer
- FAT32 read/write
- RAMFS for initramfs
- AFS (Ark Filesystem)
- ZIP archive mounting

Networking:
- ARP, IP, ICMP, UDP, TCP protocols
- Ethernet frame handling
- Socket-like API

USB:
- xHCI (USB 3.0) host controller
- EHCI (USB 2.0) host controller
- UHCI (USB 1.x) host controller
- HID class driver (keyboard, mouse)
- Mass storage class driver

Graphics:
- VESA modes up to 1920x1080
- Framebuffer console
- Software rendering support

Audio:
- AC97 codec support
- HDA codec support
- Mixer controls

================================================================================
6. DIRECTORY STRUCTURE
================================================================================

arch/           Architecture-specific code
  x86/          32-bit x86 code
  x86_64/       64-bit x86_64 code
  mm/           Memory management

drivers/        Device drivers
  aud/          Audio drivers
  fb/           Framebuffer drivers
  gpu/          Graphics drivers
  hid/          Human interface devices
  hw/           Hardware detection
  usb/          USB stack
  wf/           Network drivers

fs/             Filesystem implementations
  ata.c         ATA/PATA driver
  fat32.c       FAT32 filesystem
  ramfs.c       RAM filesystem
  vfs.c         Virtual filesystem
  zip.c         ZIP archive support

gen/            Generic kernel subsystems
  init.c        Kernel initialization
  pci.c         PCI bus driver (Credit: Yahya Mokhlis)
  printk.c      Kernel logging
  sched.c       Process scheduler
  syscall.c     System call handler
  idt_init.c    Interrupt setup

hw/             Hardware detection
  ramcheck.c    RAM detection
  smbios.c      SMBIOS table parsing
  vendor.c      CPU vendor detection

include/        Header files
  ark/          Kernel headers
  gpu/          Graphics headers
  pci/          PCI headers
  usb/          USB headers

io/             I/O operations
  serial.c      Serial port driver

ks/             Kernel scripting
  script.c      Embedded scripting
  dkmload/      Dynamic kernel modules

mm/             Memory management
  pmm.c         Physical memory manager
  vmm.c         Virtual memory manager

mp/             Multiprocessing
  mp.c          APIC/ACPI MP parsing

scripts/        Build scripts
  apply_preset.py   Apply configuration preset
  gen_kconfig_h.py Generate kconfig header
  kconfig_editor.py Configuration editor

zig/            Zig language support (optional)
  src/          Zig kernel types

rust/           Rust language support (optional)
  src/          Rust USB driver

================================================================================
7. BOOT PROCESS
================================================================================

1. Bootloader loads kernel (GRUB, QEMU, etc.)
   - Kernel placed at 4 MB physical address
   - Multiboot info passed in EBX register

2. boot.S (arch-specific)
   - Setup GDT
   - Setup IDT
   - Enable paging
   - Switch to long mode (x86_64)

3. arch/*/built-in.c
   - Early initialization
   - Memory detection
   - PCI bus scan
   - Call kernel_main()

4. gen/init.c (kernel_main)
   - Display initialization
   - FPU initialization
   - CPU verification
   - Serial port init
   - IDT setup
   - Scheduler init
   - USB init
   - Storage init
   - Network init
   - Audio init

5. Start idle thread
   - Scheduler takes over
   - First user task (if any)

================================================================================
8. DEVELOPMENT
================================================================================

Adding a New Driver:
1. Add source file to appropriate directory in drivers/
2. Add to SRCS_* in Makefile
3. Add config option in scripts/gen_kconfig_h.py
4. Initialize in gen/init.c

Adding a New Syscall:
1. Define syscall number in include/ark/syscalls.h
2. Implement handler in gen/syscall.c
3. Add to syscall table

Debugging:
- Serial output on COM1 (configurable)
- printk() for kernel logging
- DEBUG_KASAN for memory errors
- QEMU with -serial stdio for output

Testing:
- QEMU: qemu-system-x86_64 -kernel compiled/x86_64/compressed/ArkImage
- GDB: qemu-system-x86_64 -s -S -kernel compiled/x86_64/compressed/ArkImage

================================================================================
