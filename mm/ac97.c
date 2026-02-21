// ac97.c - Complete AC'97 Audio Driver with DMA
// by adnan (enhanced version)

#include "../io/built-in.h"
#include "ark/types.h"

// ============================================================================
// AC'97 Register Definitions
// ============================================================================

// Native Audio Mixer (NAM) - Codec Registers
#define AC97_RESET              0x00
#define AC97_MASTER_VOL         0x02
#define AC97_HEADPHONE_VOL      0x04
#define AC97_MASTER_MONO_VOL    0x06
#define AC97_PCM_OUT_VOL        0x18
#define AC97_EXT_AUDIO_ID       0x28
#define AC97_EXT_AUDIO_CTRL     0x2A
#define AC97_PCM_FRONT_DAC_RATE 0x2C

// Native Bus Mastering (NABM) - Bus Master Registers
// PCM Out (PO) channel offsets
#define AC97_PO_BDBAR           0x10  // Buffer Descriptor List Base Address
#define AC97_PO_CIV             0x14  // Current Index Value
#define AC97_PO_LVI             0x15  // Last Valid Index
#define AC97_PO_SR              0x16  // Status Register
#define AC97_PO_PICB            0x18  // Position In Current Buffer
#define AC97_PO_PIV             0x1A  // Prefetched Index Value
#define AC97_PO_CR              0x1B  // Control Register

// Control Register bits
#define AC97_CR_RPBM            (1 << 0)  // Run/Pause Bus Master
#define AC97_CR_RR              (1 << 1)  // Reset Registers
#define AC97_CR_LVBIE           (1 << 2)  // Last Valid Buffer Interrupt Enable
#define AC97_CR_FEIE            (1 << 3)  // FIFO Error Interrupt Enable
#define AC97_CR_IOCE            (1 << 4)  // Interrupt On Completion Enable

// Status Register bits
#define AC97_SR_DCH             (1 << 0)  // DMA Controller Halted
#define AC97_SR_CELV            (1 << 1)  // Current Equals Last Valid
#define AC97_SR_LVBCI           (1 << 2)  // Last Valid Buffer Completion Interrupt
#define AC97_SR_BCIS            (1 << 3)  // Buffer Completion Interrupt Status
#define AC97_SR_FIFOE           (1 << 4)  // FIFO Error

// Buffer Descriptor flags
#define AC97_BD_IOC             (1 << 15) // Interrupt on Completion
#define AC97_BD_BUP             (1 << 14) // Buffer Underrun Policy

// ============================================================================
// Global Variables
// ============================================================================

static u16 nam_base = 0;   // NAM base address (from BAR0)
static u16 nabm_base = 0;  // NABM base address (from BAR1)

// Buffer Descriptor List (32 entries max)
typedef struct {
    u32 buffer_addr;    // Physical address of audio buffer
    u16 length;         // Length in SAMPLES (stereo: 2 samples = 1 frame)
    u16 flags;          // IOC and BUP flags
} __attribute__((packed)) ac97_bd_t;

#define BDL_ENTRIES 32
static ac97_bd_t bdl[BDL_ENTRIES] __attribute__((aligned(8)));

// Audio buffers (16-bit stereo samples)
#define BUFFER_SIZE 4096  // In samples (not bytes)
static u16 audio_buffers[BDL_ENTRIES][BUFFER_SIZE] __attribute__((aligned(4096)));

// ============================================================================
// Low-level I/O Functions
// ============================================================================

// Write to NAM (codec) register
static void nam_write(u8 reg, u16 val) {
    outw(nam_base + reg, val);
}

// Read from NAM (codec) register
static u16 nam_read(u8 reg) {
    return inw(nam_base + reg);
}

// Write to NABM (bus master) register - 8-bit
static void nabm_write8(u16 reg, u8 val) {
    outb(nabm_base + reg, val);
}

