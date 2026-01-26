#!/bin/bash

# Helper script to generate bootable Ark OS images with GRUB2

set -e

KERNEL_FILE="${1:-../bzImage}"
OUTPUT_IMAGE="${2:-image.iso}"
SIZE_MB="${3:-25}"

if [ ! -f "$KERNEL_FILE" ]; then
    echo "Error: Kernel file not found: $KERNEL_FILE"
    exit 1
fi

echo "[*] Creating $SIZE_MB MB bootable image..."

# Create disk image
dd if=/dev/zero of="$OUTPUT_IMAGE" bs=1M count=$SIZE_MB 2>/dev/null

echo "[*] Installing GRUB2..."

# Create temporary mount point
TMPDIR=$(mktemp -d)
trap "rm -rf $TMPDIR" EXIT

# Setup loop device (requires root)
LOOPDEV=$(sudo losetup -f)
sudo losetup -P "$LOOPDEV" "$OUTPUT_IMAGE"

# Create MBR partition table and FAT32 partition
sudo parted -s "${LOOPDEV}" mklabel msdos
sudo parted -s "${LOOPDEV}" mkpart primary fat32 1MiB 100%
sudo parted -s "${LOOPDEV}" set 1 boot on

# Format as FAT32
sudo mkfs.fat -F 32 "${LOOPDEV}p1" 2>/dev/null

# Mount partition
MOUNTPOINT="$TMPDIR/mnt"
mkdir -p "$MOUNTPOINT"
sudo mount "${LOOPDEV}p1" "$MOUNTPOINT"

# Create boot directories
sudo mkdir -p "$MOUNTPOINT/boot/grub"
sudo mkdir -p "$MOUNTPOINT/boot/modules"

# Copy kernel
sudo cp "$KERNEL_FILE" "$MOUNTPOINT/bzImage"

# Create GRUB configuration
cat > "$TMPDIR/grub.cfg" << 'EOF'
set timeout=5
set default=0

menuentry 'Ark based demo bootable image' {
    multiboot /bzImage
}
EOF

sudo cp "$TMPDIR/grub.cfg" "$MOUNTPOINT/boot/grub/"

# Install GRUB2 (requires grub-install)
if command -v grub-install &> /dev/null; then
    sudo grub-install --root-directory="$MOUNTPOINT" --boot-directory="$MOUNTPOINT/boot" "${LOOPDEV}" --target=i386-pc
    echo "[+] GRUB2 installed"
else
    echo "[!] Warning: grub-install not found, GRUB2 installation skipped"
    echo "    Install with: sudo apt-get install grub2"
fi

# Unmount and cleanup
sudo umount "$MOUNTPOINT"
sudo losetup -d "$LOOPDEV"

echo "[+] Bootable disk image created: $OUTPUT_IMAGE"
echo ""
echo "To test with QEMU:"
echo "  qemu-system-i386 -drive file=$OUTPUT_IMAGE,format=raw -m 256M"
echo ""
echo "To write to USB (WARNING: destructive):"
echo "  sudo dd if=$OUTPUT_IMAGE of=/dev/sdX bs=4M status=progress"
echo "  (Replace /dev/sdX with your USB device)"
