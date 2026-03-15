/**
 * hw/vm_detect.c — Virtual machine detection for Ark
 *
 * Detection method
 * ─────────────────
 * 1. CPUID leaf 1, ECX bit 31 — "Hypervisor Present" flag.
 *    All major hypervisors set this. Real hardware CPUs leave it clear.
 *
 * 2. CPUID leaf 0x40000000 — "Hypervisor CPUID leaf".
 *    Returns a 12-byte ASCII vendor string in EBX:ECX:EDX, similar to
 *    the manufacturer string from leaf 0.  Known strings:
 *      "KVMKVMKVM\0\0\0"  — KVM (also used by QEMU+KVM)
 *      "TCGTCGTCGTCG"     — QEMU TCG (software emulation)
 *      "VBoxVBoxVBox"     — VirtualBox
 *      "VMwareVMware"     — VMware
 *      "Microsoft Hv"     — Hyper-V
 *
 * 3. BGA presence — checked via I/O port read of BGA ID register (0x01CE).
 *    Bochs/QEMU returns 0xB0C0–0xB0CF; VirtualBox also supports BGA.
 *    VMware and Hyper-V do not have BGA.
 *
 * Fallback resolution table (used when firmware doesn't give us anything):
 *   QEMU:       1024×768   — BGA safe default
 *   VirtualBox: 1024×768   — BGA safe default
 *   VMware:     1024×768   — SVGA2 (we can't program it, use whatever came in)
 *   Hyper-V:    1024×768   — synthetic video
 *   Unknown VM: 1024×768   — safe conservative default
 */

#include "ark/types.h"
#include "hw/vm_detect.h"

/* ── CPUID helper ────────────────────────────────────────────────────────── */
static void cpuid(u32 leaf, u32 *eax, u32 *ebx, u32 *ecx, u32 *edx) {
    __asm__ volatile(
        "cpuid"
        : "=a"(*eax), "=b"(*ebx), "=c"(*ecx), "=d"(*edx)
        : "a"(leaf), "c"(0)
    );
}

/* ── String comparison (no libc in early boot) ───────────────────────────── */
static bool str12eq(const char *a, const char *b) {
    for (int i = 0; i < 12; i++)
        if (a[i] != b[i]) return false;
    return true;
}

/* ── BGA presence check ──────────────────────────────────────────────────── */
static inline void outw_bga(u16 port, u16 val) {
    __asm__ volatile("outw %0,%1"::"a"(val),"Nd"(port));
}
static inline u16 inw_bga(u16 port) {
    u16 v; __asm__ volatile("inw %1,%0":"=a"(v):"Nd"(port)); return v;
}
#define BGA_IDX 0x01CE
#define BGA_DAT 0x01CF

static bool bga_present(void) {
    outw_bga(BGA_IDX, 0);          /* select ID register */
    u16 id = inw_bga(BGA_DAT);
    return (id >= 0xB0C0 && id <= 0xB0CF);
}

/* ── Public API ──────────────────────────────────────────────────────────── */

const char *vm_type_name(vm_type_t t) {
    switch (t) {
    case VM_NONE:    return "bare-metal";
    case VM_QEMU:    return "QEMU";
    case VM_VBOX:    return "VirtualBox";
    case VM_VMWARE:  return "VMware";
    case VM_HYPERV:  return "Hyper-V";
    case VM_KVM:     return "KVM";
    case VM_UNKNOWN: return "unknown-hypervisor";
    default:         return "?";
    }
}

void vm_detect(vm_info_t *out) {
    /* Zero the struct */
    out->is_vm   = false;
    out->type    = VM_NONE;
    out->name    = "bare-metal";
    out->fb_w    = 0;
    out->fb_h    = 0;
    out->fb_bpp  = 32;
    out->has_bga = false;

    /* ── Step 1: CPUID leaf 1, ECX bit 31 — hypervisor present flag ── */
    u32 eax, ebx, ecx, edx;
    cpuid(1, &eax, &ebx, &ecx, &edx);
    if (!(ecx & (1u << 31))) {
        /* Bit clear — almost certainly real hardware. Done. */
        return;
    }

    /* ── Step 2: Read hypervisor vendor string from leaf 0x40000000 ── */
    cpuid(0x40000000u, &eax, &ebx, &ecx, &edx);

    /* Vendor string is EBX:ECX:EDX packed as bytes (little-endian) */
    char vendor[13];
    vendor[ 0] = (char)( ebx        & 0xFF);
    vendor[ 1] = (char)((ebx >>  8) & 0xFF);
    vendor[ 2] = (char)((ebx >> 16) & 0xFF);
    vendor[ 3] = (char)((ebx >> 24) & 0xFF);
    vendor[ 4] = (char)( ecx        & 0xFF);
    vendor[ 5] = (char)((ecx >>  8) & 0xFF);
    vendor[ 6] = (char)((ecx >> 16) & 0xFF);
    vendor[ 7] = (char)((ecx >> 24) & 0xFF);
    vendor[ 8] = (char)( edx        & 0xFF);
    vendor[ 9] = (char)((edx >>  8) & 0xFF);
    vendor[10] = (char)((edx >> 16) & 0xFF);
    vendor[11] = (char)((edx >> 24) & 0xFF);
    vendor[12] = '\0';

    out->is_vm = true;

    /* ── Step 3: Match vendor string ── */
    if (str12eq(vendor, "KVMKVMKVM\0\0\0") ||
        str12eq(vendor, "TCGTCGTCGTCG")) {
        /* KVM string is used by both QEMU+KVM and QEMU TCG.
         * Differentiate: QEMU TCG vendor = "TCGTCGTCGTCG".
         * For our purposes both get BGA treatment. */
        out->type = str12eq(vendor, "TCGTCGTCGTCG") ? VM_QEMU : VM_KVM;
        out->name = (out->type == VM_QEMU) ? "QEMU/TCG" : "QEMU/KVM";
    } else if (str12eq(vendor, "VBoxVBoxVBox")) {
        out->type = VM_VBOX;
        out->name = "VirtualBox";
    } else if (str12eq(vendor, "VMwareVMware")) {
        out->type = VM_VMWARE;
        out->name = "VMware";
    } else if (vendor[0]=='M' && vendor[1]=='i' && vendor[2]=='c' &&
               vendor[3]=='r' && vendor[4]=='o' && vendor[5]=='s') {
        /* "Microsoft Hv" */
        out->type = VM_HYPERV;
        out->name = "Hyper-V";
    } else {
        out->type = VM_UNKNOWN;
        out->name = "unknown-hypervisor";
    }

    /* ── Step 4: BGA check ── */
    out->has_bga = bga_present();

    /* ── Step 5: Assign safe fallback resolution ── */
    switch (out->type) {
    case VM_QEMU:
    case VM_KVM:
        /* QEMU BGA: 1024×768 is always safe; use 800×600 if BGA absent */
        out->fb_w = out->has_bga ? 1024 : 800;
        out->fb_h = out->has_bga ? 768  : 600;
        break;
    case VM_VBOX:
        /* VirtualBox supports BGA too */
        out->fb_w = out->has_bga ? 1024 : 800;
        out->fb_h = out->has_bga ? 768  : 600;
        break;
    case VM_VMWARE:
        /* VMware SVGA2 — we can't program it from here; use conservative default */
        out->fb_w = 1024;
        out->fb_h = 768;
        break;
    case VM_HYPERV:
        /* Hyper-V synthetic video */
        out->fb_w = 1024;
        out->fb_h = 768;
        break;
    default:
        out->fb_w = 1024;
        out->fb_h = 768;
        break;
    }
    out->fb_bpp = 32;
}