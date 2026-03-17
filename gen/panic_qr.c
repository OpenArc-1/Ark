/**
 * gen/panic_qr.c - QR Code kernel panic system
 * 
 * Minimal QR Code generator for kernel panic debugging.
 * Uses static lookup tables - no dynamic memory allocation.
 * Supports QR Version 1-L (21x21, Low error correction).
 */

#include "ark/types.h"
#include "ark/printk.h"
#include "ark/panic.h"

void kernel_panic(const char *msg);

#if CONFIG_DEBUG_PANIC_QR

#define QR_VERSION    1
#define QR_SIZE       21
#define QR_MODULES    (QR_SIZE * QR_SIZE)
#define QR_DATA_BYTES 19

#define PANIC_LOG_SIZE  768
#define PANIC_BOOT_MARKERS 8

extern u8 *g_fb;
extern u32 g_pitch;
extern u32 g_w;
extern u32 g_h;
extern u32 g_Bpp;

static char panic_log_buf[PANIC_LOG_SIZE];
static u32 panic_log_pos = 0;
static bool panic_log_full = false;

static const char *panic_boot_markers[PANIC_BOOT_MARKERS];
static int panic_boot_marker_count = 0;
static const char *panic_last_phase = "start";

void panic_log_putchar(char c) {
    if (panic_log_full) {
        for (int i = 0; i < PANIC_LOG_SIZE - 1; i++) {
            panic_log_buf[i] = panic_log_buf[i + 1];
        }
        panic_log_buf[PANIC_LOG_SIZE - 1] = c;
    } else {
        panic_log_buf[panic_log_pos++] = c;
        if (panic_log_pos >= PANIC_LOG_SIZE - 1) {
            panic_log_full = true;
            panic_log_pos = PANIC_LOG_SIZE - 1;
        }
    }
}

void panic_log_write(const char *s) {
    while (*s) {
        panic_log_putchar(*s++);
    }
}

void panic_log_clear(void) {
    panic_log_pos = 0;
    panic_log_full = false;
    for (int i = 0; i < PANIC_LOG_SIZE; i++) panic_log_buf[i] = 0;
    panic_boot_marker_count = 0;
    panic_last_phase = "start";
}

void panic_set_boot_phase(const char *phase) {
    panic_last_phase = phase;
    if (panic_boot_marker_count < PANIC_BOOT_MARKERS) {
        panic_boot_markers[panic_boot_marker_count++] = phase;
    }
}

const char *panic_get_log(void) {
    return panic_log_buf;
}

const char *panic_get_last_phase(void) {
    return panic_last_phase;
}

int panic_get_boot_marker_count(void) {
    return panic_boot_marker_count;
}

const char *panic_get_boot_marker(int idx) {
    if (idx >= 0 && idx < panic_boot_marker_count) {
        return panic_boot_markers[idx];
    }
    return NULL;
}

static const u8 qr_finder_pattern[7][5] = {
    {1,1,1,1,1},
    {1,0,0,0,1},
    {1,0,1,0,1},
    {1,0,0,0,1},
    {1,1,1,1,1},
    {0,0,1,0,0},
    {0,0,1,0,0}
};

static const u8 qr_alignment_pattern[5][5] = {
    {1,1,1,1,1},
    {1,0,0,0,1},
    {1,0,1,0,1},
    {1,0,0,0,1},
    {1,1,1,1,1}
};

static u8 qr_matrix[QR_SIZE][QR_SIZE];
static u8 qr_codewords[QR_DATA_BYTES];
static u8 qr_bit_buffer = 0;
static int qr_bit_pos = 0;

static void qr_set_module(int x, int y, int val) {
    if (x >= 0 && x < QR_SIZE && y >= 0 && y < QR_SIZE) {
        qr_matrix[y][x] = val ? 1 : 0;
    }
}

