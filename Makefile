# -------------------------------------------------------------------
# Ark OS Build System - Multi-Architecture Support (x86/x86_64, 32/64-bit)
# Minimal Linux-style output
# -------------------------------------------------------------------
# Kernel INFO
CODE_NAME   ?= affactionate cat
BUILD_DATE  ?= 2026/2/11 5:52 PM

# Architecture & Bitness Configuration
ARCH        ?= x86
BITS        ?= 32
MACHINE     := i686
QEMU_SYSTEM := qemu-system-i386

ifeq ($(ARCH),x86_64)
  BITS ?= 64
  MACHINE := x86_64
  QEMU_SYSTEM := qemu-system-x86_64
endif

# Tools
CC          ?= gcc
LD          ?= gcc
AS          ?= as
NASM        ?= nasm
OBJCOPY     ?= objcopy
QEMU        ?= $(QEMU_SYSTEM)
PYTHON      ?= python3
HOSTCC      ?= gcc
NCURSES_LIB  ?= -lncurses

# Load menuconfig-generated variables (ARCH, BITS, etc) if present
-include .kconfig

# Build options
DEBUG       ?= 0
OPT         ?= 2
WERROR      ?= 0
BUILD_INIT  ?= 0
EXTRA_CFLAGS  ?=
EXTRA_LDFLAGS ?=

# Kernel include
KERNEL_INC  ?= include

#demo for cat

DEMO_CAT ?= new.txt
# QEMU
QEMU_RAM_MB ?= 256
QEMU_NOGRAPHIC ?= 0
QEMU_NET    ?= 1
QEMU_USB    ?= 1
QEMU_SMP    ?= 1
QEMU_EXTRA  ?=

# Disk images
DISK_SIZE_MB    ?= 256
DISK_IMAGE      ?= disk.img
DISK_FS_IMAGE   ?= disk-with-fs.img

#userspace include
INXU = userspace/usr/

# Demo script
DEMO_SCRIPT ?= ks/demo.init

# Output directory for object files
OBJDIR := compiled

#tool chain
GIT_TOOLCHAIN_REPO ?= https://github.com/OpenArc-1/ark-gcc.git
GIT ?= git clone

# -------------------------------------------------------------------
# Derived flags
# -------------------------------------------------------------------

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

CFLAGS := -m32 -fno-pic -fno-pie -std=gnu99 -ffreestanding \
          $(OPT_CFLAGS) $(WARN_CFLAGS) -I$(KERNEL_INC) $(EXTRA_CFLAGS) -Wno-unused-function -Wno-unused-variable -Wno-inplicit-function-declaration \
		    -w -Wno-error=implicit-function-declaration \


LDFLAGS := -m32 -nostdlib -nostartfiles -no-pie $(EXTRA_LDFLAGS)

# QEMU base flags
QEMU_FLAGS := -m $(QEMU_RAM_MB)M
ifeq ($(QEMU_NOGRAPHIC),1)
  QEMU_FLAGS += -nographic
endif
ifeq ($(QEMU_NET),1)
  QEMU_FLAGS += -device e1000
endif
ifeq ($(QEMU_USB),1)
  QEMU_FLAGS += -usb -device usb-mouse
endif
ifneq ($(QEMU_SMP),1)
  QEMU_FLAGS += -smp $(QEMU_SMP)
endif
QEMU_FLAGS += $(QEMU_EXTRA)

# -------------------------------------------------------------------
# Sources
# -------------------------------------------------------------------

