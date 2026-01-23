#pragma once
# include "types.h"



// reading a 38 bit PCI registery to find the device ;
u32 pciread(u8 bus , u8 slot , u8 func , u8 offset);
// scaning  looping in PCI
void scanAll(void);