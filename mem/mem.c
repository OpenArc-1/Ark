// mem.c
#include <stdint.h>
#include <stddef.h>

void *memcpy(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t*)dest;
    const uint8_t *s = (const uint8_t*)src;

    for (size_t i = 0; i < n; i++)
        d[i] = s[i];

    return dest;
}

void *memset(void *dest, int val, size_t n) {
    uint8_t *d = (uint8_t*)dest;

    for (size_t i = 0; i < n; i++)
        d[i] = (uint8_t)val;

    return dest;
}

void *memmove(void *dest, const void *src, size_t n) {
    uint8_t *d = (uint8_t*)dest;
    const uint8_t *s = (const uint8_t*)src;

    if (d < s) {
        for (size_t i = 0; i < n; i++)
            d[i] = s[i];
    } else {
        for (size_t i = n; i != 0; i--)
            d[i-1] = s[i-1];
    }

    return dest;
}

int memcmp(const void *a, const void *b, size_t n) {
    const uint8_t *x = (const uint8_t*)a;
    const uint8_t *y = (const uint8_t*)b;

    for (size_t i = 0; i < n; i++) {
        if (x[i] != y[i])
            return x[i] - y[i];
    }
    return 0;
}
