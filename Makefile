#ARK BUILD ID !93
MOD = 12
KV = 1.0
ARCH        ?= x86
# Build with the host GCC as a 32‑bit freestanding compiler.
CC          ?= gcc
# IMPORTANT: force using gcc as the linker driver.
# Some environments export LD=ld, which breaks -m32 linking.
LD          := $(CC)
OBJCOPY     ?= objcopy

CFLAGS      := -m32 -fno-pic -fno-pie -std=gnu99 -ffreestanding -O2 -Wall -Wextra -Iinclude
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
    $(wildcard mp/*.c) \
    $(wildcard wf/*.c) \
    $(wildcard arch/$(ARCH)/*.c)

OBJS := $(SRCS:.c=.o)

all: bzImage

bzImage: linker.ld $(OBJS)
	$(LD) $(LDFLAGS) -T linker.ld -o $@ $(OBJS)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

clean:
	rm -f $(OBJS) bzImage
	rm -f kconfig/menuconfig

run: bzImage
	$(QEMU) $(QEMU_FLAGS) -kernel bzImage

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
	  fi; \
	done

help:
	@echo "Useable commands to build debug the kernel"
	@echo "  make         - build the kernel image (bzImage)"
	@echo "  make run     - run the kernel in QEMU"
	@echo "  make menuconfig - build the menuconfig tool"
	@echo "  make clean   - clean all build artifacts"
	@echo "  make list    - list build tools"
	@echo "  make help    - show this help message"
.PHONY: all clean run list


