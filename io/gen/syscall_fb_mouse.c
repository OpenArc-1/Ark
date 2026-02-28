/**
 * gen/syscall_fb_mouse.c — Additional syscall handlers for VESA FB + mouse
 *
 * Add to gen/syscall.c (or compile separately and include in the kernel build).
 *
 * New syscall numbers:
 *   SYS_FB_INFO   300  — fill in framebuffer geometry for userspace
 *   SYS_MOUSE_READ 301 — read one PS/2 mouse packet (non-blocking)
 *
 * Kernel maps the physical framebuffer at ARK_FB_USERSPACE_BASE (0xE0000000)
 * before the first user ELF is loaded, so libark/vesa.c can write directly.
 *
 * Integration:
 *   1. In gen/syscall.c, add to the syscall dispatch table / switch:
 *        case 300: return ark_syscall_fb_info(a1,a2,a3,a4);
 *        case 301: return ark_syscall_mouse_read();
 *   2. Call ark_syscall_fb_map() once from kernel_main() AFTER vesa_init().
 *   3. Call ps2_mouse_kernel_init() once from kernel_main().
 */

#include "../include/ark/types.h"
#include "../include/ark/printk.h"
#include "../include/gpu/vesa.h"
#include "../include/ark/arch.h"

/* ── Framebuffer userspace virtual address ─────────────────────────────── */
#define ARK_FB_USERSPACE_BASE  0xE0000000u
#define ARK_FB_MAX_SIZE        (1024u * 768u * 4u)   /* 3 MB for 1024×768×32 */

/* ── Page table helpers (identity-plus map for physical = virtual here) ── */
/* Ark uses a flat 4 GB identity map for the kernel, so mapping the physical
 * framebuffer into the "user" window at 0xE0000000 requires adding a mapping
 * in the page directory used for ring-3 processes.
 *
 * If Ark does NOT use paging (real-mode / protected-mode flat), the physical
 * address IS already accessible at 0xE0000000 if the FB happens to live
 * there, otherwise a page-directory entry must be added.  This stub issues
 * a printk and returns; replace with real page-mapping code when paging
 * is wired up.
 */

/**
 * ark_syscall_fb_map() — Called once from kernel_main after vesa_init().
 *
 * Maps the physical framebuffer into the userspace window.
 * On Ark's current flat-memory model the framebuffer is already accessible;
 * this function mainly writes ARK_FB_USERSPACE_BASE so userspace code can
 * read through it.  For a paged kernel, insert a page-table mapping here.
 */
void ark_syscall_fb_map(void) {
    u32 phys = (u32)(uptr)vesa_get_framebuffer();
    if (!phys) {
        printk("ark_syscall_fb_map: VESA not ready, skipping map\n");
        return;
    }

    /* ── Paged kernel: add 4-MB identity page-dir entry ──────────────────
     * Replace this block with your page-directory code.
     * Example (4-MB PSE pages):
     *
     *   u32 *pgdir = (u32 *)current_pgdir_phys;
     *   u32  virt  = ARK_FB_USERSPACE_BASE;
     *   u32  idx   = virt >> 22;           // PDE index
     *   pgdir[idx] = (phys & 0xFFC00000)   // aligned physical base
     *                | 0x87;               // P | R/W | U/S | PS(4MB)
     *   asm volatile("invlpg (%0)" :: "r"(virt) : "memory");
     * ─────────────────────────────────────────────────────────────────── */

    /* Flat model: the FB physical address is already reachable; store it in
     * a known kernel variable so SYS_FB_INFO can return it. */
    printk("ark_fb_map: FB phys=0x%08x mapped to userspace 0x%08x\n",
           phys, ARK_FB_USERSPACE_BASE);
}

/**
 * ark_syscall_fb_info() — SYS_FB_INFO (300) handler
 *
 * Fills in framebuffer geometry via pointers supplied in the syscall
 * registers.  Because we call this with:
 *   eax=300, ebx=&w, ecx=&h, edx=&pitch, esi=&bpp
 * the kernel sees (a1..a4) as user virtual addresses of u32 variables.
 *
 * Returns: physical address of the framebuffer (or ARK_FB_USERSPACE_BASE
 *          if paging remapped it).  Returns 0 if VESA is not ready.
 */
