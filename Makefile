# Ark Kernel Build System (x86, 32-bit freestanding)

ARCH        ?= x86
CC          ?= gcc
LD          := $(CC)
OBJCOPY     ?= objcopy
NASM        ?= nasm
QEMU        ?= qemu-system-i386
QEMU_FLAGS  ?= -m 256M

CFLAGS      := -m32 -fno-pic -fno-pie -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Iinclude
LDFLAGS     := -m32 -nostdlib -no-pie

# Source files
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

# Assembly files
NASMSRCS := mp/bios.S
GASSRCS  := $(wildcard arch/$(ARCH)/*.S)

# Object files
OBJS := $(SRCS:.c=.o)
OBJS += $(NASMSRCS:.S=.o)
OBJS += $(GASSRCS:.S=.o)

# Default target
all: bzImage

# Link kernel
bzImage: linker.ld $(OBJS)
	$(LD) $(LDFLAGS) -T linker.ld -o $@ $(OBJS)

# Compile C files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile NASM files (16-bit BIOS helpers)
mp/%.o: mp/%.S
	$(NASM) -f elf32 $< -o $@

# Compile GAS files
arch/$(ARCH)/%.o: arch/$(ARCH)/%.S
	$(AS) --32 -o $@ $<

# Clean build
clean:
	rm -f $(OBJS) bzImage init.bin ark.img

# Bootable disk image (optional)
disk.img: bzImage
	@scripts/create_bootable_image.py bzImage disk.img 256

# QEMU targets
run: bzImage
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage

run-disk: disk.img
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -drive file=disk.img,format=raw -m 256M -device e1000 -usb -device usb-kbd -device usb-mouse

run-nographic: bzImage
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -nographic -device e1000 -usb -device usb-kbd -device usb-mouse

.PHONY: all clean run run-disk run-nographic
