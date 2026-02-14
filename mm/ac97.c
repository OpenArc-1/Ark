// ac97.c by adnan
#include "../io/built-in.h"
#include "ark/types.h"

// Standard AC'97 registers
#define AC97_RESET        0x00
#define AC97_MASTER_VOL   0x02
#define AC97_PCM_OUT_VOL  0x18
#define AC97_PCM_OUT_CTRL 0x1A

#define AC97_CODEC_CMD    0x80
#define AC97_CODEC_STAT   0x84

// Base I/O port for the AC'97 controller (update after PCI probe)
#define AC97_REG_BASE     0xF000  

// Write to AC'97 codec
static void ac97_write(u16 reg, u16 val) {
    outw(AC97_REG_BASE + AC97_CODEC_CMD, (reg & 0xFF) | (val << 8));
}

// Read from AC'97 codec
static u16 ac97_read(u16 reg) {
    outw(AC97_REG_BASE + AC97_CODEC_CMD, reg);
    // Wait until codec ready (bit 15 clears)
    while (inw(AC97_REG_BASE + AC97_CODEC_STAT) & 0x8000);
    return inw(AC97_REG_BASE + AC97_CODEC_STAT) & 0xFFFF;
}

// Initialize the AC'97 codec
void ac97_init() {
    // Reset the codec
    ac97_write(AC97_RESET, 0x0000);

    // Simple delay for reset to complete
    for (volatile int i = 0; i < 100000; i++);

    // Set Master Volume to ~50%
    ac97_write(AC97_MASTER_VOL, 0x0808);

    // Set PCM Output Volume to ~50%
    ac97_write(AC97_PCM_OUT_VOL, 0x0808);

    // Enable PCM playback
    ac97_write(AC97_PCM_OUT_CTRL, 0x0001);
}

// Optional: play a simple square wave (blocking) for testing
void ac97_test() {
    // This is just a simple hack: write alternating values to PCM volume
    for (int i = 0; i < 50000; i++) {
        ac97_write(AC97_PCM_OUT_VOL, (i & 0xFF) | ((i & 0xFF) << 8));
    }
}
