# Simple build system for Ark kernel (x86, Multiboot, linear framebuffer).

ARCH        ?= x86
# Build with the host GCC as a 32‑bit freestanding compiler.
CC          ?= gcc
AS          ?= as
# IMPORTANT: force using gcc as the linker driver.
# Some environments export LD=ld, which breaks -m32 linking.
LD          := $(CC)
OBJCOPY     ?= objcopy

CFLAGS      := -m32 -fno-pic -fno-pie -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Iinclude
ASFLAGS     := --32
# Freestanding 32‑bit link, no CRT or host libs, no PIE.
LDFLAGS     := -m32 -nostdlib -no-pie

QEMU        ?= qemu-system-i386
QEMU_FLAGS  ?= -m 256M

HOST_CC     ?= gcc
HOST_CFLAGS ?= -std=c99 -Wall -Wextra
HOST_LDFLAGS ?= -lncurses
MCONFIG     ?= ./kconfig/menuconfig

SRCS := \
    $(wildcard gen/*.c) \
    $(wildcard fb/*.c) \
    $(wildcard fs/*.c) \
    $(wildcard hid/*.c) \
    $(wildcard io/*.c) \
    $(wildcard ks/*.c) \
    $(wildcard mem/*.c) \
    $(wildcard mp/*.c) \
    $(wildcard usb/*.c) \
    $(wildcard wf/*.c) \
    $(wildcard arch/$(ARCH)/*.c)

OBJS := $(SRCS:.c=.o)

all: bzImage

bzImage: linker.ld $(OBJS)
	$(LD) $(LDFLAGS) -T linker.ld -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Build a minimal init.bin for testing (32-bit flat binary)
init.bin: gen/init.c
	$(CC) $(CFLAGS) -c gen/init.c -o /tmp/init_test.o 2>/dev/null || \
	echo "int main(void) { return 0; }" | $(CC) $(CFLAGS) -x c - -o init.bin

clean:
	rm -f $(OBJS) bzImage init.bin ark.img
	rm -f kconfig/menuconfig

# Create bootable disk image with GRUB2 and kernel
disk.img: bzImage
	@echo "Creating bootable disk image..."
	@scripts/create_bootable_image.py bzImage disk.img 256
	@echo "Bootable image created: disk.img"

# Boot from bootable image
run-disk: disk.img
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -drive file=disk.img,format=raw -m 256M
run: bzImage
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage

# Run kernel with external init.bin as Multiboot module
run-with-init: bzImage init.bin
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -initrd init.bin,/init.bin

# Build a test init.bin and run
test-init: bzImage
	echo "Building test init.bin..."
	echo 'void _start(void) { while(1); }' > /tmp/init_test.c
	$(CC) $(CFLAGS) -c /tmp/init_test.c -o /tmp/init_test.o 2>/dev/null
	$(LD) $(LDFLAGS) -e _start -o init.bin /tmp/init_test.o 2>/dev/null || \
	echo "U Boot Image" | dd of=init.bin 2>/dev/null || \
	dd if=/dev/zero of=init.bin bs=1024 count=4
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -initrd init.bin,/init.bin

nogui:
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -nographic
# Host-side menuconfig tool (uses ncurses) to write .kconfig.
menuconfig: kconfig/menuconfig

kconfig/menuconfig: kconfig/menuconfig.c
	$(HOST_CC) $(HOST_CFLAGS) -o $@ $< $(HOST_LDFLAGS)
	$(MCONFIG)

# List and check availability of common toolchain components.
TOOLS := $(CC) $(LD) $(OBJCOPY) $(QEMU) qemu-system-x86_64 nasm

list:
	@echo "Checking Ark build tools:"
	@set -e; \
	for t in $(TOOLS); do \
	  printf "  %-20s" "$$t"; \
	  if command -v $$t >/dev/null 2>&1; then \
	    echo " - found"; \
	  else \
	    echo " - MISSING"; \
	@echo "  make         - build the kernel image (bzImage)"
	@echo "  make run          - run kernel via QEMU Multiboot"
	@echo "  make run-with-init - run kernel with init.bin as Multiboot module"
	@echo "  make test-init    - build test init.bin and run"
	@echo "  make menuconfig   - build the menuconfig tool"
	@echo "  make clean        - clean all build artifacts"
	@echo "  make list         - list build tools"
	@echo "  make help         - show this help message"
.PHONY: all clean run list

