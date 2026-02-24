/**
 * Kernel panic handling.
 */

#pragma once

#include "ark/types.h"

/**
 * Print a panic message and halt the CPU.
 *
 * This should never return.
 */
void kernel_panic(const char *msg);

