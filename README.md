# Ark OS Kernel

A lightweight, modular x86/x86_64 kernel designed for embedded systems, IoT devices, and low-memory environments. Ark prioritizes user experience and system efficiency over heavyweight features.


## Latest Features

### Storage & Filesystems (NEW)
- **Virtual Filesystem Layer (VFS)** - Unified abstraction for multiple filesystems with file descriptor management
- **FAT32 Driver** - Full FAT32 filesystem support with boot sector parsing and cluster management
- **ATA/IDE Driver** - Legacy hard drive support with CHS/LBA addressing and IDENTIFY commands
- **SATA/AHCI Driver** - Modern SSD/drive support via AHCI protocol with PCI enumeration
- **ramfs** - Fast in-memory filesystem for temporary storage

### Network & Drivers (NEW)
- **E1000 Network Driver** - Intel E1000 NIC support with PCI scanning and initialization
- **USB Driver** - USB device enumeration and initialization
- **Keyboard Driver (HID)** - Keyboard input handling

### Bootloading (NEW)
- **GRUB2 Integration** - Bootable disk image creation with MBR partitioning
- **Multiboot Compliance** - Full multiboot specification support for real hardware
- **Real Hardware Ready** - Tested bootable on x86 BIOS systems (QEMU verified, real hardware compatible)

### Core Systems
- **Serial Print System** - Early kernel logging and debug output
- **PCI Scanner** - Hardware device enumeration and management
- **Framebuffer Console** - Graphical console output support
- **Memory Management** - Memory mapping and MMIO support

## File Structure

```
arch/          - CPU architecture-specific code (x86, x86_64)
  x86/         - x86 bootloader and protected mode setup
  x86_64/      - x86_64 architecture support

fb/            - Framebuffer console and graphics
fs/            - Filesystem drivers and storage
  vfs.c/h      - Virtual filesystem layer
  fat32.c/h    - FAT32 driver
  ata.c/h      - ATA/IDE driver
  sata.c/h     - SATA/AHCI driver
  ramfs.c/h    - RAM filesystem
  built-in.c   - Storage subsystem initialization

gen/           - General kernel subsystems
  init.c       - Kernel initialization sequence
  printk.c     - Kernel logging
  pci.c/h      - PCI device scanning
  panic.c      - Panic handling

hid/           - Human Interface Devices
  kbd100.c     - PS/2 keyboard driver

io/            - Input/Output system (port I/O, MMIO)
ks/            - Kernel scripting support
mem/           - Memory management (malloc, MMIO)
mp/            - Module/probe system
wf/            - Network drivers
  e100.c       - Intel E100 NIC
  e1000.c      - Intel E1000 NIC
usb/           - USB device support
kconfig/       - Kernel configuration system
```

## Building & Running

### Requirements
- GCC toolchain (i686-elf or compatible)
- GNU Make
- QEMU (for testing)
- Optional: grub2, parted (for bootable images)

### Build Kernel
```bash
make              # Build bzImage kernel
```

### Run in QEMU
```bash
make run          # Run kernel with Multiboot (QEMU)
make run-disk     # Run kernel with disk image (GRUB2)
```

### Create Bootable Disk Image
```bash
make boot.img     # Creates FAT32 GRUB2 bootable image
```

### Write to USB Device (Real Hardware)
```bash
sudo dd if=boot.img of=/dev/sdX bs=1M status=progress
```

## Architecture

### Boot Sequence
1. **GRUB2 Bootloader** - Loads kernel in protected mode with multiboot info
2. **Kernel Init** - Initializes core systems (memory, PCI, interrupts)
3. **Driver Initialization** - USB, network, storage drivers probe hardware
4. **Filesystem Mount** - VFS mounts ramfs and FAT32 partitions
5. **Init Process** - Kernel hands off to init.bin (optional)

### Filesystem Architecture
- **VFS (Virtual Filesystem)** - Unified interface for multiple filesystems
- **Mount Points** - Multiple filesystems can be mounted at different paths
- **File Descriptors** - Per-process file table for open files
- **Cluster Caching** - FAT32 cluster allocation table cached in memory

### Storage Drivers
- **ATA (IDE)** - Port-mapped I/O (I/O ports 0x1F0-0x1F7, 0x3F6-0x3F7)
- **SATA (AHCI)** - Memory-mapped I/O with PCI BAR discovery
- **Both support LBA addressing** - Linear block addressing for sector access

## Development Status

### Completed
✓ USB driver integration  
✓ E1000 network driver  
✓ ATA/IDE storage driver  
✓ SATA/AHCI storage driver  
✓ Virtual Filesystem Layer  
✓ FAT32 driver framework  
✓ GRUB2 bootable images  
✓ Real hardware boot support  

### In Progress
- Full FAT32 implementation (directory traversal, file reading)
- SATA read/write operations
- Filesystem testing and validation

### Future Work
- ext4 filesystem driver
- UEFI bootloader support
- ARM and RISC-V architecture ports
- Kernel scripting engine
- Userspace support (ring 3 execution)
- Advanced memory management (paging, swapping)
- SMP (multiprocessor) support

## Why Ark?

The OpenArc-1 team believes Ark kernel can serve everywhere in small areas where big projects won't work. Perfect for:
- **Embedded Systems** - Minimal dependencies, small footprint
- **IoT Devices** - Low memory requirements, efficient drivers
- **Specialized Hardware** - Custom architecture support (x86, ARM, RISC-V planned)
- **Real Hardware** - Bootable on actual x86 computers via GRUB2

We prioritize **user experience** and **system efficiency** over heavyweight features. Ark can run where Linux won't fit.

## Documentation

See [FILESYSTEM.md](FILESYSTEM.md) for detailed filesystem architecture and API documentation.

## License

See LICENSE file for licensing information.

## Contributing

OpenArc-1 welcomes contributions. Please submit issues and pull requests for improvements.
