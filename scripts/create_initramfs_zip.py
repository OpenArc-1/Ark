#!/usr/bin/env python3
"""
create_initramfs_zip.py — build an initramfs.zip for Ark kernel

Usage:
    python3 scripts/create_initramfs_zip.py <init_binary> [output.zip] [extra_file:zip_path ...]

Example:
    python3 scripts/create_initramfs_zip.py init.bin initramfs.zip
    python3 scripts/create_initramfs_zip.py init.bin initramfs.zip bin/sh:bin/sh

The ZIP must contain a file named exactly 'init' (no leading slash needed —
the kernel's zip.c normalises all paths to /name automatically).

QEMU boot:
    qemu-system-i386 -kernel bzImage -initrd initramfs.zip
    (QEMU passes -initrd as multiboot mods[0]; kernel detects PK magic → extracts)
"""

import sys, os, zipfile

def main():
    if len(sys.argv) < 2:
        print(__doc__)
        sys.exit(1)

    init_bin = sys.argv[1]
    out_zip  = sys.argv[2] if len(sys.argv) > 2 else "initramfs.zip"
    extras   = sys.argv[3:]  # "host_path:zip_path" pairs

    if not os.path.isfile(init_bin):
        print(f"ERROR: init binary not found: {init_bin}", file=sys.stderr)
        sys.exit(1)

    with zipfile.ZipFile(out_zip, 'w', zipfile.ZIP_DEFLATED) as zf:
        # /init is the first thing the kernel looks for
        zf.write(init_bin, 'init')
        print(f"  added: init  ({os.path.getsize(init_bin)} bytes)  <- {init_bin}")

        for extra in extras:
            if ':' in extra:
                host, zpath = extra.split(':', 1)
            else:
                host  = extra
                zpath = os.path.basename(extra)
            if os.path.isfile(host):
                zf.write(host, zpath)
                print(f"  added: {zpath}  ({os.path.getsize(host)} bytes)  <- {host}")
            else:
                print(f"  WARN: extra file not found, skipping: {host}", file=sys.stderr)

    print(f"\nBuilt {out_zip}:")
    with zipfile.ZipFile(out_zip) as zf:
        for info in zf.infolist():
            method = "stored" if info.compress_type == 0 else "deflated"
            print(f"  {info.filename:<30} {info.file_size:>8} bytes  ({method})")

    print(f"\nTo run:")
    print(f"  qemu-system-i386 -kernel bzImage -initrd {out_zip}")
    print(f"  make run-with-zip INITRAMFS_ZIP={out_zip}")

if __name__ == '__main__':
    main()
