# -------------------------------------------------------------------
# Ark OS Build System (x86, 32-bit freestanding)
# Minimal Linux-style output
# -------------------------------------------------------------------

# Tools
ARCH        ?= x86
CC          ?= gcc
LD          ?= gcc
AS          ?= as
NASM        ?= nasm
OBJCOPY     ?= objcopy
QEMU        ?= qemu-system-i386
PYTHON      ?= python3

# Build options
DEBUG       ?= 0
OPT         ?= 2
WERROR      ?= 0
EXTRA_CFLAGS  ?=
EXTRA_LDFLAGS ?=

# Kernel include
KERNEL_INC  ?= include

# Userspace display server
BUILD_INIT       ?= 1
INIT_TEXT        ?= 0x1000
INIT_DATA        ?= 0x2000

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

# Demo script
DEMO_SCRIPT ?= ks/demo.init

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
          $(OPT_CFLAGS) $(WARN_CFLAGS) -I$(KERNEL_INC) $(EXTRA_CFLAGS)

LDFLAGS := -m32 -nostdlib -no-pie $(EXTRA_LDFLAGS)

# QEMU base flags
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
        $(wildcard arch/$(ARCH)/*.c)

NASMSRCS := mp/bios.S
GASSRCS  := $(wildcard arch/$(ARCH)/*.S)
USERSPACE_SRCS := userspace/display_server.c
USERSPACE_ASM  := userspace/display_server_entry.S

OBJS := $(SRCS:.c=.o) $(NASMSRCS:.S=.o) $(GASSRCS:.S=.o)
USERSPACE_OBJS := $(USERSPACE_SRCS:.c=.o) $(USERSPACE_ASM:.S=.o)

# -------------------------------------------------------------------
# Targets
# -------------------------------------------------------------------

.PHONY: all clean run run-disk run-disk-with-fs run-nographic run-with-demo-script

all: bzImage
ifneq ($(BUILD_INIT),0)
all: init.bin
endif

# ------------------------
# Kernel build
# ------------------------
bzImage: linker.ld $(OBJS)
	@printf "  LD      %s\n" $@
	$(CC) $(LDFLAGS) -T linker.ld -o $@ $(OBJS)

# ------------------------
# Userspace init
# ------------------------
init.bin: userspace/linker.ld $(USERSPACE_OBJS)
	@printf "  LD      %s\n" $@
	$(CC) -m32 -nostdlib -nostartfiles \
	      -Ttext=$(INIT_TEXT) -Tdata=$(INIT_DATA) -T userspace/linker.ld \
	      -o $@ $(USERSPACE_OBJS)

# ------------------------
# Compile C files
# Compile C files
%.o: %.c
	@printf "  CC      %s\n" $@
	@$(CC) $(CFLAGS) -c $< -o $@

# NASM assembly
mp/%.o: mp/%.S
	@printf "  AS      %s\n" $@
	@$(NASM) -f elf32 $< -o $@

# GAS assembly
arch/$(ARCH)/%.o: arch/$(ARCH)/%.S
	@printf "  AS      %s\n" $@
	@$(AS) --32 -o $@ $<

userspace/%.o: userspace/%.S
	@printf "  AS      %s\n" $@
	@$(AS) --32 -o $@ $<

# ------------------------
# Clean
# ------------------------
clean:
	rm -f $(OBJS) $(USERSPACE_OBJS) bzImage init.bin \
	      $(DISK_IMAGE) $(DISK_FS_IMAGE) disk-with-init.img
	@echo "[+] Cleaned"

# ------------------------
# Disk images
# ------------------------
disk.img: bzImage
	@$(PYTHON) scripts/create_bootable_image.py bzImage $(DISK_IMAGE) $(DISK_SIZE_MB)

disk-with-fs.img: bzImage init.bin
	@$(PYTHON) scripts/create_disk_with_init.py bzImage init.bin $(DISK_FS_IMAGE) $(DISK_SIZE_MB)

# ------------------------
# QEMU targets
# ------------------------
run: bzImage
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage

run-disk: bzImage $(DISK_IMAGE)
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -drive file=$(DISK_IMAGE),format=raw

run-disk-with-fs: bzImage $(DISK_FS_IMAGE)
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage -drive file=$(DISK_FS_IMAGE),format=raw -initrd init.bin

run-nographic: bzImage init.bin
	$(QEMU) $(QEMU_FLAGS) -nographic -kernel bzImage -initrd init.bin

run-with-demo-script: bzImage init.bin $(DEMO_SCRIPT)
	@echo "[*] Running with demo script: $(DEMO_SCRIPT)"
	$(QEMU) $(QEMU_FLAGS) -nographic -kernel bzImage -initrd "$(DEMO_SCRIPT),init.bin"


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