// Write to NABM (bus master) register - 16-bit
static void nabm_write16(u16 reg, u16 val) {
    outw(nabm_base + reg, val);
}

// Write to NABM (bus master) register - 32-bit
static void nabm_write32(u16 reg, u32 val) {
    outl(nabm_base + reg, val);
}

// Read from NABM (bus master) register - 8-bit
static u8 nabm_read8(u16 reg) {
    return inb(nabm_base + reg);
}

// Read from NABM (bus master) register - 16-bit
static u16 nabm_read16(u16 reg) {
    return inw(nabm_base + reg);
}

// ============================================================================
// PCI Configuration (Simplified)
// ============================================================================

// You should implement proper PCI scanning, but for now we'll use common defaults
// Intel ICH AC'97 is typically at:
// - Vendor ID: 0x8086 (Intel)
// - Device ID: 0x2415, 0x2425, 0x2445, etc.
// - BAR0: NAM base (I/O)
// - BAR1: NABM base (I/O)

void ac97_detect_pci() {
    // EXAMPLE VALUES - replace with actual PCI scanning!
    // These are typical for Intel ICH4/ICH5
    nam_base = 0xD800;   // Common default, scan PCI BAR0 for real value
    nabm_base = 0xDC00;  // Common default, scan PCI BAR1 for real value
    
    // TODO: Implement proper PCI enumeration
    // 1. Scan PCI bus for AC'97 controller (class 0x04, subclass 0x01)
    // 2. Read BAR0 and BAR1
    // 3. Enable bus mastering and I/O space in PCI command register
}

// ============================================================================
// AC'97 Initialization
// ============================================================================

void ac97_init() {
    // Detect PCI device and get base addresses
    ac97_detect_pci();
    
    // Reset bus master
    nabm_write8(AC97_PO_CR, AC97_CR_RR);
    for (volatile int i = 0; i < 1000; i++);  // Short delay
    nabm_write8(AC97_PO_CR, 0);
    
    // Reset codec
    nam_write(AC97_RESET, 0x0000);
    
    // Wait for codec ready (check if we can read/write)
    for (volatile int i = 0; i < 100000; i++);
    
    // Check if Variable Rate Audio is supported
    u16 ext_id = nam_read(AC97_EXT_AUDIO_ID);
    if (ext_id & 0x0001) {
        // Enable Variable Rate Audio
        nam_write(AC97_EXT_AUDIO_CTRL, 0x0001);
        
        // Set sample rate to 48000 Hz
        nam_write(AC97_PCM_FRONT_DAC_RATE, 48000);
    }
    
    // Set volumes (0x0000 = max, 0x8000 = muted)
    // Format: bit 15 = mute, bits 0-4 = attenuation
    nam_write(AC97_MASTER_VOL, 0x0000);      // Max volume
    nam_write(AC97_PCM_OUT_VOL, 0x0808);     // -12dB (reasonable level)
    
    // Clear status register
    nabm_write16(AC97_PO_SR, 0x1E);  // Clear all status bits
}

// ============================================================================
// DMA Setup
// ============================================================================

void ac97_setup_dma() {
    // Stop any ongoing DMA
    nabm_write8(AC97_PO_CR, 0);
    
    // Reset bus master
    nabm_write8(AC97_PO_CR, AC97_CR_RR);
    for (volatile int i = 0; i < 1000; i++);
    nabm_write8(AC97_PO_CR, 0);
    
    // Set up Buffer Descriptor List
    // We'll use just 2 buffers for ping-pong playback
    for (int i = 0; i < 2; i++) {
        bdl[i].buffer_addr = (u32)&audio_buffers[i][0];  // Physical address
        bdl[i].length = BUFFER_SIZE / 2;  // In stereo samples (frames)
        bdl[i].flags = AC97_BD_IOC;  // Interrupt on completion
    }
    
    // Write BDL base address to controller
    nabm_write32(AC97_PO_BDBAR, (u32)&bdl[0]);
    
    // Set Last Valid Index
    nabm_write8(AC97_PO_LVI, 1);  // We're using entries 0 and 1
}