SRCS := $(wildcard gen/*.c) \
        $(wildcard fb/*.c) \
        $(wildcard fs/*.c) \
        $(wildcard hid/*.c) \
        $(wildcard io/*.c) \
        $(wildcard ks/*.c) \
        $(wildcard mem/*.c) \
        $(wildcard mp/*.c) \
        $(wildcard usb/*.c) \
        $(wildcard wf/*.c) \
        $(wildcard arch/$(ARCH)/*.c)\
	    $(wildcard mm/*c) \
		$(wildcard gpu/*c)
NASMSRCS := mp/bios.S
GASSRCS  := $(wildcard arch/$(ARCH)/*.S)
# Map all .o paths into compiled/
OBJS         := $(patsubst %,$(OBJDIR)/%,$(SRCS:.c=.o) $(NASMSRCS:.S=.o) $(GASSRCS:.S=.o))
# -------------------------------------------------------------------
# Targets
# -------------------------------------------------------------------

.PHONY: all clean run run-disk run-disk-with-fs run-nographic run-with-demo-script run-with-zip run-with-zip-nographic libark init
 .PHONY: menuconfig kconfig

all: bzImage
ifneq ($(BUILD_INIT),0)
all: init
endif

# ------------------------
# Kconfig (host-side) + generated header
# ------------------------
KCONFIG_TOOL   := kconfig/menuconfig
KCONFIG_IN     := .kconfig
KCONFIG_HEADER := include/ark/kconfig.h

$(KCONFIG_TOOL): kconfig/menuconfig.c
	@printf "  HOSTCC  %s\n" $@
	@$(HOSTCC) -O2 -o $@ $< $(NCURSES_LIB)

menuconfig: $(KCONFIG_TOOL)
	@$(KCONFIG_TOOL)
	@$(PYTHON) scripts/gen_kconfig_h.py $(KCONFIG_IN) $(KCONFIG_HEADER)

kconfig: $(KCONFIG_HEADER)

$(KCONFIG_HEADER): scripts/gen_kconfig_h.py $(KCONFIG_IN)
	@printf "  GEN     %s\n" $@
	@$(PYTHON) scripts/gen_kconfig_h.py $(KCONFIG_IN) $(KCONFIG_HEADER)

# ------------------------
# Userspace init (init.bin)
# ------------------------
ARK_GCC ?= ark-gcc/ark-gcc

libark:
	@$(MAKE) -C ark-gcc libark

# init.bin uses the kernel API directly (not syscalls), so it needs:
#   - kernel include/ for ark/init_api.h, ark/types.h, etc.
#   - a custom crt0 (userspace/ark_crt0.S) that passes the api pointer to main()
#     instead of argc/argv (ark-gcc's crt0 uses argc/argv convention)
INIT_CFLAGS ?= -I$(KERNEL_INC)

userspace/ark_crt0.o: userspace/ark_crt0.S
	@printf "  AS      %s\n" $@
	@$(CC) -m32 -c $< -o $@

init: userspace/init.c userspace/ark_crt0.o | libark
	@printf "  CC      userspace/init.c\n"
	@$(CC) -m32 -ffreestanding -fno-pic -fno-pie -nostdlib -std=gnu99 \
	  $(INIT_CFLAGS) -Iark-gcc/include -c userspace/init.c -o userspace/init.o
	@printf "  LD      %s\n" $@
	@$(CC) -m32 -nostdlib -no-pie -T userspace/linker.ld \
	  -o $@ userspace/ark_crt0.o userspace/init.o ark-gcc/libark/build/libark.a

# ------------------------
# Kernel build
# ------------------------
bzImage: linker.ld $(OBJS)
	@printf "  LD      %s\n" $@
	@$(CC) $(LDFLAGS) -T linker.ld -o $@ $(OBJS)
	@printf "Kernel built: %s\n" $@

# ------------------------
# Ensure compiled/ subdirs exist before compiling
# ------------------------
$(OBJDIR)/%.o: | $(OBJDIR)

$(OBJDIR):
	@mkdir -p $(OBJDIR)

# ------------------------
# Compile C files
# ------------------------
$(OBJDIR)/%.o: %.c $(KCONFIG_HEADER)
	@mkdir -p $(dir $@)
	@printf "  CC      %s\n" $@
	@$(CC) $(CFLAGS) -c $< -o $@

# NASM assembly
$(OBJDIR)/mp/%.o: mp/%.S
	@mkdir -p $(dir $@)
	@printf "  AS      %s\n" $@
	@$(NASM) -f elf32 $< -o $@

# GAS assembly (arch)
$(OBJDIR)/arch/$(ARCH)/%.o: arch/$(ARCH)/%.S
	@mkdir -p $(dir $@)
	@printf "  AS      %s\n" $@
	@$(AS) --32 -o $@ $<

# GAS assembly (userspace)
$(OBJDIR)/userspace/%.o: userspace/%.S
	@mkdir -p $(dir $@)
	@printf "  AS      %s\n" $@
	@$(AS) --32 -o $@ $<

# ------------------------
# Clean
# ------------------------
clean:
	rm -rf $(OBJDIR) bzImage init.bin init \
	      userspace/init.o userspace/ark_crt0.o \
	      $(DISK_IMAGE) $(DISK_FS_IMAGE) disk-with-init.img
	@echo ":: Cleaned"

# ------------------------
# Disk images
# ------------------------
disk.img: bzImage
	@$(PYTHON) scripts/create_bootable_image.py bzImage $(DISK_IMAGE) $(DISK_SIZE_MB)

disk-with-fs.img: bzImage 
	@$(PYTHON) scripts/create_disk_with_init.py bzImage init.bin $(DISK_FS_IMAGE) $(DISK_SIZE_MB)

# ------------------------
# QEMU targets
# ------------------------
# -vga std gives us the VBE linear framebuffer that our multiboot header requests.
# The kernel reads framebuffer_addr/pitch/width/height from multiboot and renders
# text via the 8x16 bitmap font directly onto the 1024x768x32 surface.
# -vga std enables the Bochs VGA adapter (BGA) in QEMU.
# The kernel writes BGA I/O ports 0x01CE/0x01CF to switch to 1024x768x32
# from protected mode -- no BIOS calls needed.
run: bzImage
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -vga std

run-vesa: bzImage
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -vga std -serial stdio

# Attach any disk image containing a .init script.
# Usage: make run-with-disk DISK=test.img
#        make run-with-disk DISK=myfs.img
# The kernel will scan ATA bus 0 drive 0 for FAT16/FAT32 and load the INIT file.
DISK ?= test.img
run-with-disk: bzImage
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -vga std \
	  -drive file=$(DISK),format=raw,if=ide,index=0

run-disk: bzImage $(DISK_IMAGE)
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -drive file=$(DISK_IMAGE),format=raw

run-disk-with-fs: bzImage $(DISK_FS_IMAGE)
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -drive file=$(DISK_FS_IMAGE),format=raw -full-screen

run-nographic: bzImage
	$(QEMU) $(QEMU_FLAGS) -nographic -kernel bzImage

run-with-demo-script: bzImage $(DEMO_SCRIPT)
	@echo "[*] Running with demo script: $(DEMO_SCRIPT)"
	$(QEMU) $(QEMU_FLAGS) -nographic -kernel bzImage "$(DEMO_SCRIPT)"

# ZIP initramfs variable
INITRAMFS_ZIP ?= initramfs.zip
INIT_BIN      ?= init

# Build initramfs.zip: packs INIT_BIN as /init inside a ZIP archive.
# QEMU -initrd passes it to the kernel as multiboot mods[0].
# The kernel detects the PK signature and calls zip_load_into_ramfs().
initramfs.zip: $(INIT_BIN)
	@echo "  INITRAMFS $(INITRAMFS_ZIP)"
	@$(PYTHON) scripts/create_initramfs_zip.py $(INIT_BIN) $(INITRAMFS_ZIP)


# Run with ZIP initramfs: QEMU -kernel + -initrd passes the ZIP as mods[0]
# The kernel detects PK magic, calls zip_load_into_ramfs(), finds /init
run-with-zip: bzImage $(INITRAMFS_ZIP)
	@echo "[*] Running with ZIP initramfs: $(INITRAMFS_ZIP)"
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -initrd $(INITRAMFS_ZIP) -vga std

run-with-zip-nographic: bzImage $(INITRAMFS_ZIP)
	@echo "[*] Running with ZIP initramfs (nographic): $(INITRAMFS_ZIP)"
	$(QEMU) $(QEMU_FLAGS) -nographic -kernel bzImage -initrd $(INITRAMFS_ZIP)

# Run with a custom ZIP: make run-with-zip INITRAMFS_ZIP=myfs.zip


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
	@echo "  run-with-zip         QEMU + initramfs.zip (Linux-style, recommended)"
	@echo "  initramfs.zip        Build ZIP initramfs from init.bin"
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
