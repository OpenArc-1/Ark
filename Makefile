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
#
# Override any variable via:
#   make VAR=value ...
# or create config.mk (see "Configuration" below).

# ------------------------------------------------------------------------------
# Configuration — toggle everything here or in config.mk
# ------------------------------------------------------------------------------

# Tools (use ?= to allow env override)
ARCH        ?= x86
CC          ?= gcc
LD          ?= ld
AS          ?= as
OBJCOPY     ?= objcopy
NASM        ?= nasm
QEMU        ?= qemu-system-i386
PYTHON      ?= python3

# Build type
DEBUG       ?= 0          # 1 = -O0 -g, 0 = use OPT level
OPT         ?= 2          # 0,1,2,s — optimization (ignored if DEBUG=1)
WERROR      ?= 0          # 1 = -Werror
EXTRA_CFLAGS  ?=          # extra C flags
EXTRA_LDFLAGS ?=          # extra linker flags

# Kernel include path
KERNEL_INC  ?= include

# init.bin userspace
BUILD_INIT  ?= 1          # 1 = build init.bin, 0 = skip
INIT_TEXT   ?= 0x1000     # init.bin .text load address
INIT_DATA   ?= 0x2000     # init.bin .data load address

# QEMU
QEMU_RAM_MB ?= 256
QEMU_NOGRAPHIC ?= 0       # 1 = -nographic, 0 = graphical
QEMU_NET    ?= 1          # 1 = -device e1000
QEMU_USB    ?= 1          # 1 = -usb -device usb-kbd -device usb-mouse
QEMU_SMP    ?= 1          # number of CPUs (1 = unset)
QEMU_EXTRA  ?=            # extra QEMU args

# Disk images
DISK_SIZE_MB    ?= 256
DISK_IMAGE      ?= disk.img
DISK_FS_IMAGE   ?= disk-with-fs.img

# Init script (for run-with-demo-script)
DEMO_SCRIPT ?= ks/demo.init

# Optional local overrides (create config.mk, never committed)
-include config.mk

# ------------------------------------------------------------------------------
# Derived: CFLAGS, LDFLAGS, QEMU_FLAGS
# ------------------------------------------------------------------------------

ifeq ($(DEBUG),1)
  OPT_CFLAGS := -O0 -g
else
  ifeq ($(OPT),0)
    OPT_CFLAGS := -O0
  else ifeq ($(OPT),1)
    OPT_CFLAGS := -O1
  else ifeq ($(OPT),s)
    OPT_CFLAGS := -Os
  else
    OPT_CFLAGS := -O2
  endif
endif

WARN_CFLAGS := -Wall -Wextra
ifeq ($(WERROR),1)
  WARN_CFLAGS += -Werror
endif

CFLAGS := -m32 -fno-pic -fno-pie -std=gnu99 -ffreestanding $(OPT_CFLAGS) $(WARN_CFLAGS) -I$(KERNEL_INC) $(EXTRA_CFLAGS)
LDFLAGS := -m32 -nostdlib -no-pie $(EXTRA_LDFLAGS)

# QEMU base
QEMU_FLAGS := -m $(QEMU_RAM_MB)M
ifeq ($(QEMU_NOGRAPHIC),1)
  QEMU_FLAGS += -nographic
endif
ifeq ($(QEMU_NET),1)
  QEMU_FLAGS += -device e1000
endif
ifeq ($(QEMU_USB),1)
  QEMU_FLAGS += -usb -device usb-kbd -device usb-mouse
endif
ifneq ($(QEMU_SMP),1)
  QEMU_FLAGS += -smp $(QEMU_SMP)
endif
QEMU_FLAGS += $(QEMU_EXTRA)