// ============================================================================
// Audio Playback Functions
// ============================================================================

void ac97_start() {
    // Clear status bits
    nabm_write16(AC97_PO_SR, 0x1E);
    
    // Start DMA: Run + IOC interrupt enabled
    nabm_write8(AC97_PO_CR, AC97_CR_RPBM | AC97_CR_IOCE);
}

void ac97_stop() {
    nabm_write8(AC97_PO_CR, 0);
}

// ============================================================================
// Audio Generation (Test Functions)
// ============================================================================

// Generate a sine wave tone
// freq: frequency in Hz
// duration_samples: how many samples to generate
void generate_sine_wave(u16* buffer, int samples, int freq, int sample_rate) {
    for (int i = 0; i < samples; i += 2) {  // Stereo: 2 samples per frame
        // Simple sine wave using integer math
        // sin approximation: we'll use a lookup or simple calculation
        int t = (i / 2) * freq * 360 / sample_rate;
        t = t % 360;
        
        // Very rough sine approximation (replace with proper lookup table)
        int sine_val;
        if (t < 90) {
            sine_val = (t * 32767) / 90;
        } else if (t < 180) {
            sine_val = ((180 - t) * 32767) / 90;
        } else if (t < 270) {
            sine_val = -((t - 180) * 32767) / 90;
        } else {
            sine_val = -((360 - t) * 32767) / 90;
        }
        
        // Apply to both channels (stereo)
        buffer[i] = (i16)sine_val;      // Left channel
        buffer[i + 1] = (i16)sine_val;  // Right channel
    }
}

// Better sine wave using a simple lookup table
static const i16 sine_table[256] = {
    0, 804, 1608, 2410, 3212, 4011, 4808, 5602, 6393, 7179, 7962, 8739, 9512, 10278, 11039, 11793,
    12539, 13279, 14010, 14732, 15446, 16151, 16846, 17530, 18204, 18868, 19519, 20159, 20787, 21403, 22005, 22594,
    23170, 23731, 24279, 24811, 25329, 25832, 26319, 26790, 27245, 27683, 28105, 28510, 28898, 29268, 29621, 29956,
    30273, 30571, 30852, 31113, 31356, 31580, 31785, 31971, 32137, 32285, 32412, 32521, 32609, 32678, 32728, 32757,
    32767, 32757, 32728, 32678, 32609, 32521, 32412, 32285, 32137, 31971, 31785, 31580, 31356, 31113, 30852, 30571,
    30273, 29956, 29621, 29268, 28898, 28510, 28105, 27683, 27245, 26790, 26319, 25832, 25329, 24811, 24279, 23731,
    23170, 22594, 22005, 21403, 20787, 20159, 19519, 18868, 18204, 17530, 16846, 16151, 15446, 14732, 14010, 13279,
    12539, 11793, 11039, 10278, 9512, 8739, 7962, 7179, 6393, 5602, 4808, 4011, 3212, 2410, 1608, 804,
    0, -804, -1608, -2410, -3212, -4011, -4808, -5602, -6393, -7179, -7962, -8739, -9512, -10278, -11039, -11793,
    -12539, -13279, -14010, -14732, -15446, -16151, -16846, -17530, -18204, -18868, -19519, -20159, -20787, -21403, -22005, -22594,
    -23170, -23731, -24279, -24811, -25329, -25832, -26319, -26790, -27245, -27683, -28105, -28510, -28898, -29268, -29621, -29956,
    -30273, -30571, -30852, -31113, -31356, -31580, -31785, -31971, -32137, -32285, -32412, -32521, -32609, -32678, -32728, -32757,
    -32767, -32757, -32728, -32678, -32609, -32521, -32412, -32285, -32137, -31971, -31785, -31580, -31356, -31113, -30852, -30571,
    -30273, -29956, -29621, -29268, -28898, -28510, -28105, -27683, -27245, -26790, -26319, -25832, -25329, -24811, -24279, -23731,
    -23170, -22594, -22005, -21403, -20787, -20159, -19519, -18868, -18204, -17530, -16846, -16151, -15446, -14732, -14010, -13279,
    -12539, -11793, -11039, -10278, -9512, -8739, -7962, -7179, -6393, -5602, -4808, -4011, -3212, -2410, -1608, -804
};

