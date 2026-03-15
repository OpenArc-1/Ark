// mem.h
#ifndef MEM_H
#define MEM_H


#include "ark/types.h"

void *memcpy(void *dest, const void *src, u32 n);
void *memset(void *dest, int val, u32 n);
void *memmove(void *dest, const void *src, u32 n);
int   memcmp(const void *a, const void *b, u32 n);

#endif