static void qr_add_finder_patterns(void) {
    for (int y = 0; y < 7; y++) {
        for (int x = 0; x < 7; x++) {
            qr_set_module(x, y, qr_finder_pattern[y][x]);
            qr_set_module(x + QR_SIZE - 7, y, qr_finder_pattern[y][x]);
            qr_set_module(x, y + QR_SIZE - 7, qr_finder_pattern[y][x]);
        }
    }
    for (int y = 0; y < 5; y++) {
        for (int x = 0; x < 5; x++) {
            qr_set_module(x + QR_SIZE - 8, y + QR_SIZE - 8, qr_alignment_pattern[y][x]);
        }
    }
}

static void qr_add_timing_patterns(void) {
    for (int i = 8; i < QR_SIZE - 8; i++) {
        qr_set_module(6, i, (i % 2 == 0) ? 1 : 0);
        qr_set_module(i, 6, (i % 2 == 0) ? 1 : 0);
    }
}

static void qr_add_reserved_format_area(void) {
    for (int i = 0; i < 9; i++) {
        qr_set_module(8, i, 0);
        qr_set_module(i, 8, 0);
    }
    qr_set_module(8, 8, 0);
    for (int i = 0; i < 7; i++) {
        qr_set_module(8, QR_SIZE - 1 - i, 0);
        qr_set_module(QR_SIZE - 1 - i, 8, 0);
    }
}

static void qr_init_matrix(void) {
    for (int y = 0; y < QR_SIZE; y++) {
        for (int x = 0; x < QR_SIZE; x++) {
            qr_matrix[y][x] = 0;
        }
    }
    qr_add_finder_patterns();
    qr_add_timing_patterns();
    qr_add_reserved_format_area();
}

static void qr_put_bit(int bit) {
    qr_bit_buffer = (qr_bit_buffer << 1) | (bit & 1);
    qr_bit_pos++;
    if (qr_bit_pos == 8) {
        qr_codewords[qr_bit_pos / 8 - 1] = qr_bit_buffer;
        qr_bit_buffer = 0;
        qr_bit_pos = 0;
    }
}

static void qr_put_bits(int bits, int count) {
    for (int i = count - 1; i >= 0; i--) {
        qr_put_bit((bits >> i) & 1);
    }
}

static int qr_alphanumeric_encode(const char *data, int len) {
    static const char *alphanum = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ $%*+-./:";
    int i = 0;
    
    qr_put_bits(0100, 4);
    qr_put_bits(len, 8);
    
    while (i < len) {
        if (i + 1 < len) {
            int c1 = -1, c2 = -1;
            for (int j = 0; alphanum[j]; j++) {
                if (alphanum[j] == data[i]) c1 = j;
                if (alphanum[j] == data[i+1]) c2 = j;
            }
            if (c1 >= 0 && c2 >= 0) {
                qr_put_bits(c1 * 45 + c2, 11);
                i += 2;
            } else {
                if (c1 >= 0) {
                    qr_put_bits(c1, 6);
                } else {
                    qr_put_bits(data[i], 6);
                }
                i++;
            }
        } else {
            int c1 = -1;
            for (int j = 0; alphanum[j]; j++) {
                if (alphanum[j] == data[i]) c1 = j;
            }
            if (c1 >= 0) {
                qr_put_bits(c1, 6);
            } else {
                qr_put_bits(data[i], 6);
            }
            i++;
        }
    }
    
    qr_put_bits(0, 4);
    while (qr_bit_pos != 0) {
        qr_put_bit(0);
    }
    
    return (qr_bit_pos + 7) / 8;
}

