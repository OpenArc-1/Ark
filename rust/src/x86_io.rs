#![no_std]

#[inline]
pub fn outb(port: u16, value: u8) {
    unsafe {
        core::arch::asm!("out dx, al",
            in("dx") port,
            in("al") value,
            options(nostack, preserves_flags)
        );
    }
}

#[inline]
pub fn outw(port: u16, value: u16) {
    unsafe {
        core::arch::asm!("out dx, ax",
            in("dx") port,
            in("ax") value,
            options(nostack, preserves_flags)
        );
    }
}

#[inline]
pub fn outl(port: u16, value: u32) {
    unsafe {
        core::arch::asm!("out dx, eax",
            in("dx") port,
            in("eax") value,
            options(nostack, preserves_flags)
        );
    }
}

#[inline]
pub fn inb(port: u16) -> u8 {
    let value: u8;
    unsafe {
        core::arch::asm!("in al, dx",
            in("dx") port,
            out("al") value,
            options(nostack, preserves_flags)
        );
    }
    value
}

#[inline]
pub fn inw(port: u16) -> u16 {
    let value: u16;
    unsafe {
        core::arch::asm!("in ax, dx",
            in("dx") port,
            out("ax") value,
            options(nostack, preserves_flags)
        );
    }
    value
}

#[inline]
pub fn inl(port: u16) -> u32 {
    let value: u32;
    unsafe {
        core::arch::asm!("in eax, dx",
            in("dx") port,
            out("eax") value,
            options(nostack, preserves_flags)
        );
    }
    value
}

/// Read a 32-bit MMIO register.
/// `base` is a pointer-sized MMIO address; safe on both 32- and 64-bit.
#[inline]
pub fn mmio_read32(base: usize, offset: u32) -> u32 {
    let ptr = (base + offset as usize) as *const u32;
    unsafe { ptr.read_volatile() }
}

/// Write a 32-bit MMIO register.
/// `base` is a pointer-sized MMIO address; safe on both 32- and 64-bit.
#[inline]
pub fn mmio_write32(base: usize, offset: u32, value: u32) {
    let ptr = (base + offset as usize) as *mut u32;
    unsafe { ptr.write_volatile(value) };
}

#[inline]
pub fn msleep(ms: u32) {
    for _ in 0..ms * 60000 {
        unsafe { core::arch::asm!("") };
    }
}
