#!/bin/bash
# make_initramfs.sh — Build initramfs.zip for Ark kernel
#
# Linux-like initramfs workflow:
#   1. Compile your /init binary (or script)  
#   2. Run this script to pack it into initramfs.zip
#   3. Tell GRUB to load initramfs.zip as a module
#   4. Ark kernel extracts it at boot, mounts /, executes /init
#
# Usage:
#   ./scripts/make_initramfs.sh [init_binary] [output.zip]
#   ./scripts/make_initramfs.sh userspace/init ArcOS/boot/initramfs.zip

set -e

INIT_BIN="${1:-userspace/init}"
OUT_ZIP="${2:-ArcOS/boot/initramfs.zip}"
STAGING_DIR=$(mktemp -d)

cleanup() { rm -rf "$STAGING_DIR"; }
trap cleanup EXIT

echo "[initramfs] Building $OUT_ZIP from $INIT_BIN"

if [ ! -f "$INIT_BIN" ]; then
    echo "ERROR: init binary not found: $INIT_BIN"
    echo "Build it first with: make init  (or: cd userspace && make)"
    exit 1
fi

# Create a minimal rootfs layout (like Linux initramfs)
mkdir -p "$STAGING_DIR"/{bin,dev,proc,sys,etc}

# The kernel looks for /init first (just like Linux)
cp "$INIT_BIN" "$STAGING_DIR/init"
chmod +x "$STAGING_DIR/init"

# Optionally copy extra files (bin/sh, etc.) if they exist
for extra in bin/sh bin/ls lib/libc.so; do
    if [ -f "$extra" ]; then
        mkdir -p "$STAGING_DIR/$(dirname $extra)"
        cp "$extra" "$STAGING_DIR/$extra"
    fi
done

# Pack into zip — use -j0 (store, no compression) for method-0 entries,
# or default deflate (method-8) which the kernel also supports via tinflate.
cd "$STAGING_DIR"
zip -r -9 "$OLDPWD/$OUT_ZIP" .
cd "$OLDPWD"

echo "[initramfs] Done: $OUT_ZIP"
echo "[initramfs] Contents:"
unzip -l "$OUT_ZIP"
echo ""
echo "[initramfs] Add to GRUB config:"
echo "   multiboot /boot/bzImage"
echo "   module    /boot/$(basename $OUT_ZIP)"
