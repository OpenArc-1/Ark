# Makefile for ks/dkmload - Dynamic Kernel Module (DKM) loader
#
# This makefile builds:
#   - sample.elf: standalone example DKM
#   - strapper: userspace module loader utility
#   - Custom modules following the pattern shown
#
# Usage:
#   make sample.elf        # Build sample module
#   make strapper          # Build strapper utility
#   make clean             # Clean all built files

ARCH ?= x86
TOP := ../../
INCLUDE := -I$(TOP)include

# ark-gcc compiler (must be in PATH or use full path)
AR_GCC ?= ark-gcc

# Compiler flags for standalone freestanding modules
CFLAGS := -ffreestanding -nostdlib -fno-builtin
CFLAGS += -Wall -Wextra -std=c99
CFLAGS += $(INCLUDE)

# Linker flags
LDFLAGS := -ffreestanding -nostdlib

# Entry point for modules
ENTRY := module_init

.PHONY: all clean help

all: sample.elf strapper

help:
	@echo "DKM (Dynamic Kernel Module) Makefile"
	@echo "Available targets:"
	@echo "  make sample.elf      - Build sample minimal module"
	@echo "  make strapper        - Build strapper loader utility"
	@echo "  make clean           - Remove all build artifacts"
	@echo ""
	@echo "To build custom modules:"
	@echo "  $(AR_GCC) -ffreestanding -nostdlib -Wl,-e,module_init -o mymodule.elf mymodule.c"
	@echo ""
	@echo "To load a module at runtime:"
	@echo "  strapper load /path/to/module.elf"

# Sample module - standalone, no kernel API required
sample.elf: sample_module.c
	$(AR_GCC) $(CFLAGS) $(LDFLAGS) -Wl,-e,$(ENTRY) \
		-o $@ $<

# Strapper utility - userspace loader
strapper: strapper.c
	$(AR_GCC) $(CFLAGS) $(LDFLAGS) \
		-o $@ $<

clean:
	rm -f sample.elf strapper *.o

.PHONY: install
install: sample.elf strapper
	@echo "Install targets to initramfs:"
	@echo "  cp sample.elf /path/to/initramfs/"
	@echo "  cp strapper /path/to/initramfs/bin/"
