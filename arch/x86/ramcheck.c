#include <stdint.h>
#include "ark/printk.h"
#include "ark/panic.h"
// Define where 16MB ends in physical memory
#define SIXTEENTH_MB 0x1000000

// You should define this in your kernel headers
extern void kernel_panic(const char* msg);

/**
 * A destructive memory test: 
 * It writes a pattern to the last byte of the 16th MB and reads it back.
 * NOTE: Only use this during very early boot before you have a memory manager!
 */
int check_minimum_ram() {
    // We check the very last 4 bytes of the 16MB boundary
    volatile uint32_t* mem_to_test = (volatile uint32_t*)(SIXTEENTH_MB - 4);
    
    // Save original value to be polite (though we are likely about to panic)
    uint32_t old_val = *mem_to_test;
    
    // Write a "magic" pattern
    *mem_to_test = 0xDEADBEEF;
    
    // Check if it stuck
    if (*mem_to_test == 0xDEADBEEF) {
        *mem_to_test = old_val; // Restore original
        return 1; // 16MB is present
    }
    
    return 0; // RAM doesn't exist at this address
}

void mem_verify() {
    printk(T,"Checking for minimum 16MB RAM...\n");

    if (!check_minimum_ram()) {
        printk(T,"ERROR: Less than 16MB of RAM detected.\n");
        kernel_panic("Insufficient amount of ram please run with more ram\n");
    }

    printk(T,"RAM Check Passed. Proceeding...\n");
}
