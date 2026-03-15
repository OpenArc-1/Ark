# =============================================================================
# Ark Kernel — Build System
# =============================================================================
#
#  make              Build kernel image
#  make kernel       Same as default
#  make menuconfig   Interactive configuration (ncurses)
#  make defconfig    Apply recommended defaults
#  make tinyconfig   Minimal footprint preset
#  make allyes       Enable everything
#  make allno        Bare minimum preset
#  make clean        Remove all build artefacts
#  make info         Print current configuration summary
#
# GCC 15 Compatibility Notes:
#   - Updated from -std=gnu99 to -std=gnu11 (gnu23 is the new GCC 15 default)
#   - Removed --data-sections from LDFLAGS (compiler flag, not linker flag)
#   - Added -Wno-implicit-function-declaration / -Wno-incompatible-pointer-types
#     to handle stricter GCC 15 defaults for freestanding kernel code
#   - stdbool.h updated for C23 where bool/true/false are language keywords
#
# Variables (all overridable on the command line):
#   ARCH=x86        x86 | x86_64
#   CC=gcc          C compiler
#   ACC=ark-gcc     Compiler for DKM
#   DEBUG=0         1 → -O0 -g
#   OPT=2           GCC optimisation level: 0 1 2 s
#   WERROR=0        1 → -Werror
# =============================================================================

CODE_NAME  ?= affectionate-cat
BUILD_DATE ?= $(shell date '+%Y-%m-%d')

# -----------------------------------------------------------------------------
# Architecture
# -----------------------------------------------------------------------------
ARCH ?= x86_64

ifeq ($(ARCH),x86_64)
  BITS    := 64
  MACHINE := x86_64
else
  BITS    := 32
  MACHINE := i686
endif

# -----------------------------------------------------------------------------
# Toolchain
# -----------------------------------------------------------------------------
CC      ?= gcc
AS      ?= as
NASM    ?= nasm
OBJCOPY ?= objcopy
PYTHON  ?= python3
HOSTCC  ?= gcc
ACC     ?= ark-gcc

-include .kconfig

# -----------------------------------------------------------------------------
# Build options
# -----------------------------------------------------------------------------
DEBUG  ?= 0
OPT    ?= 2
WERROR ?= 0

EXTRA_CFLAGS  ?=
EXTRA_LDFLAGS ?=
KERNEL_INC    ?= include

# -----------------------------------------------------------------------------
# Output paths
# -----------------------------------------------------------------------------
OBJDIR   := compiled
IMG_DIR  := $(OBJDIR)/$(ARCH)/compressed
ARKIMAGE := $(IMG_DIR)/ArkImage

# -----------------------------------------------------------------------------
# Compiler flags
# -----------------------------------------------------------------------------
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

WARN_CFLAGS := -w
ifeq ($(WERROR),1)
  WARN_CFLAGS += -Werror
endif

ifeq ($(ARCH),x86_64)
  MARCH_FLAG    := -m64
  ARCH_FLAG     := --64
  LINKER_SCRIPT := arch/x86_64/linker64.ld
  ARCH_DEFINES  := -DARK_BITS=64 -D_ARK_KERNEL
else
  MARCH_FLAG    := -m32
  ARCH_FLAG     := --32
  LINKER_SCRIPT := arch/x86/linker.ld
  ARCH_DEFINES  := -DARK_BITS=32 -D_ARK_KERNEL
endif

CFLAGS := $(MARCH_FLAG) $(ARCH_DEFINES)           \
          -fno-pic -fno-pie -ffreestanding         \
          -fno-stack-protector                     \
          -std=gnu11                               \
          -ffunction-sections -fdata-sections      \
          -fomit-frame-pointer                     \
          $(OPT_CFLAGS) $(WARN_CFLAGS)            \
          -I$(KERNEL_INC)                          \
          -Wno-implicit-function-declaration       \
          -Wno-incompatible-pointer-types          \
          -Wno-int-conversion                      \
          $(EXTRA_CFLAGS)

# NOTE: --data-sections is a compiler flag (-fdata-sections), NOT a linker flag.
# Removed from LDFLAGS to fix GCC 15 compatibility (ld rejects unknown flags).
LDFLAGS := $(MARCH_FLAG) -nostdlib -nostartfiles -no-pie \
           $(EXTRA_LDFLAGS)

# -----------------------------------------------------------------------------
# Kconfig
# -----------------------------------------------------------------------------
NCURSES_LIB    ?= -lncurses
KCONFIG_TOOL   := kconfig/menuconfig
KCONFIG_IN     := .kconfig
KCONFIG_HEADER := include/ark/kconfig.h

