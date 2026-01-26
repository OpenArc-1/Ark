# Ark Kernel Build System (x86, 32-bit freestanding)

ARCH        ?= x86
CC          ?= gcc
LD          ?= ld
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
    $(wildcard arch/$(ARCH)/*.c) \

# Userspace sources (init.bin - shell)
USERSPACE_SRCS := $(wildcard userspace/*.c)
USERSPACE_ASM := $(wildcard userspace/*.S)
# Assembly files
NASMSRCS := mp/bios.S
GASSRCS  := $(wildcard arch/$(ARCH)/*.S)

# Object files
OBJS := $(SRCS:.c=.o)
OBJS += $(NASMSRCS:.S=.o)
OBJS += $(GASSRCS:.S=.o)

# Userspace object files (init.bin)
USERSPACE_OBJS := $(USERSPACE_SRCS:.c=.o)
USERSPACE_OBJS += $(USERSPACE_ASM:.S=.o)

# Default target
all: bzImage init.bin

# Link kernel
bzImage: linker.ld $(OBJS)
	$(CC) $(LDFLAGS) -T linker.ld -o $@ $(OBJS)

# Build userspace init binary (keep as ELF)
init.bin: userspace/linker.ld $(USERSPACE_OBJS)
	$(LD) -m elf_i386 -Ttext=0x1000 -Tdata=0x2000 -T userspace/linker.ld -o $@ $(USERSPACE_OBJS)
	@echo "Built init.bin"
	@ls -lh init.bin

# Compile C files
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# Compile NASM files (16-bit BIOS helpers)
mp/%.o: mp/%.S
	$(NASM) -f elf32 $< -o $@

# Compile GAS files
arch/$(ARCH)/%.o: arch/$(ARCH)/%.S
	$(AS) --32 -o $@ $<

# Compile userspace assembly files
userspace/%.o: userspace/%.S
	$(AS) --32 -o $@ $<

# Clean build
clean:
	rm -f $(OBJS) $(USERSPACE_OBJS) bzImage disk.img disk-with-fs.img disk-with-init.img

# Bootable disk image (optional)
disk.img: bzImage
	@scripts/create_bootable_image.py bzImage disk.img 256

# Disk image with init.bin in FAT32 filesystem
disk-with-fs.img: bzImage 
	@echo "[*] Creating disk image with FAT32..."
	@python3 scripts/create_disk_with_init.py bzImage disk-with-fs.img 256

# QEMU targets
run: bzImage
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage 

run-disk: disk.img
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -drive file=disk.img,format=raw -m 256M -device e1000 -usb -device usb-kbd -device usb-mouse

run-with-init: bzImage init.bin
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -nographic -m 256 -initrd init, init.bin

run-disk-with-fs: disk-with-fs.img
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -drive file=disk-with-fs.img,format=raw -m 256M -device e1000 -usb -device usb-kbd -device usb-mouse 

run-nographic: bzImage
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -nographic -device e1000 -usb -device usb-kbd -device usb-mouse

# Run with script-based init system
# Loads both the init script and init.bin as multiboot modules
# Note: QEMU passes modules via -initrd with comma-separated files
# The module names will be set by QEMU, but our scanner will find the #!init script
run-with-script: bzImage init.bin init
	@echo "[*] Running kernel with script-based init system..."
	@echo "[*] Loading modules: init (script) and init.bin (binary)"
	@echo "[*] The script scanner will find #!init and execute file:/init.bin"
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage \
		-initrd init,init.bin \
		-m 256M -nographic


# Run with demo script from ks/ folder
run-with-demo-script: bzImage init.bin ks/demo.init
	@echo "[*] Running kernel with demo script (ks/demo.init)..."
	@echo "[*] Loading modules: ks/demo.init (script) and init.bin (binary)"
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage \
		-initrd ks/demo.init,init.bin \
		-nographic -m 256M

.PHONY: all clean run run-disk run-with-init run-disk-with-fs run-nographic run-with-script run-with-demo-script
