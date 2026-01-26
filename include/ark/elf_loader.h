/**
 * ELF Binary Loader Header
 */

#pragma once

#include "ark/types.h"

/**
 * Execute an ELF binary loaded in memory
 * 
 * @param binary Pointer to ELF binary data
 * @param size Size of the binary
 * @return Exit code from the binary
 */
int elf_execute(u8 *binary, u32 size);
