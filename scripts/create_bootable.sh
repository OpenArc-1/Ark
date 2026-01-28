#!/bin/bash
# pack-init.sh - Create bootable Ark OS disk with init.bin included

set -e

KERNEL="/mnt/c/Users/adnan/Desktop/Ark-main/bzImage"
INITBIN="/mnt/c/Users/adnan/Desktop/Ark-main/init.bin"
INITSC="/mnt/c/Users/adnan/Desktop/Ark-main/init.init"
OUTPUT="ark.img"
SIZE_MB=256

# Check files exist
if [ ! -f "$KERNEL" ]; then
    echo "Error: $KERNEL not found"
    exit 1
fi

if [ ! -f "$INITBIN" ]; then
    echo "Error: $INITBIN not found"
    exit 1
fi

echo "[*] Creating $SIZE_MB MB bootable disk image..."
dd if=/dev/zero of="$OUTPUT" bs=1M count=$SIZE_MB status=none

# Setup loop device
LOOPDEV=$(sudo losetup -f)
sudo losetup -P "$LOOPDEV" "$OUTPUT"

# Create partition table and FAT32 partition
sudo parted -s "$LOOPDEV" mklabel msdos
sudo parted -s "$LOOPDEV" mkpart primary fat32 1MiB 100%
sudo parted -s "$LOOPDEV" set 1 boot on

# Format FAT32
sudo mkfs.fat -F32 "${LOOPDEV}p1" >/dev/null

# Mount partition
MOUNTPOINT=$(mktemp -d)
sudo mount "${LOOPDEV}p1" "$MOUNTPOINT"

# Copy kernel and init.bin
sudo cp "$KERNEL" "$MOUNTPOINT/bzImage"
sudo cp "$INITBIN" "$MOUNTPOINT/init.bin"
sudo cp "$INITSC" "$MOUNTPOINT/init"

# Create GRUB config
sudo mkdir -p "$MOUNTPOINT/boot/grub"
cat << 'EOF' | sudo tee "$MOUNTPOINT/boot/grub/grub.cfg" >/dev/null
set timeout=5
set default=0

menuentry "Ark OS with init.bin" {
    multiboot /bzImage
    linux /init.bin
}
EOF

# Install GRUB2
if command -v grub-install &>/dev/null; then
    sudo grub-install --target=i386-pc --boot-directory="$MOUNTPOINT/boot" "$LOOPDEV"
    echo "[+] GRUB2 installed"
else
    echo "[!] grub-install not found, GRUB2 not installed"
fi

# Cleanup
sudo umount "$MOUNTPOINT"
sudo losetup -d "$LOOPDEV"
rm -rf "$MOUNTPOINT"

echo "[+] Bootable disk created: $OUTPUT"
echo "To test:"
echo "  qemu-system-i386 -drive file=$OUTPUT,format=raw -m 256M -serial stdio"
