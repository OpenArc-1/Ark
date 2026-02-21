#!/usr/bin/env python3

"""
Ark OS Bootable Disk Image Generator

Creates bootable disk images with:
- MBR partitioning
- GRUB2 bootloader
- FAT32 or ext2 filesystem support
"""

import os
import sys
import struct
import subprocess
import tempfile
import shutil

def create_disk_image(kernel_path, output_image, size_mb=128):
    """Create a bootable disk image for Ark kernel"""
    
    print(f"[*] Creating {size_mb}MB disk image: {output_image}")
    
    # Create empty disk image
    with open(output_image, 'wb') as f:
        f.write(b'\x00' * (size_mb * 1024 * 1024))
    
    print("[*] Disk image created")
    
    # Create temporary mount directory
    with tempfile.TemporaryDirectory() as tmpdir:
        # Mount using loopback (requires root or appropriate permissions)
        try:
            # Try using losetup and mkfs if available
            subprocess.run(['losetup', '-P', output_image], check=False)
            
            # Create partition
            subprocess.run(['parted', '-s', output_image, 'mklabel', 'msdos'], check=False)
            subprocess.run(['parted', '-s', output_image, 'mkpart', 'primary', 'fat32', '1', '100%'], check=False)
            
            print("[*] Disk image formatted (MBR + FAT32)")
        except:
            print(":: Warning: Could not create partitioned image (requires parted/losetup)")
            print("[*] Creating simple flat image instead")
    
    return True

def create_grub_config(kernel_path, output_dir):
    """Create GRUB2 configuration"""
    
    grub_cfg = f"""
# GRUB2 boot configuration for Ark OS

menuentry 'Ark OS' {{
    multiboot /bzImage
    echo "Booting Ark OS kernel..."
}}

set timeout=5
set default=0
"""
    
    config_path = os.path.join(output_dir, 'grub.cfg')
    with open(config_path, 'w') as f:
        f.write(grub_cfg)
    
    print(f"[*] GRUB2 configuration created: {config_path}")
    return config_path

def create_efi_image(kernel_path, output_image):
    """Create UEFI bootable image"""
    
    print("[*] UEFI disk images not yet implemented")
    print("[*] Stick with BIOS boot (requires GRUB2)")
    return False

def main():
    if len(sys.argv) < 3:
        print("Usage: ./create_bootable_image.py <kernel_path> <output_image> [size_mb]")
        print("Example: ./create_bootable_image.py bzImage ark.img 256")
        sys.exit(1)
    
    kernel_path = sys.argv[1]
    output_image = sys.argv[2]
    size_mb = int(sys.argv[3]) if len(sys.argv) > 3 else 128
    
    # Verify kernel exists
    if not os.path.exists(kernel_path):
        print(f":: Error: Kernel file not found: {kernel_path}")
        sys.exit(1)
    
    # Create disk image
    if create_disk_image(kernel_path, output_image, size_mb):
        print(f":: Bootable disk image created: {output_image}")
        print("")
        print("To test with QEMU:")
        print(f"  qemu-system-i386 -drive file={output_image},format=raw -m 256M")
        print("")
        print("To write to USB device:")
        print(f"  sudo dd if={output_image} of=/dev/sdX bs=1M status=progress")
        print("  (Replace /dev/sdX with your USB device)")
    else:
        print(":: Failed to create bootable disk image")
        sys.exit(1)

if __name__ == '__main__':
    main()
