#!/bin/bash
# make_disk.sh — create a FAT16 disk image with a .init script
#
# Usage:
#   scripts/make_disk.sh [output.img] [init_script]
#
# Default: creates test.img with a sample .init script

OUT="${1:-test.img}"
INIT_SRC="${2:-}"

set -e

MB=16
SECTORS=$((MB * 1024 * 1024 / 512))

echo "[*] Creating ${MB}MB FAT16 image: $OUT"
dd if=/dev/zero of="$OUT" bs=512 count=$SECTORS status=none

# Format as FAT16
mkfs.fat -F 16 -n "ARKDISK" "$OUT" > /dev/null

# Write the .init script
if [ -n "$INIT_SRC" ] && [ -f "$INIT_SRC" ]; then
    echo "[*] Copying $INIT_SRC -> INIT"
    mcopy -i "$OUT" "$INIT_SRC" "::INIT"
else
    # Default sample .init
    TMP=$(mktemp)
    cat > "$TMP" << 'INITEOF'
#!/init
# Ark .init script — loaded from disk image
echo Ark init script running from disk
echo Hello from disk .init!
echo Loading userspace...
INITEOF
    echo "[*] Writing sample .init script"
    mcopy -i "$OUT" "$TMP" "::INIT"
    rm "$TMP"
fi

echo "[*] Done: $OUT"
echo "[*] Run with: make run-with-disk DISK=$OUT"
echo "    or:       make run QEMU_EXTRA=\"-drive file=$OUT,format=raw,if=ide\""
