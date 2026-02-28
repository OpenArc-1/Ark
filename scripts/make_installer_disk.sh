#!/bin/bash
# scripts/make_installer_disk.sh — Build a FAT16 disk image for the Ark installer
#
# Produces:  installer.img  (16 MB FAT16)
# Contents:
#   INIT       — #!init script that loads /installer
#   INSTALLER  — the installer ELF (VESA + PS/2 mouse)
#
# The Ark kernel's disk_init.c reads both files on boot:
#   INIT      -> ramfs /init        (the .init script, executed first)
#   INSTALLER -> ramfs /installer   (the ELF, launched by the script)
#
# Usage:
#   scripts/make_installer_disk.sh                    # defaults
#   scripts/make_installer_disk.sh installer.img      # custom image name
#   scripts/make_installer_disk.sh installer.img 32   # 32 MB image
#
# Requirements:
#   dosfstools  (mkfs.fat)
#   mtools      (mcopy, minfo)
#   gcc-multilib + make  (to build ark-gcc + installer)
#
# Install on Ubuntu/Debian:
#   sudo apt install dosfstools mtools gcc-multilib make binutils

set -e

OUT="${1:-installer.img}"
MB="${2:-16}"
SECTORS=$(( MB * 1024 * 1024 / 512 ))

INIT_SCRIPT="installer.init"
INSTALLER_ELF="installer"

# ── Sanity checks ────────────────────────────────────────────────────────
check_tool() {
    if ! command -v "$1" &>/dev/null; then
        echo "ERROR: '$1' not found. Install with:"
        echo "  sudo apt install $2"
        exit 1
    fi
}
check_tool mkfs.fat  dosfstools
check_tool mcopy     mtools

# ── Build libark + installer if not already built ────────────────────────
if [ ! -f "$INSTALLER_ELF" ]; then
    echo "[*] installer ELF not found — building..."
    if ! command -v ark-gcc &>/dev/null; then
        echo "[*] ark-gcc not in PATH — building and installing libark..."
        make -C ark-gcc libark
        sudo make -C ark-gcc install
        export PATH=$PATH:/bin/ark-gcc-32/bin
    fi
    make installer
fi

if [ ! -f "$INSTALLER_ELF" ]; then
    echo "ERROR: installer ELF still not found after build"
    exit 1
fi

if [ ! -f "$INIT_SCRIPT" ]; then
    echo "ERROR: $INIT_SCRIPT not found in current directory"
    echo "  Expected: installer.init (the #!init launch script)"
    exit 1
fi

# ── Size check ───────────────────────────────────────────────────────────
INST_SIZE=$(wc -c < "$INSTALLER_ELF")
INIT_SIZE=$(wc -c < "$INIT_SCRIPT")
AVAIL=$(( MB * 1024 * 1024 - 512 * 64 ))   # rough FAT overhead

echo "[*] installer ELF:  $(( INST_SIZE / 1024 )) KB"
echo "[*] init script:    $INIT_SIZE bytes"

if [ "$INST_SIZE" -gt 2097152 ]; then
    echo "WARNING: installer ELF > 2MB — kernel INSTALLER_BUF_SIZE may be too small"
fi

# ── Create raw disk image ─────────────────────────────────────────────────
echo "[*] Creating ${MB}MB FAT16 image: $OUT"
rm -f "$OUT"
dd if=/dev/zero of="$OUT" bs=512 count="$SECTORS" status=none

# Format as FAT16 with volume label ARKDISK
mkfs.fat -F 16 -n "ARKDISK" "$OUT" > /dev/null

echo "[*] FAT16 formatted (volume: ARKDISK)"

# ── Copy files into image ─────────────────────────────────────────────────
# The kernel's disk_init.c uses FAT 8.3 names:
#   INIT script  -> stored as "INIT"      (8.3: "INIT    " + "   ")
#   Installer ELF -> stored as "INSTALLER" (8.3: "INSTALLE" + "R  ")
#
# mcopy -i <img> <src> ::<FAT83NAME>

echo "[*] Writing INIT script -> ::INIT"
mcopy -i "$OUT" "$INIT_SCRIPT" "::INIT"

echo "[*] Writing installer ELF -> ::INSTALLER"
mcopy -i "$OUT" "$INSTALLER_ELF" "::INSTALLER"

# ── Verify ────────────────────────────────────────────────────────────────
echo "[*] Disk image contents:"
mdir -i "$OUT" :: 2>/dev/null || true

echo ""
echo "[+] Done: $OUT (${MB} MB FAT16)"
echo ""
echo "Run with QEMU:"
echo "  make run-with-disk DISK=$OUT"
echo ""
echo "Or directly:"
echo "  qemu-system-i386 -kernel bzImage -vga std -m 256M \\"
echo "    -drive file=$OUT,format=raw,if=ide,index=0 \\"
echo "    -device ps2-mouse \\"
echo "    -serial stdio"
