/**
 * ELF Binary Loader Header
 */

#pragma once

#include "ark/types.h"
 #include "ark/init_api.h"

/**
 * Execute an ELF binary loaded in memory.
 *
 * Ark's current model jumps to the ELF entrypoint directly.
 * For init.bin, the entrypoint is expected to match `ark_init_entry_t`
 * and will receive a pointer to a versioned kernel API table.
 *
 * @param binary Pointer to ELF binary data
 * @param size Size of the binary
 * @param api Kernel API table pointer (may be NULL for legacy/raw runs)
 * @return Exit code from the binary (0 if none)
 */
int elf_execute(u8 *binary, u32 size, const ark_kernel_api_t *api);