void generate_tone_lookup(u16* buffer, int samples, int freq, int sample_rate) {
    for (int i = 0; i < samples; i += 2) {
        // Calculate phase (0-255 for table lookup)
        int phase = ((i / 2) * freq * 256) / sample_rate;
        phase = phase & 0xFF;  // Keep in range 0-255
        
        i16 sample = sine_table[phase];
        
        // Stereo output
        buffer[i] = sample;      // Left
        buffer[i + 1] = sample;  // Right
    }
}

// Generate square wave (for debugging - very obvious sound)
void generate_square_wave(u16* buffer, int samples, int freq, int sample_rate) {
    int half_period = sample_rate / (2 * freq);
    
    for (int i = 0; i < samples; i += 2) {
        i16 sample = ((i / 2) / half_period) % 2 ? 16384 : -16384;
        buffer[i] = sample;
        buffer[i + 1] = sample;
    }
}

// ============================================================================
// Test Functions
// ============================================================================

// Play a simple 440Hz tone (A4 note) for testing
void ac97_test_tone() {
    // Fill both buffers with a 440Hz tone
    generate_tone_lookup((u16*)&audio_buffers[0][0], BUFFER_SIZE, 440, 48000);
    generate_tone_lookup((u16*)&audio_buffers[1][0], BUFFER_SIZE, 440, 48000);
    
    // Setup DMA with these buffers
    ac97_setup_dma();
    
    // Start playback
    ac97_start();
    
    // Let it play for a while (blocking wait)
    // In a real OS, you'd handle this with interrupts
    for (volatile int i = 0; i < 10000000; i++);
    
    // Stop playback
    ac97_stop();
}

// Play a sequence of tones (musical scale)
void ac97_test() {
    // C major scale frequencies
    int scale[] = {262, 294, 330, 349, 392, 440, 494, 523};  // C4 to C5
    
    for (int note = 0; note < 8; note++) {
        // Generate tone
        generate_tone_lookup((u16*)&audio_buffers[0][0], BUFFER_SIZE, scale[note], 48000);
        generate_tone_lookup((u16*)&audio_buffers[1][0], BUFFER_SIZE, scale[note], 48000);
        
        // Setup and play
        ac97_setup_dma();
        ac97_start();
        
        // Play for ~0.5 seconds
        for (volatile int i = 0; i < 5000000; i++);
        
        ac97_stop();
        
        // Short pause between notes
        for (volatile int i = 0; i < 1000000; i++);
    }
}

// ============================================================================
// Interrupt Handler (optional but recommended)
// ============================================================================

void ac97_interrupt_handler() {
    u16 status = nabm_read16(AC97_PO_SR);
    
    if (status & AC97_SR_BCIS) {
        // Buffer completed
        // You could refill the completed buffer here for continuous playback
        
        // Clear the interrupt
        nabm_write16(AC97_PO_SR, AC97_SR_BCIS);
    }
    
    if (status & AC97_SR_FIFOE) {
        // FIFO error - handle it
        nabm_write16(AC97_PO_SR, AC97_SR_FIFOE);
    }
}

// ============================================================================
// Main initialization sequence
// ============================================================================

void ac97_full_init_and_test() {
    // 1. Initialize AC'97 hardware
    ac97_init();
    
    // 2. Setup DMA (optional here, test functions do it)
    // ac97_setup_dma();
    
    // 3. Run test
    // ac97_test_tone();      // Single 440Hz tone
    ac97_test();     // Musical scale
}