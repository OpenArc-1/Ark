#![no_std]

static mut USB_HEAP: [u8; 131072] = [0; 131072];
static mut HEAP_CUR: usize = 0;

const ALIGNMENT: usize = 4096;

#[inline]
pub fn alloc(size: usize) -> *mut u8 {
    unsafe {
        let aligned = (HEAP_CUR + ALIGNMENT - 1) & !(ALIGNMENT - 1);
        if aligned + size > USB_HEAP.len() {
            return core::ptr::null_mut();
        }
        HEAP_CUR = aligned + size;
        USB_HEAP.as_mut_ptr().add(aligned)
    }
}

#[inline]
pub fn alloc_zeroed(size: usize) -> *mut u8 {
    let ptr = alloc(size);
    if !ptr.is_null() {
        unsafe {
            core::ptr::write_bytes(ptr, 0, size);
        }
    }
    ptr
}

/// Returns the physical address of a pointer as a native pointer-width integer.
/// On 32-bit this is 32 bits, on 64-bit this is 64 bits (via usize).
/// The heap is a static array so physical == virtual for a bare-metal kernel.
#[inline]
pub fn phys(v: *const u8) -> usize {
    v as usize
}

/// Low 32 bits of a physical address.
/// Use for hardware registers that are documented as 32-bit (e.g. UHCI FLBASE,
/// or the low-half of a split 64-bit xHCI/OHCI register).
#[inline]
pub fn phys_lo(v: *const u8) -> u32 {
    (v as usize) as u32
}

/// High 32 bits of a physical address.
/// Always 0 on 32-bit targets; used for the upper half of 64-bit xHCI registers.
#[inline]
pub fn phys_hi(v: *const u8) -> u32 {
    #[cfg(target_pointer_width = "64")]
    { ((v as usize) >> 32) as u32 }
    #[cfg(not(target_pointer_width = "64"))]
    { 0u32 }
}
