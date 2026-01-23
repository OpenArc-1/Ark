#include <stdint.h>

#define VGA_WIDTH 25
#define VGA_HEIGHT 80
#define WHITE_ON_BLACK 0x0F
static volatile uint8_t* const VGA_MEMORY = (volatile uint8_t*)0xB8000;


void clear_screen() {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_MEMORY[i * 2] = ' ';
        VGA_MEMORY[i * 2 + 1] = WHITE_ON_BLACK;
    }
}