# ------------------------------------------------------------------------------
# Sources and objects
# ------------------------------------------------------------------------------

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
USERSPACE_SRCS := $(wildcard userspace/*.c)
USERSPACE_ASM  := $(wildcard userspace/*.S)
NASMSRCS       := mp/bios.S
GASSRCS        := $(wildcard arch/$(ARCH)/*.S)

OBJS         := $(SRCS:.c=.o) $(NASMSRCS:.S=.o) $(GASSRCS:.S=.o)
USERSPACE_OBJS := $(USERSPACE_SRCS:.c=.o) $(USERSPACE_ASM:.S=.o)

# ------------------------------------------------------------------------------
# Targets
# ------------------------------------------------------------------------------

.PHONY: all clean help config \
	run run-disk run-disk-with-fs run-nographic run-with-demo-script run-with-script \
	bzImage init.bin disk.img disk-with-fs.img

all: bzImage
ifneq ($(BUILD_INIT),0)
all: init.bin
endif

# Kernel
bzImage: linker.ld $(OBJS)
	$(CC) $(LDFLAGS) -T linker.ld -o $@ $(OBJS)
	@echo "[+] Built bzImage"

# Userspace init (optional)
init.bin: userspace/linker.ld $(USERSPACE_OBJS)
	$(LD) -m elf_i386 -Ttext=$(INIT_TEXT) -Tdata=$(INIT_DATA) -T userspace/linker.ld -o $@ $(USERSPACE_OBJS)
	@echo "[+] Built init.bin"

# C
%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

# NASM
mp/%.o: mp/%.S
	$(NASM) -f elf32 $< -o $@

# GAS
arch/$(ARCH)/%.o: arch/$(ARCH)/%.S
	$(AS) --32 -o $@ $<

# Userspace asm
userspace/%.o: userspace/%.S
	$(AS) --32 -o $@ $<

# ------------------------------------------------------------------------------
# Clean
# ------------------------------------------------------------------------------

clean:
	rm -f $(OBJS) $(USERSPACE_OBJS) bzImage init.bin $(DISK_IMAGE) $(DISK_FS_IMAGE) disk-with-init.img
	@echo "[+] Cleaned"

# ------------------------------------------------------------------------------
# Disk images
# ------------------------------------------------------------------------------

disk.img: bzImage
	@$(PYTHON) scripts/create_bootable_image.py bzImage $(DISK_IMAGE) $(DISK_SIZE_MB)

disk-with-fs.img: bzImage init.bin
	@echo "[*] Creating $(DISK_SIZE_MB)MB FAT32 disk: $(DISK_FS_IMAGE)"
	@$(PYTHON) scripts/create_disk_with_init.py bzImage init.bin $(DISK_FS_IMAGE) $(DISK_SIZE_MB)

# ------------------------------------------------------------------------------
# QEMU run targets
# ------------------------------------------------------------------------------

run: bzImage
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage

run-disk: bzImage $(DISK_IMAGE)
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -drive file=$(DISK_IMAGE),format=raw

run-disk-with-fs: bzImage $(DISK_FS_IMAGE)
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -drive file=$(DISK_FS_IMAGE),format=raw -initrd init.bin

# Serial-only run (nographic + init.bin as module)
run-nographic: bzImage init.bin
	$(QEMU) $(QEMU_FLAGS) -nographic -kernel bzImage -initrd init.bin

# Demo script + init.bin as multiboot modules
run-with-demo-script: bzImage init.bin $(DEMO_SCRIPT)
	@echo "[*] Running with demo script: $(DEMO_SCRIPT)"
	$(QEMU) $(QEMU_FLAGS) -nographic -kernel bzImage -initrd "$(DEMO_SCRIPT),init.bin"

# Script-based init (disk with init); ensure disk has init.bin
run-with-script: bzImage init.bin $(DISK_IMAGE)
	@echo "[*] Running with script init + disk"
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -drive file=$(DISK_IMAGE),format=raw

# ------------------------------------------------------------------------------
# Help and config dump
# ------------------------------------------------------------------------------

help:
	@echo "Ark Kernel Build — configurable Makefile"
	@echo ""
	@echo "Toggles (override via make VAR=val or config.mk):"
	@echo "  ARCH CC LD AS NASM QEMU PYTHON | DEBUG OPT WERROR | BUILD_INIT INIT_TEXT INIT_DATA"
	@echo "  QEMU_RAM_MB QEMU_NOGRAPHIC QEMU_NET QEMU_USB QEMU_SMP QEMU_EXTRA"
	@echo "  DISK_SIZE_MB DISK_IMAGE DISK_FS_IMAGE | DEMO_SCRIPT | KERNEL_INC EXTRA_CFLAGS EXTRA_LDFLAGS"
	@echo ""
	@echo "Targets:"
	@echo "  all                  Build kernel (+ init.bin if BUILD_INIT=1)"
	@echo "  bzImage              Kernel ELF"
	@echo "  init.bin             Userspace init (if BUILD_INIT=1)"
	@echo "  disk.img             Bootable disk ($(DISK_IMAGE))"
	@echo "  disk-with-fs.img     FAT32 disk with init ($(DISK_FS_IMAGE))"
	@echo "  clean                Remove build artifacts"
	@echo "  run                  QEMU -kernel bzImage"
	@echo "  run-disk             QEMU + disk"
	@echo "  run-disk-with-fs     QEMU + FAT32 disk"
	@echo "  run-nographic        QEMU -nographic"
	@echo "  run-with-demo-script QEMU + demo init script + init.bin"
	@echo "  run-with-script      QEMU + disk (script init)"
	@echo "  config               Show current configuration"
	@echo ""
	@echo "Override any variable: make VAR=value [target]"
	@echo "Or create config.mk with overrides (e.g. DEBUG=1, QEMU_NOGRAPHIC=1)."
	@echo ""
	@echo "Examples:"
	@echo "  make DEBUG=1 run"
	@echo "  make QEMU_NOGRAPHIC=1 QEMU_RAM_MB=512 run"
	@echo "  make BUILD_INIT=0 all"
	@echo "  make WERROR=1"

config:
	@echo "Architecture:     ARCH=$(ARCH)"
	@echo "Tools:            CC=$(CC) LD=$(LD) AS=$(AS) NASM=$(NASM) QEMU=$(QEMU) PYTHON=$(PYTHON)"
	@echo "Build:            DEBUG=$(DEBUG) OPT=$(OPT) WERROR=$(WERROR)"
	@echo "Kernel include:   KERNEL_INC=$(KERNEL_INC)"
	@echo "Init:             BUILD_INIT=$(BUILD_INIT) INIT_TEXT=$(INIT_TEXT) INIT_DATA=$(INIT_DATA)"
	@echo "QEMU:             RAM=$(QEMU_RAM_MB)M NOGRAPHIC=$(QEMU_NOGRAPHIC) NET=$(QEMU_NET) USB=$(QEMU_USB) SMP=$(QEMU_SMP)"
	@echo "Disk:             DISK_SIZE_MB=$(DISK_SIZE_MB) DISK_IMAGE=$(DISK_IMAGE) DISK_FS_IMAGE=$(DISK_FS_IMAGE)"
	@echo "Demo script:      DEMO_SCRIPT=$(DEMO_SCRIPT)"
	@echo "CFLAGS:           $(CFLAGS)"
	@echo "LDFLAGS:          $(LDFLAGS)"
	@echo "QEMU_FLAGS:       $(QEMU_FLAGS)"
