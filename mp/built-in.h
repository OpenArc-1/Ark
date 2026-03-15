#pragma once
#include "ark/types.h"

/*
 * show_sysinfo_bios: reads BIOS data area (only valid in 32-bit real/prot mode).
 * In 64-bit builds this is a no-op stub — BIOS data area is not accessible.
 */
void show_sysinfo_bios(void);