static const u8 qr_gf256[256] = {
    0x01,0x02,0x04,0x08,0x10,0x20,0x40,0x80,0x19,0x32,0x64,0xC8,0x89,0x1B,0x36,0x6C,
    0xD8,0xB1,0x7B,0xF6,0xD1,0xBD,0x7D,0xFA,0xE9,0x9F,0x27,0x4E,0x9C,0x21,0x42,0x84,
    0x11,0x22,0x44,0x88,0x05,0x0A,0x14,0x28,0x50,0xA0,0x79,0xF2,0xE9,0x97,0x2F,0x5E,
    0xBC,0x71,0xE2,0xC9,0x8F,0x03,0x06,0x0C,0x18,0x30,0x60,0xC0,0x99,0x1F,0x3E,0x7C,
    0xF8,0xE1,0xC3,0x8B,0x1F,0x3E,0x7C,0xF8,0xE1,0xC3,0x8B,0x13,0x26,0x4C,0x98,0x21,
    0x42,0x84,0x09,0x12,0x24,0x48,0x90,0x29,0x52,0xA4,0x49,0x92,0x25,0x4A,0x94,0x39,
    0x72,0xE4,0xD9,0xAB,0x57,0xAE,0x5C,0xB8,0x61,0xC2,0x99,0x1F,0x3E,0x7C,0xF8,0xE1,
    0xC3,0x8B,0x13,0x26,0x4C,0x98,0x21,0x42,0x84,0x09,0x12,0x24,0x48,0x90,0x29,0x52,
    0xA4,0x51,0xA2,0x59,0xB2,0x69,0xD2,0xB9,0x6F,0xDE,0xA1,0x5F,0xBE,0x61,0xC2,0x99,
    0x1F,0x3E,0x7C,0xF8,0xE1,0xC3,0x8B,0x13,0x26,0x4C,0x98,0x21,0x42,0x84,0x09,0x12,
    0x24,0x48,0x90,0x29,0x52,0xA4,0x51,0xA2,0x59,0xB2,0x69,0xD2,0xB9,0x6F,0xDE,0xA1,
    0x5F,0xBE,0x61,0xC2,0x99,0x1F,0x3E,0x7C,0xF8,0xE1,0xC3,0x8B,0x13,0x26,0x4C,0x98,
    0x21,0x42,0x84,0x09,0x12,0x24,0x48,0x90,0x29,0x52,0xA4,0x51,0xA2,0x59,0xB2,0x69,
    0xD2,0xB9,0x6F,0xDE,0xA1,0x5F,0xBE,0x61,0xC2,0x99,0x1F,0x3E,0x7C,0xF8,0xE1,0xC3,
    0x8B,0x13,0x26,0x4C,0x98,0x21,0x42,0x84,0x09,0x12,0x24,0x48,0x90,0x29,0x52,0xA4,
    0x51,0xA2,0x59,0xB2,0x69,0xD2,0xB9,0x6F,0xDE,0xA1,0x5F,0xBE,0x61,0xC2,0x81,0x17
};

static const u8 qr_log[256] = {
    0,0,1,2,3,4,5,6,7,8,9,10,11,12,13,14,15,16,17,18,19,20,21,22,23,24,25,26,27,28,29,
    30,31,32,33,34,35,36,37,38,39,40,41,42,43,44,45,46,47,48,49,50,51,52,53,54,55,56,
    57,58,59,60,61,62,63,64,65,66,67,68,69,70,71,72,73,74,75,76,77,78,79,80,81,82,83,
    84,85,86,87,88,89,90,91,92,93,94,95,96,97,98,99,100,101,102,103,104,105,106,107,
    108,109,110,111,112,113,114,115,116,117,118,119,120,121,122,123,124,125,126,127,
    128,129,130,131,132,133,134,135,136,137,138,139,140,141,142,143,144,145,146,147,
    148,149,150,151,152,153,154,155,156,157,158,159,160,161,162,163,164,165,166,167,
    168,169,170,171,172,173,174,175,176,177,178,179,180,181,182,183,184,185,186,187,
    188,189,190,191,192,193,194,195,196,197,198,199,200,201,202,203,204,205,206,207,
    208,209,210,211,212,213,214,215,216,217,218,219,220,221,222,223,224,225,226,227,
    228,229,230,231,232,233,234,235,236,237,238,239,240,241,242,243,244,245,246,247,
    248,249,250,251,252,253,254,0
};