$(KCONFIG_TOOL): kconfig/menuconfig.c
	@printf "  HOSTCC  %s\n" $@
	@$(HOSTCC) -O2 -o $@ $< $(NCURSES_LIB)

menuconfig: $(KCONFIG_TOOL)
	@$(KCONFIG_TOOL)
	@$(PYTHON) scripts/gen_kconfig_h.py $(KCONFIG_IN) $(KCONFIG_HEADER)

defconfig:
	@printf "  PRESET  defconfig\n"
	@$(PYTHON) scripts/apply_preset.py defconfig $(KCONFIG_IN)
	@$(PYTHON) scripts/gen_kconfig_h.py $(KCONFIG_IN) $(KCONFIG_HEADER)

tinyconfig:
	@printf "  PRESET  tinyconfig\n"
	@$(PYTHON) scripts/apply_preset.py tinyconfig $(KCONFIG_IN)
	@$(PYTHON) scripts/gen_kconfig_h.py $(KCONFIG_IN) $(KCONFIG_HEADER)

allyes:
	@printf "  PRESET  allyes\n"
	@$(PYTHON) scripts/apply_preset.py allyes $(KCONFIG_IN)
	@$(PYTHON) scripts/gen_kconfig_h.py $(KCONFIG_IN) $(KCONFIG_HEADER)

allno:
	@printf "  PRESET  allno\n"
	@$(PYTHON) scripts/apply_preset.py allno $(KCONFIG_IN)
	@$(PYTHON) scripts/gen_kconfig_h.py $(KCONFIG_IN) $(KCONFIG_HEADER)

$(KCONFIG_HEADER): scripts/gen_kconfig_h.py
	@if [ ! -f $(KCONFIG_IN) ]; then \
	    printf "  CONFIG  .kconfig missing — applying defconfig\n"; \
	    $(PYTHON) scripts/apply_preset.py defconfig $(KCONFIG_IN); \
	fi
	@printf "  GEN     %s\n" $@
	@$(PYTHON) scripts/gen_kconfig_h.py $(KCONFIG_IN) $(KCONFIG_HEADER)

# -----------------------------------------------------------------------------
# Source selection
# -----------------------------------------------------------------------------
_kflag = $(if $(filter 0,$($(1))),0,1)

# ── Core ─────────────────────────────────────────────────────────────────────
SRCS_CORE := \
  $(wildcard gen/*.c)           \
  $(wildcard arch/$(ARCH)/*.c)  \
  $(wildcard arch/mm/*.c)       \
  io/built-in.c                 \
  arch/x86/sysinfo.c arch/x86/time.c

ifeq ($(ARCH),x86_64)
  NASMSRCS :=
else
  NASMSRCS := arch/x86/bios.S
endif
GASSRCS := $(filter-out arch/x86/bios.S, $(wildcard arch/$(ARCH)/*.S))

# ── Framebuffer ───────────────────────────────────────────────────────────────
ifeq ($(call _kflag,FB_ENABLE),1)
  SRCS_CORE += $(wildcard drivers/fb/*.c)
endif

# ── GPU ───────────────────────────────────────────────────────────────────────
SRCS_GPU :=
ifeq ($(call _kflag,GPU_ENABLE),1)
  SRCS_GPU += drivers/gpu/vesa.c drivers/gpu/efi_gop.c
endif

# ── Storage ───────────────────────────────────────────────────────────────────
SRCS_FS := fs/modules.c fs/disk_init.c fs/built-in.c
ifeq ($(call _kflag,ATA_ENABLE),1)
  SRCS_FS += fs/ata.c
endif
ifeq ($(call _kflag,SATA_ENABLE),1)
  SRCS_FS += fs/sata.c
endif
ifeq ($(call _kflag,SD_ENABLE),1)
  SRCS_FS += fs/sd-dev.c
endif
ifeq ($(call _kflag,FAT32_ENABLE),1)
  SRCS_FS += fs/fat32.c
endif
ifeq ($(call _kflag,RAMFS_ENABLE),1)
  SRCS_FS += fs/ramfs.c
endif
ifeq ($(call _kflag,VFS_ENABLE),1)
  SRCS_FS += fs/vfs.c
endif
ifeq ($(call _kflag,AFS_ENABLE),1)
  SRCS_FS += fs/afs.c
endif
ifeq ($(call _kflag,ZIP_ENABLE),1)
  SRCS_FS += fs/zip.c fs/tinflate.c
endif

# ── Networking ────────────────────────────────────────────────────────────────
SRCS_NET :=
ifeq ($(call _kflag,NET_ENABLE),1)
  SRCS_NET += drivers/wf/net.c drivers/wf/loopback.c drivers/wf/eth-dev.c
  SRCS_NET += drivers/wf/e1000.c drivers/wf/rtl8139.c
  SRCS_NET += drivers/wf/virtio_net.c drivers/wf/pcnet.c
  ifeq ($(call _kflag,E100_ENABLE),1)
    SRCS_NET += drivers/wf/e100.c
  endif
  ifeq ($(call _kflag,IP_ENABLE),1)
    SRCS_NET += drivers/wf/ip.c
  endif
endif

# ── USB ───────────────────────────────────────────────────────────────────────
SRCS_USB :=

ifeq ($(call _kflag,USB_ENABLE),1)
  SRCS_USB += drivers/usb/usb.c drivers/usb/usb_hub.c drivers/usb/usb_msd.c
  ifeq ($(call _kflag,USB_XHCI),1)
    SRCS_USB += drivers/usb/xhci.c
  endif
  ifeq ($(call _kflag,USB_EHCI),1)
    SRCS_USB += drivers/usb/ehci.c
  endif
  ifeq ($(call _kflag,USB_UHCI),1)
    SRCS_USB += drivers/usb/uhci.c drivers/usb/ohci.c
  endif
  ifeq ($(call _kflag,USB_HID),1)
    SRCS_USB += drivers/usb/hid.c drivers/usb/usb_hid_mouse.c
    SRCS_USB += drivers/hid/usb_kbd.c
  endif
endif

# Rust USB driver (conditional)
# Gated by RUST_ENABLE=1 in .kconfig (set via menuconfig Language Runtimes).
# Legacy USB_KBD_RUST=1 is also accepted for backwards compatibility.
RUST_OBJS :=
RUST_SRC := rust/src
RUST_OBJ :=

_RUST_ON := $(or $(call _kflag,RUST_ENABLE),$(call _kflag,USB_KBD_RUST))
ifeq ($(_RUST_ON),1)
ifeq ($(ARCH),x86_64)
# Rust USB keyboard driver — x86_64 only (ELF64 object, incompatible with i386)
$(OBJDIR)/rust/rust_usb_kbd.o: $(RUST_SRC)/lib.rs $(wildcard $(RUST_SRC)/*.rs) $(wildcard $(RUST_SRC)/*/*.rs)
	@mkdir -p $(dir $@)
	@echo "  RUST    $@"
	@. $(HOME)/.cargo/env && cd rust && rustc --edition 2021 --crate-type=staticlib \
		--target x86_64-unknown-linux-gnu \
		-o ../$@ src/lib.rs 2>&1 | grep -v "^warning:" || true

