#include <stdint.h>

#define VGA_WIDTH 25
#define VGA_HEIGHT 80

void clear_screen() {
    for (int i = 0; i < VGA_WIDTH * VGA_HEIGHT; i++) {
        VGA_MEMORY[i * 2] = ' ';
        VGA_MEMORY[i * 2 + 1] = WHITE_ON_BLACK;
    }
    cursor_pos = 0;
}
