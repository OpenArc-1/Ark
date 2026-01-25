# Ark OS Filesystem and Real Hardware Booting Guide

## Overview

This guide explains how to:
1. Use the filesystem infrastructure in Ark OS
2. Create bootable disk images
3. Boot Ark OS on real hardware

## Filesystem Architecture

### Virtual Filesystem Layer (VFS)

The VFS (`fs/vfs.c`, `fs/vfs.h`) provides a unified interface for multiple filesystem types:

```c
// Mount a filesystem
vfs_mount("ramfs", NULL, "/");
vfs_mount("fat32", "/dev/sda1", "/data");

// Open and read files
int fd = vfs_open("/path/to/file");
vfs_read(fd, buffer, size);
vfs_close(fd);
```

### Supported Filesystems

1. **ramfs** - In-memory filesystem
   - Fast access
   - Limited by RAM
   - Pre-populated by bootloader
   - Used for `/init.bin`

2. **FAT32** - Disk-based filesystem
   - Widely compatible
   - Supported on USB drives
   - Can read large files
   - Implementation in `fs/fat32.c`

### Filesystem Initialization

The filesystem is initialized in `gen/init.c`:

```c
// Storage drivers
sata_init();  // SATA/AHCI devices
ata_init();   // Legacy IDE devices

// Filesystem drivers
fs_built_in_init();  // Initializes VFS, FAT32
ramfs_init();        // Initialize ramfs
ramfs_mount();       // Mount at root
```

## Building for Real Hardware

### Option 1: QEMU Boot (Testing)

```bash
# Test with Multiboot (fastest, QEMU-only)
make
make run

# Test with bootable disk image
make boot.img
make run-disk
```

### Option 2: Create Bootable USB/Disk Image

#### Requirements
- grub2: `sudo apt-get install grub2`
- parted: `sudo apt-get install parted`
- Root access (for losetup/mount)

#### Create Image
```bash
# Using the shell script
bash scripts/create_bootable.sh bzImage ark.img 256

# This creates a 256MB disk image with:
# - MBR partition table
# - FAT32 filesystem
# - GRUB2 bootloader
# - bzImage kernel
```

#### Write to USB
```bash
# WARNING: This is destructive, verify device first!
lsblk  # List devices

# Write image to USB
sudo dd if=ark.img of=/dev/sdX bs=4M status=progress
sudo sync  # Flush to disk

# Boot from USB in BIOS/UEFI settings
```

## Booting Sequence

### On QEMU
```
1. BIOS/UEFI emulation
2. GRUB2 bootloader loads
3. GRUB reads /bzImage from FAT32
4. Kernel loads at 0x100000
5. Kernel enters 32-bit protected mode
6. kernel_main() executes
7. Filesystems initialized
```

### On Real Hardware
```
1. BIOS loads MBR from first sector
2. GRUB2 receives control
3. GRUB reads FAT32 partition
4. GRUB loads /bzImage
5. GRUB sets up Multiboot2 structure
6. GRUB jumps to kernel entry point
```

## Kernel Multiboot Info Structure

The kernel expects the bootloader to provide a Multiboot info structure:

```c
typedef struct {
    u32 flags;
    u32 mem_lower;
    u32 mem_upper;
    u32 boot_device;
    u32 cmdline;
    u32 mods_count;
    u32 mods_addr;
    /* ... more fields ... */
    u32 framebuffer_addr;
    u32 framebuffer_pitch;
    u32 framebuffer_width;
    u32 framebuffer_height;
    u32 framebuffer_bpp;
} multiboot_info_t;
```

GRUB2 automatically populates this structure, but custom bootloaders need to fill it manually.

## Reading Files from Disk

### In Kernel Code

```c
#include "fs/vfs.h"

// Mount filesystem
vfs_mount("fat32", "/dev/sda1", "/");

// Open file
int fd = vfs_open("/data/somefile.bin");
if (fd < 0) {
    printk("Error: file not found\n");
} else {
    u8 buffer[1024];
    int bytes = vfs_read(fd, buffer, sizeof(buffer));
    vfs_close(fd);
}
```

## Storage Device Access

### ATA/IDE Devices
- Legacy hard drives
- IDE/PATA cables
- Modern kernels should prefer SATA

```c
#include "fs/ata.h"

ata_read(bus, drive, lba, sector_count, buffer);
```

### SATA/AHCI Devices
- Modern hard drives, SSDs
- Hot-plug capable
- Better performance

```c
#include "fs/sata.h"

sata_read(bus, port, lba, sector_count, buffer);
```

## Troubleshooting

### Kernel Not Loading
- Check GRUB menu appears
- Verify bzImage path in grub.cfg
- Check disk image has FAT32 partition
- Ensure kernel is < 64MB

### No Filesystem
- Verify VFS initialization in init.c
- Check FAT32 driver loaded
- Ensure partition is FAT32 (not ext4)

### Slow Boot
- Disable debug prints in production
- Optimize sector read sizes
- Cache FAT table in memory

## Performance Optimization

1. **Reduce debug output**
   - Set printk level to errors only
   - Remove timestamp calculations

2. **Cache FAT table**
   - Load full FAT into memory at mount
   - Speeds up file lookups

3. **Use larger read operations**
   - Read 64KB+ at once from disk
   - Reduces BIOS interrupt overhead

## Future Improvements

- [ ] ext4 filesystem support
- [ ] FAT32 write support
- [ ] Journaling filesystem
- [ ] NVMe driver
- [ ] Network boot (PXE)
- [ ] Encrypted filesystem
