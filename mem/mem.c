/**
 * mem/mem.c - Architecture-independent memory utilities for Ark kernel
 * Works for both 32-bit and 64-bit builds.
 */
#include "ark/types.h"

void *memcpy(void *dest, const void *src, u32 n) {
    u8 *d = (u8 *)dest;
    const u8 *s = (const u8 *)src;
    for (u32 i = 0; i < n; i++) d[i] = s[i];
    return dest;
}

void *memset(void *dest, int val, u32 n) {
    u8 *d = (u8 *)dest;
    for (u32 i = 0; i < n; i++) d[i] = (u8)val;
    return dest;
}

void *memmove(void *dest, const void *src, u32 n) {
    u8 *d = (u8 *)dest;
    const u8 *s = (const u8 *)src;
    if (d < s)
        for (u32 i = 0; i < n; i++) d[i] = s[i];
    else
        for (u32 i = n; i > 0; i--) d[i-1] = s[i-1];
    return dest;
}

int memcmp(const void *a, const void *b, u32 n) {
    const u8 *x = (const u8 *)a;
    const u8 *y = (const u8 *)b;
    for (u32 i = 0; i < n; i++)
        if (x[i] != y[i]) return (int)x[i] - (int)y[i];
    return 0;
}