static void qr_calc_error_correction(const u8 *data, int data_len, u8 *ecc, int ecc_len) {
    int num_blocks = 1;
    int block_ec_len = ecc_len / num_blocks;
    
    for (int b = 0; b < num_blocks; b++) {
        int msg_len = data_len;
        u8 msg[32];
        for (int i = 0; i < msg_len; i++) msg[i] = data[i];
        
        for (int i = 0; i < msg_len; i++) {
            if (msg[i] != 0) {
                u8 coef = qr_log[msg[i]];
                for (int j = 0; j < block_ec_len; j++) {
                    int idx = (coef + block_ec_len - 1 - j) % 255;
                    msg[j] ^= qr_gf256[idx];
                }
            }
        }
        
        for (int i = 0; i < block_ec_len; i++) {
            ecc[i] = msg[i];
        }
    }
}

static void qr_place_data(const char *data, int len) {
    int data_words = qr_alphanumeric_encode(data, len);
    u8 ecc[16];
    qr_calc_error_correction(qr_codewords, data_words, ecc, 16);
    
    int total_words = data_words + 16;
    u8 all_words[48];
    for (int i = 0; i < data_words; i++) all_words[i] = qr_codewords[i];
    for (int i = 0; i < 16; i++) all_words[data_words + i] = ecc[i];
    
    int x = QR_SIZE - 1;
    int y = QR_SIZE - 1;
    int dir = -1;
    int bit_idx = 0;
    
    for (int i = 0; i < total_words; i++) {
        for (int bit = 7; bit >= 0; bit--) {
            int done = 0;
            while (!done) {
                if (x == 6) x--;
                
                int module_exists = 1;
                if (x < 0 || y < 0 || y >= QR_SIZE) {
                    module_exists = 0;
                } else if (qr_matrix[y][x] != 0) {
                    module_exists = 0;
                }
                
                if (module_exists) {
                    qr_matrix[y][x] = ((all_words[i] >> bit) & 1);
                    done = 1;
                }
                
                x += dir;
                y -= dir;
                if (x < 0 || x >= QR_SIZE || y < 0) {
                    x += dir;
                    y -= dir;
                    dir = -dir;
                    x += dir;
                    y -= dir;
                }
            }
        }
    }
}

static void qr_apply_mask(int mask_id) {
    for (int y = 0; y < QR_SIZE; y++) {
        for (int x = 0; x < QR_SIZE; x++) {
            if (qr_matrix[y][x] != 0 && qr_matrix[y][x] != 2) {
                int apply = 0;
                switch (mask_id) {
                    case 0: apply = ((x + y) % 2 == 0); break;
                    case 1: apply = (y % 2 == 0); break;
                    case 2: apply = (x % 3 == 0); break;
                    case 3: apply = ((x + y) % 3 == 0); break;
                    case 4: apply = (((x/3) + (y/2)) % 2 == 0); break;
                    case 5: apply = (((x*y) % 2) + ((x*y) % 3) == 0); break;
                    case 6: apply = ((((x*y) % 2) + ((x*y) % 3)) % 2 == 0); break;
                    case 7: apply = ((((x+y) % 2) + ((x*y) % 3)) % 2 == 0); break;
                }
                if (apply) qr_matrix[y][x] ^= 1;
            }
        }
    }
}

static void qr_add_format_info(void) {
    u32 format_data = 0x077C4;
    for (int i = 0; i < 15; i++) {
        int bit = (format_data >> i) & 1;
        if (i < 6) {
            qr_matrix[8][i] = bit;
            qr_matrix[QR_SIZE - 1 - i][8] = bit;
        } else if (i < 8) {
            qr_matrix[8][i + 1] = bit;
            qr_matrix[QR_SIZE - 1 - i + 1][8] = bit;
        } else {
            qr_matrix[8][QR_SIZE - 15 + i] = bit;
            qr_matrix[15 - i][8] = bit;
        }
    }
}

static void qr_generate(const char *data) {
    qr_init_matrix();
    qr_place_data(data, 0);
    qr_apply_mask(0);
    qr_add_format_info();
}

static void panic_draw_pixel(int x, int y, u32 color) {
    if (!g_fb || !g_pitch || !g_w || !g_h) return;
    if (x < 0 || x >= (int)g_w || y < 0 || y >= (int)g_h) return;
    if (g_Bpp == 4) {
        u32 *p = (u32*)(g_fb + y * g_pitch + x * 4);
        *p = color;
    }
}

