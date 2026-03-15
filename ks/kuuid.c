#include <stdint.h>

/* --- Global State --- */
static uint32_t g_seed = 0xACE1U; // Default seed

/* --- Hardware Entropy (x86 32-bit) --- */
// Reads the CPU Time Stamp Counter
static inline uint32_t get_entropy(void) {
    uint32_t low;
    // 'rdtsc' puts the low 32 bits in EAX (a) and high 32 bits in EDX (d)
    __asm__ __volatile__ ("rdtsc" : "=a"(low) : : "edx");
    return low;
}

// Seed the generator
void id_init(void) {
    g_seed ^= get_entropy();
}

// Generate a 32-bit random number
static uint32_t next_rand(void) {
    g_seed = (uint32_t)((uint64_t)g_seed * 1103515245 + 12345);
    return g_seed;
}

/* --- String Utilities (Internal) --- */
static int k_strlen(const char* str) {
    int len = 0;
    while (str[len]) len++;
    return len;
}

/* --- The Unique ID Creator --- */
/**
 * Generates an LDM ID in the format: ldmXXXX-XXXXX-XXXXX
 * Buffer must be at least 22 bytes long.
 */
void create_unique_id(char* buffer) {
    const char* prefix = "ldm";
    const char* charset = "abcdefghijklmnopqrstuvwxyz0123456789";
    const char* digits = "0123456789";
    int pos = 0;

    int prefix_len = k_strlen(prefix);
    for (int i = 0; i < prefix_len; i++) {
        buffer[pos++] = prefix[i];
    }

    for (int i = 0; i < 4; i++) {
        buffer[pos++] = digits[next_rand() % 10];
    }

    buffer[pos++] = '-';

    for (int i = 0; i < 5; i++) {
        buffer[pos++] = charset[next_rand() % 36];
    }

    buffer[pos++] = '-';


    for (int i = 0; i < 5; i++) {
        buffer[pos++] = charset[next_rand() % 36];
    }

    buffer[pos] = '\0';
}