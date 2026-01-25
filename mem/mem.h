// mem.h
#ifndef MEM_H
#define MEM_H

#include <stddef.h>
#include <stdint.h>

void *memcpy(void *dest, const void *src, size_t n);
void *memset(void *dest, int val, size_t n);
void *memmove(void *dest, const void *src, size_t n);
int   memcmp(const void *a, const void *b, size_t n);

#endif