static void panic_draw_rect(int x, int y, int w, int h, u32 color) {
    for (int py = y; py < y + h; py++) {
        for (int px = x; px < x + w; px++) {
            panic_draw_pixel(px, py, color);
        }
    }
}

void panic_qr_draw_module(int mx, int my, int module_size, u32 fg_color, u32 bg_color) {
    int px = mx * module_size;
    int py = my * module_size;
    int val = qr_matrix[my][mx];
    u32 color = val ? fg_color : bg_color;
    panic_draw_rect(px, py, module_size, module_size, color);
}

void panic_qr_display(const char *qr_data) {
    qr_generate(qr_data);
    
    printk("\n[QR] Debug data:\n%s\n", qr_data);
    
    if (!g_fb || !g_pitch || !g_w || !g_h) {
        printk("[QR] No framebuffer - cannot display QR\n");
        return;
    }
    
    u32 qr_display_size = g_w < g_h ? g_w - 40 : g_h - 120;
    if (qr_display_size > 400) qr_display_size = 400;
    if (qr_display_size < 84) qr_display_size = 84;
    
    int module_size = qr_display_size / QR_SIZE;
    if (module_size < 1) module_size = 1;
    
    int qr_actual_size = module_size * QR_SIZE;
    int offset_x = (g_w - qr_actual_size) / 2;
    int offset_y = 40;
    
    u32 fg = 0xFFFFFFFF;
    u32 bg = 0xFF000000;
    u32 border = 0xFF404040;
    
    panic_draw_rect(offset_x - module_size, offset_y - module_size,
                    qr_actual_size + module_size*2, qr_actual_size + module_size*2, border);
    
    for (int y = 0; y < QR_SIZE; y++) {
        for (int x = 0; x < QR_SIZE; x++) {
            panic_qr_draw_module(x, y, module_size, fg, bg);
        }
    }
    
    printk("[QR] Displayed at center of screen\n");
}

static int panic_strcat(char *buf, int buf_len, const char *str) {
    int i = 0;
    while (buf[i] && i < buf_len) i++;
    int j = 0;
    while (str[j] && i + j < buf_len - 1) {
        buf[i + j] = str[j];
        j++;
    }
    buf[i + j] = 0;
    return i + j;
}

static int panic_hex_to_str(char *buf, int buf_len, unsigned long long val) {
    const char *hex = "0123456789abcdef";
    char tmp[32];
    int pos = 0;
    
    if (val == 0) {
        tmp[pos++] = '0';
    } else {
        char rev[32];
        int rpos = 0;
        while (val > 0) {
            rev[rpos++] = hex[val & 0xF];
            val >>= 4;
        }
        for (int i = rpos - 1; i >= 0; i--) {
            tmp[pos++] = rev[i];
        }
    }
    tmp[pos] = 0;
    
    return panic_strcat(buf, buf_len, tmp);
}

static int panic_append_hex(char *buf, int buf_len, const char *prefix, unsigned long long val) {
    int pos = panic_strcat(buf, buf_len, prefix);
    pos += panic_hex_to_str(buf + pos, buf_len - pos, val);
    return pos;
}

static void panic_build_qr_data(char *buf, int buf_len, const char *reason,
                                u64 rip, u64 rsp, u64 rbp) {
    int pos = 0;
    
    pos += panic_strcat(buf, buf_len - pos, "P=");
    const char *r = reason ? reason : "unk";
    while (*r && pos < buf_len - 3) {
        char c = *r++;
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        if (c >= 'A' && c <= 'Z') buf[pos++] = c;
        else if (c >= '0' && c <= '9') buf[pos++] = c;
        else if (c == ' ') buf[pos++] = '_';
        else if (c == ':' || c == '-' || c == '/' || c == '.') buf[pos++] = c;
    }
    if (pos < buf_len - 1) buf[pos++] = '|';
    
    const char *phase = panic_get_last_phase();
    pos += panic_strcat(buf + pos, buf_len - pos, "PH=");
    while (*phase && pos < buf_len - 3) {
        char c = *phase++;
        if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
        if (c >= 'A' && c <= 'Z') buf[pos++] = c;
        else if (c >= '0' && c <= '9') buf[pos++] = c;
        else if (c == '_' || c == '-') buf[pos++] = c;
    }
    if (pos < buf_len - 1) buf[pos++] = '|';
    
    pos += panic_append_hex(buf + pos, buf_len - pos, "RSP=", rsp);
    if (pos < buf_len - 1) buf[pos++] = '|';
    
    pos += panic_append_hex(buf + pos, buf_len - pos, "RBP=", rbp);
    
    buf[pos] = 0;
}