RUST_OBJ := $(OBJDIR)/rust/rust_usb_kbd.o
else
# i386 build: Rust driver is ELF64-only.
# Fall back to the C USB keyboard driver (drivers/hid/usb_kbd.c).
$(warning RUST_ENABLE is not supported on ARCH=x86 (ELF64 object cannot link into i386 kernel))
$(warning Falling back to C USB keyboard driver (drivers/hid/usb_kbd.c))
RUST_OBJ :=
endif
endif

# -----------------------------------------------------------------------------
# Zig kernel support (conditional)
# -----------------------------------------------------------------------------
ZIG := $(shell which zig 2>/dev/null)
ZIG_SRC := zig/src
ZIG_OBJ :=
ZIG_ENABLED := 0

ifeq ($(call _kflag,ZIG_ENABLE),1)
ifeq ($(strip $(ZIG)),)
$(warning Zig compiler not found - install zig or disable ZIG_ENABLE)
else
ZIG_ENABLED := 1

# Select freestanding target based on ARCH
# Use freestanding ABI — the Zig code has no OS or libc dependencies.
ifeq ($(ARCH),x86_64)
ZIG_TARGET := x86_64-freestanding-none
else
ZIG_TARGET := x86-freestanding-none
endif

# Compile Zig library to a relocatable ELF object ready for ld.
# zig build-lib with -ofmt=elf writes <name>.o directly (no .a wrapper).
$(OBJDIR)/zig/ark_zig.o: $(ZIG_SRC)/lib.zig $(wildcard $(ZIG_SRC)/*.zig)
	@mkdir -p $(dir $@)
	@echo "  ZIG     $@"
	$(ZIG) build-lib \
		-target $(ZIG_TARGET) \
		-ofmt=elf \
		-fno-compiler-rt \
		-O ReleaseSafe \
		--name ark_zig \
		-femit-bin=$@ \
		$(ZIG_SRC)/lib.zig

ZIG_OBJ := $(OBJDIR)/zig/ark_zig.o
endif
endif

# ── HID / PS2 ─────────────────────────────────────────────────────────────────
SRCS_HID :=
ifeq ($(call _kflag,KBD_ENABLE),1)
  SRCS_HID += drivers/hid/kbd100.c
endif
ifeq ($(call _kflag,MOUSE_ENABLE),1)
  SRCS_HID += drivers/hid/touch.c
endif

# ── Audio ─────────────────────────────────────────────────────────────────────
SRCS_AUD :=
ifeq ($(call _kflag,AUDIO_ENABLE),1)
  SRCS_AUD += drivers/aud/aud-dev.c
  ifeq ($(call _kflag,AC97_ENABLE),1)
    SRCS_AUD += drivers/aud/ac97.c
  endif
endif

# ── Kernel scripts / DKM ──────────────────────────────────────────────────────
SRCS_KS := ks/script.c ks/kuuid.c \
  $(filter-out ks/dkmload/strapper.c ks/dkmload/sample_module.c, \
               $(wildcard ks/dkmload/*.c))

# ── Combine ───────────────────────────────────────────────────────────────────
SRCS := $(sort \
  $(SRCS_CORE) \
  $(SRCS_GPU)  \
  $(SRCS_FS)   \
  $(SRCS_NET)  \
  $(SRCS_USB)  \
  $(SRCS_HID)  \
  $(SRCS_AUD)  \
  $(SRCS_KS))

OBJS := $(patsubst %, $(OBJDIR)/%, \
          $(SRCS:.c=.o)            \
          $(NASMSRCS:.S=.o)        \
          $(GASSRCS:.S=.o))

# Add Rust object (already compiled to rust/rust_usb_kbd.o)
OBJS += $(RUST_OBJ)

# Add Zig objects
ifeq ($(ZIG_ENABLED),1)
OBJS += $(ZIG_OBJ)
endif

# -----------------------------------------------------------------------------
# Phony targets
# -----------------------------------------------------------------------------
.PHONY: all kernel clean info \
        menuconfig defconfig tinyconfig allyes allno

all: kernel

# -----------------------------------------------------------------------------
# Kernel
# -----------------------------------------------------------------------------
kernel: $(KCONFIG_HEADER) $(ARKIMAGE)

$(ARKIMAGE): $(LINKER_SCRIPT) $(OBJS)
	@mkdir -p $(IMG_DIR)
	@printf "  LD      %s\n" $@
	@$(CC) $(LDFLAGS) -T $(LINKER_SCRIPT) -o $@ $(OBJS)
	@printf "  OK      $(ARKIMAGE)  (%s bytes)\n" $$(wc -c < $(ARKIMAGE))

# -----------------------------------------------------------------------------
# Compile rules
# -----------------------------------------------------------------------------
$(OBJDIR)/%.o: %.c $(KCONFIG_HEADER)
	@mkdir -p $(dir $@)
	@printf "  CC      %s\n" $<
	@$(CC) $(CFLAGS) -c $< -o $@

NASM_FORMAT := $(if $(filter x86_64,$(ARCH)),elf64,elf32)
$(OBJDIR)/arch/x86/bios.o: arch/x86/bios.S
	@mkdir -p $(dir $@)
	@printf "  AS      %s\n" $<
	@$(NASM) -f $(NASM_FORMAT) $< -o $@

GAS_BITS := $(if $(filter x86_64,$(ARCH)),--64,--32)
$(OBJDIR)/arch/$(ARCH)/%.o: arch/$(ARCH)/%.S
	@mkdir -p $(dir $@)
	@printf "  AS      %s\n" $<
	@$(AS) $(GAS_BITS) -o $@ $<

# -----------------------------------------------------------------------------
# Clean
# -----------------------------------------------------------------------------
clean:
	@rm -rf $(OBJDIR) iso_root
	@printf "  CLEAN   done\n"

# -----------------------------------------------------------------------------
# Info
# -----------------------------------------------------------------------------
info:
	@printf "\nArk kernel build configuration\n"
	@printf "  Code name  : %s\n"          "$(CODE_NAME)"
	@printf "  Arch       : %s (%s-bit)\n" "$(ARCH)" "$(BITS)"
	@printf "  Kernel out : %s\n"          "$(ARKIMAGE)"
	@printf "  CC         : %s\n"          "$(CC)"
	@printf "  ACC        : %s\n"          "$(ACC)"
	@printf "  Debug      : %s  Opt: -O%s  Werror: %s\n" \
	        "$(DEBUG)" "$(OPT)" "$(WERROR)"
	@printf "  USB        : $(call _kflag,USB_ENABLE)\n"
	@printf "  Net        : $(call _kflag,NET_ENABLE)\n"
	@printf "  Audio      : $(call _kflag,AUDIO_ENABLE)\n"
	@printf "  GPU        : $(call _kflag,GPU_ENABLE)\n"
	@printf "  CFLAGS     : %s\n"          "$(CFLAGS)"
	@printf "\n"
