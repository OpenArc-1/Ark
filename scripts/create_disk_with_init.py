#!/usr/bin/env python3

"""
Ark OS Disk Image Creator with init.bin

Creates a bootable FAT32 disk image with:
- /bin directory containing init.bin
- /usr directory for user data
- MBR bootloader
"""

import os
import sys
import subprocess
import tempfile
import shutil

def run_cmd(cmd, check=True):
    """Run shell command and return output"""
    try:
        result = subprocess.run(cmd, shell=True, capture_output=True, text=True, check=check)
        return result.returncode, result.stdout, result.stderr
    except Exception as e:
        print(f"[!] Error running command: {cmd}")
        print(f"[!] Exception: {e}")
        return 1, "", str(e)

def create_fat32_disk(output_image, size_mb, kernel_path, init_bin_path):
    """Create FAT32 disk image with bin/ and usr/ directories"""
    
    print(f"[*] Creating {size_mb}MB FAT32 disk image: {output_image}")
    
    # Remove old image
    if os.path.exists(output_image):
        os.remove(output_image)
    
    # Create empty disk image (512MB = 524288 sectors)
    sector_count = (size_mb * 1024 * 1024) // 512
    with open(output_image, 'wb') as f:
        f.write(b'\x00' * (size_mb * 1024 * 1024))
    print(f"[+] Created {size_mb}MB disk image")
    
    # Create MBR partition table
    print("[*] Creating MBR partition table...")
    ret, _, err = run_cmd(f"parted -s {output_image} mklabel msdos", check=False)
    if ret != 0:
        print(f"[!] parted mklabel failed: {err}")
    
    # Create FAT32 partition (use most of disk)
    print("[*] Creating FAT32 partition...")
    ret, _, err = run_cmd(f"parted -s {output_image} mkpart primary fat32 1MB 100%", check=False)
    if ret != 0:
        print(f"[!] parted mkpart failed: {err}")
    
    # Format as FAT32
    print("[*] Formatting partition as FAT32...")
    ret, _, err = run_cmd(f"parted -s {output_image} set 1 boot on", check=False)
    
    # Find loop device
    print("[*] Setting up loop device...")
    ret, out, err = run_cmd(f"losetup -f --show {output_image}", check=False)
    if ret != 0:
        print(f"[!] Could not find loop device: {err}")
        print("[!] Trying with partition offset...")
        loopdev = None
        # Try to use offset for partition
        ret, out, err = run_cmd(f"losetup -f", check=False)
        if ret == 0:
            loopdev = out.strip()
    else:
        loopdev = out.strip()
    
    if not loopdev:
        print("[!] Failed to get loop device")
        return False
    
    print(f"[+] Using loop device: {loopdev}")
    
    # Try to format the partition
    partition_dev = f"{loopdev}p1"
    if not os.path.exists(partition_dev):
        # Try without p1
        partition_dev = loopdev
    
    print(f"[*] Formatting {partition_dev} as FAT32...")
    ret, _, err = run_cmd(f"mkfs.fat -F 32 {partition_dev}", check=False)
    if ret != 0:
        print(f"[!] mkfs.fat failed: {err}")
        # Try alternative
        ret, _, err = run_cmd(f"mkfs.vfat -F 32 {partition_dev}", check=False)
    
    # Mount the partition
    with tempfile.TemporaryDirectory() as tmpdir:
        mount_point = tmpdir
        print(f"[*] Mounting partition to {mount_point}...")
        ret, _, err = run_cmd(f"mount {partition_dev} {mount_point}", check=False)
        
        if ret == 0:
            try:
                # Create directories
                print("[*] Creating /bin and /usr directories...")
                os.makedirs(os.path.join(mount_point, "bin"), exist_ok=True)
                os.makedirs(os.path.join(mount_point, "usr"), exist_ok=True)
                
                # Copy init.bin to /bin
                if os.path.exists(init_bin_path):
                    print(f"[*] Copying {init_bin_path} to /bin/init.bin...")
                    shutil.copy2(init_bin_path, os.path.join(mount_point, "bin", "init.bin"))
                    print("[+] init.bin copied")
                else:
                    print(f"[!] Warning: init.bin not found at {init_bin_path}")
                
                # Copy kernel to root
                if os.path.exists(kernel_path):
                    print(f"[*] Copying {kernel_path} to /bzImage...")
                    shutil.copy2(kernel_path, os.path.join(mount_point, "bzImage"))
                    print("[+] bzImage copied")
                
                # Create boot directory for GRUB
                os.makedirs(os.path.join(mount_point, "boot", "grub"), exist_ok=True)
                
                # Create grub.cfg
                grub_cfg = """# GRUB2 boot configuration for Ark OS

menuentry 'Ark OS' {
    multiboot /bzImage
    echo "Booting Ark OS kernel..."
}

set timeout=5
set default=0
"""
                grub_path = os.path.join(mount_point, "boot", "grub", "grub.cfg")
                with open(grub_path, 'w') as f:
                    f.write(grub_cfg)
                print("[+] GRUB configuration created")
                
                # List contents
                print("\n[*] Disk image contents:")
                for root, dirs, files in os.walk(mount_point):
                    level = root.replace(mount_point, '').count(os.sep)
                    indent = ' ' * 2 * level
                    rel_path = os.path.relpath(root, mount_point)
                    if rel_path == '.':
                        print("  /")
                    else:
                        print(f"  {indent}{os.path.basename(root)}/")
                    subindent = ' ' * 2 * (level + 1)
                    for file in files:
                        size = os.path.getsize(os.path.join(root, file))
                        print(f"  {subindent}{file} ({size} bytes)")
                
            finally:
                # Unmount
                print("\n[*] Unmounting partition...")
                run_cmd(f"umount {partition_dev}", check=False)
        else:
            print(f"[!] Failed to mount partition: {err}")
            print("[!] Proceeding with unmounted image")
    
    # Clean up loop device
    run_cmd(f"losetup -d {loopdev}", check=False)
    
    return True

def main():
    if len(sys.argv) < 4:
        print("Usage: ./create_disk_with_init.py <kernel_path> <init.bin_path> <output_image> [size_mb]")
        print("Example: ./create_disk_with_init.py bzImage init.bin disk.img 256")
        sys.exit(1)
    
    kernel_path = sys.argv[1]
    init_bin_path = sys.argv[2]
    output_image = sys.argv[3]
    size_mb = int(sys.argv[4]) if len(sys.argv) > 4 else 256
    
    # Verify files exist
    if not os.path.exists(kernel_path):
        print(f"[!] Error: Kernel not found: {kernel_path}")
        sys.exit(1)
    
    if not os.path.exists(init_bin_path):
        print(f"[!] Warning: init.bin not found: {init_bin_path}")
        print("[!] Creating disk without init.bin...")
    
    # Create disk image
    if create_fat32_disk(output_image, size_mb, kernel_path, init_bin_path):
        print(f"\n[+] Disk image created successfully: {output_image}")
        print(f"[+] Filesystem: FAT32")
        print(f"[+] Contents: /bin/init.bin, /usr/, /boot/grub/grub.cfg")
        print("\n[*] To test with QEMU:")
        print(f"    qemu-system-i386 -drive file={output_image},format=raw -m 256M")
        print("\n[*] To write to USB device:")
        print(f"    sudo dd if={output_image} of=/dev/sdX bs=1M status=progress")
    else:
        print("[!] Failed to create disk image")
        sys.exit(1)

if __name__ == '__main__':
    main()