void kernel_panic_with_qr(const char *msg) {
    __asm__ __volatile__("cli");
    
    u32 esp = 0, ebp = 0;
    u64 rsp = 0, rip = 0;
    
#if CONFIG_64BIT
    u64 rax = 0, rbx = 0, rcx = 0, rdx = 0;
    u64 rsi = 0, rdi = 0, rbp = 0, rflags = 0;
    __asm__ volatile (
        "mov %%rax, %0\n"
        "mov %%rbx, %1\n"
        "mov %%rcx, %2\n"
        "mov %%rdx, %3\n"
        "mov %%rsi, %4\n"
        "mov %%rdi, %5\n"
        "mov %%rbp, %6\n"
        "pushfq\npop %7\n"
        "mov %%rsp, %8\n"
        : "=m"(rax), "=m"(rbx), "=m"(rcx), "=m"(rdx), 
          "=m"(rsi), "=m"(rdi), "=m"(rbp), "=m"(rflags),
          "=m"(rsp)
    );
#else
    u32 eax = 0, ebx = 0, ecx = 0, edx = 0;
    u32 esi = 0, edi = 0, eflags = 0;
    u32 esp = 0;
    __asm__ volatile (
        "mov %%eax, %0\n"
        "mov %%ebx, %1\n"
        "mov %%ecx, %2\n"
        "mov %%edx, %3\n"
        "mov %%esi, %4\n"
        "mov %%edi, %5\n"
        "mov %%ebp, %6\n"
        "pushfl\npop %7\n"
        "mov %%esp, %8\n"
        : "=m"(eax), "=m"(ebx), "=m"(ecx), "=m"(edx), 
          "=m"(esi), "=m"(edi), "=m"(ebp), "=m"(eflags),
          "=m"(esp)
    );
    rsp = esp;
#endif
    
    printk(T, "*** KERNEL PANIC ***\n");
    if (msg) {
        printk(T, "reason : %s\n", msg);
    } else {
        printk(T, "reason : unknown fatal error\n");
    }
    
    printk(T, "CPU    : ");
    cpu_name();
    id_ldm();
    printk("\n");
    
#if CONFIG_64BIT
    printk(T, "Registers:\n");
    printk("  RIP=%llx RSP=%llx RBP=%llx\n", 
           (unsigned long long)rip, (unsigned long long)rsp, (unsigned long long)rbp);
#else
    printk(T, "Registers:\n");
    printk("  ESP=%08x EBP=%08x\n", esp, ebp);
#endif
    
    char qr_data[256];
#if CONFIG_64BIT
    panic_build_qr_data(qr_data, sizeof(qr_data), msg ? msg : "unknown",
                        rip, rsp, rbp);
#else
    panic_build_qr_data(qr_data, sizeof(qr_data), msg ? msg : "unknown",
                        (u64)(u32)esp, (u64)(u32)esp, (u64)(u32)ebp);
#endif
    
    printk("\n[QR] Generating debug QR code...\n");
    panic_qr_display(qr_data);
    
    printk("\nThe system has been halted.\n");
    printk("Please scan QR code for detailed debug information.\n");
    
    for (;;)
        __asm__ __volatile__("hlt");
}

#else

void kernel_panic_with_qr(const char *msg) {
    kernel_panic(msg);
}

#endif