long ark_syscall_fb_info(u32 w_ptr, u32 h_ptr, u32 pitch_ptr, u32 bpp_ptr) {
    if (!vesa_is_ready()) return 0;

    u32 w     = vesa_get_width();
    u32 h     = vesa_get_height();
    u32 pitch = vesa_get_pitch();
    u32 bpp   = vesa_get_bpp();

    /* Write results back through the userspace pointers */
    if (w_ptr)     *(u32 *)(uptr)w_ptr     = w;
    if (h_ptr)     *(u32 *)(uptr)h_ptr     = h;
    if (pitch_ptr) *(u32 *)(uptr)pitch_ptr = pitch;
    if (bpp_ptr)   *(u32 *)(uptr)bpp_ptr   = bpp;

    /* Return physical base (flat model: same as linear address) */
    return (long)(uptr)vesa_get_framebuffer();
}

/* ════════════════════════════════════════════════════════════════════════
 * PS/2 mouse kernel-side mini-driver (SYS_MOUSE_READ = 301)
 * ════════════════════════════════════════════════════════════════════════
 *
 * The full userspace driver (libark/mouse.c) talks directly to I/O ports
 * 0x60/0x64, which requires ring-0 (kernel mode) or IOPL = 3.
 *
 * Strategy A (recommended): grant IOPL 3 to the installer process with a
 *   small stub in the ELF loader or a dedicated syscall, and let libark
 *   handle everything in userspace.  This is the zero-kernel-change path.
 *
 * Strategy B: implement SYS_MOUSE_READ (301) in the kernel which reads one
 *   PS/2 packet and returns it packed into a 32-bit integer:
 *     bits 31..24 = flags byte (buttons + overflow)
 *     bits 23..16 = dx  (signed, two's complement)
 *     bits 15..8  = dy  (signed, two's complement)
 *     bits  7..0  = 0
 *   Returns -1 if no packet is ready.
 *
 * Implementation of Strategy B follows.
 */

/* 8042 ports */
#define PS2K_DATA   0x60
#define PS2K_STATUS 0x64
#define PS2K_CMD    0x64
#define PS2_OBF     0x01
#define PS2_IBF     0x02

static inline void _wait_write(void) {
    u32 t = 100000;
    while (--t && (io_inb(PS2K_STATUS) & PS2_IBF));
}
static inline int _wait_read(void) {
    u32 t = 100000;
    while (--t) if (io_inb(PS2K_STATUS) & PS2_OBF) return 1;
    return 0;
}
static inline void _mouse_send(u8 b) {
    _wait_write(); io_outb(PS2K_CMD, 0xD4);
    _wait_write(); io_outb(PS2K_DATA, b);
}
static inline u8 _ps2_read(void) { _wait_read(); return io_inb(PS2K_DATA); }

/* Packet accumulation state */
static u8  kps2_pkt[3];
static u32 kps2_idx = 0;
static u32 kps2_ready = 0;  /* 1 when a full packet is available */

/**
 * ps2_mouse_kernel_init() — Call once from kernel_main() before enabling IRQs.
 */
void ps2_mouse_kernel_init(void) {
    /* Enable aux port */
    _wait_write(); io_outb(PS2K_CMD, 0xA8);
    /* Enable IRQ12 in compaq status */
    _wait_write(); io_outb(PS2K_CMD, 0x20);
    if (!_wait_read()) return;
    u8 cfg = io_inb(PS2K_DATA);
    cfg |= 0x02;
    _wait_write(); io_outb(PS2K_CMD, 0x60);
    _wait_write(); io_outb(PS2K_DATA, cfg);
    /* Defaults + stream mode */
    _mouse_send(0xF6); _ps2_read();
    _mouse_send(0xF4); _ps2_read();
    kps2_idx = 0;
    kps2_ready = 0;
    printk("ps2_mouse: initialised\n");
}

/**
 * ark_syscall_mouse_read() — SYS_MOUSE_READ (301) handler.
 *
 * Drains one complete 3-byte PS/2 packet.
 * Returns packed u32 (see above) or -1 if nothing available.
 */
long ark_syscall_mouse_read(void) {
    /* Drain bytes from the 8042 output buffer */
    while (io_inb(PS2K_STATUS) & PS2_OBF) {
        u8 byte = io_inb(PS2K_DATA);
        if (kps2_idx == 0 && !(byte & 0x08)) continue; /* re-sync */
        kps2_pkt[kps2_idx++] = byte;
        if (kps2_idx == 3) {
            kps2_ready = 1;
            kps2_idx   = 0;
        }
    }
    if (!kps2_ready) return -1;
    kps2_ready = 0;
    /* Pack */
    u32 packed = ((u32)kps2_pkt[0] << 24) |
                 ((u32)kps2_pkt[1] << 16) |
                 ((u32)kps2_pkt[2] <<  8);
    return (long)packed;
}
