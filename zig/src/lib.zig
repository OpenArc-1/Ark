// zig/src/lib.zig — Ark kernel block scanner  (x86 + x86_64)
//
// Compiled by the Makefile as:
//   zig build-lib -target x86_64-freestanding-none   (ARCH=x86_64)
//   zig build-lib -target x86-freestanding-none      (ARCH=x86)
//
// C callers: #include "zig/lib.h"
// Enable:    ZIG_ENABLE=1 in .kconfig

const builtin = @import("builtin");

// ---------------------------------------------------------------------------
// Arch detection  —  mirrors include/ark/arch.h
// ---------------------------------------------------------------------------

const IS_64 = builtin.cpu.arch == .x86_64;

/// Matches phys_addr_t / uptr in arch.h:
///   x86    → u32
///   x86_64 → u64
pub const PhysAddr = if (IS_64) u64 else u32;
pub const UPtr     = PhysAddr;

// ---------------------------------------------------------------------------
// Configuration  —  matches PAGE_SIZE in include/ark/arch.h
// ---------------------------------------------------------------------------

pub const PAGE_SIZE:  UPtr  = 4096;
pub const PAGE_SHIFT: u5    = 12;

/// Max buddy order.
/// Order 0 = 4 KiB ... Order 10 = 4 MiB (x86) / Order 11 = 8 MiB (x86_64)
pub const MAX_ORDER: usize = if (IS_64) 12 else 11;

/// The shift amount type must exactly match the bit-width of UPtr:
///   u32 shift needs u5  (max shift = 31)
///   u64 shift needs u6  (max shift = 63)
/// Resolving this at comptime avoids the "u5 cannot represent u6" error.
const ShiftT = if (IS_64) u6 else u5;

pub inline fn blockSize(order: usize) UPtr {
    return PAGE_SIZE << @as(ShiftT, @intCast(order));
}

// ---------------------------------------------------------------------------
// Multiboot memory map  —  mirrors include/ark/multiboot.h exactly
//
// multiboot_info_t field layout (all u32, packed, no padding):
//   offset  0  flags
//   offset  4  mem_lower
//   offset  8  mem_upper
//   offset 12  boot_device
//   offset 16  cmdline
//   offset 20  mods_count
//   offset 24  mods_addr
//   offset 28  num    ─┐
//   offset 32  size    ├─ ELF/a.out symbol table union (4 x u32)
//   offset 36  addr    │
//   offset 40  shndx  ─┘
//   offset 44  mmap_length
//   offset 48  mmap_addr
// ---------------------------------------------------------------------------

const MultibootInfo = extern struct {
    flags:       u32,
    mem_lower:   u32,
    mem_upper:   u32,
    boot_device: u32,
    cmdline:     u32,
    mods_count:  u32,
    mods_addr:   u32,
    syms:        [4]u32,
    mmap_length: u32,
    mmap_addr:   u32,
};

comptime {
    if (@offsetOf(MultibootInfo, "mmap_length") != 44)
        @compileError("MultibootInfo.mmap_length not at offset 44 — sync with multiboot.h");
    if (@offsetOf(MultibootInfo, "mmap_addr") != 48)
        @compileError("MultibootInfo.mmap_addr not at offset 48 — sync with multiboot.h");
}

/// multiboot_mmap_entry_t  (__attribute__((packed)) in multiboot.h)
const MmapEntry = extern struct {
    size:   u32,
    addr:   u64 align(1),
    length: u64 align(1),
    kind:   u32,
};

// ---------------------------------------------------------------------------
// Intrusive free-block node  (stored at the top of every free block)
// ---------------------------------------------------------------------------

const FreeNode = extern struct {
    next: ?*FreeNode,
};

comptime {
    if (@sizeOf(FreeNode) > PAGE_SIZE)
        @compileError("FreeNode does not fit inside one page");
}

// ---------------------------------------------------------------------------
// Per-order free stack
// ---------------------------------------------------------------------------

const FreeStack = struct {
    head:  ?*FreeNode = null,
    count: u32        = 0,

    fn push(self: *FreeStack, phys: PhysAddr) void {
        const node: *FreeNode = @ptrFromInt(phys);
        node.next  = self.head;
        self.head  = node;
        self.count += 1;
    }

    fn pop(self: *FreeStack) ?PhysAddr {
        const node = self.head orelse return null;
        self.head   = node.next;
        node.next   = null;
        self.count -= 1;
        return @intFromPtr(node);
    }

    fn isEmpty(self: *const FreeStack) bool {
        return self.head == null;
    }

    fn remove(self: *FreeStack, target: PhysAddr) bool {
        var prev: ?*FreeNode = null;
        var cur               = self.head;
        while (cur) |node| {
            if (@intFromPtr(node) == target) {
                if (prev) |p| p.next = node.next
                else       self.head = node.next;
                node.next   = null;
                self.count -= 1;
                return true;
            }
            prev = node;
            cur  = node.next;
        }
        return false;
    }
};

// ---------------------------------------------------------------------------
// Block scanner  —  buddy allocator over Multiboot memory regions
// ---------------------------------------------------------------------------

pub const BlockScanner = struct {
    stacks:            [MAX_ORDER]FreeStack = [_]FreeStack{.{}} ** MAX_ORDER,
    total_free_bytes:  UPtr                 = 0,
    total_free_blocks: u32                  = 0,

    pub fn scanMultiboot(
        self:         *BlockScanner,
        mbi:          *const MultibootInfo,
        kernel_start: PhysAddr,
        kernel_end:   PhysAddr,
    ) void {
        if ((mbi.flags & (1 << 6)) == 0) return;

        var cursor: u32 = mbi.mmap_addr;
        const map_end: u32 = mbi.mmap_addr + mbi.mmap_length;

        while (cursor < map_end) {
            const entry: *const MmapEntry = @ptrFromInt(cursor);
            if (entry.kind == 1) {
                const base = @as(PhysAddr, @truncate(entry.addr));
                const len  = @as(PhysAddr, @truncate(entry.length));
                if (len > 0)
                    self.scanRegionExclude(base, len, kernel_start, kernel_end);
            }
            cursor += entry.size + @sizeOf(u32);
        }
    }

    pub fn scanRegionExclude(
        self:       *BlockScanner,
        base:       PhysAddr,
        length:     PhysAddr,
        excl_start: PhysAddr,
        excl_end:   PhysAddr,
    ) void {
        const LOW1M: PhysAddr = 0x100000;
        const eff_base   = if (base < LOW1M) LOW1M else base;
        const region_end = base + length;
        if (eff_base >= region_end) return;

        if (excl_end <= eff_base or excl_start >= region_end) {
            self.scanRegion(eff_base, region_end - eff_base);
            return;
        }
        if (excl_start > eff_base)
            self.scanRegion(eff_base, excl_start - eff_base);
        if (excl_end < region_end)
            self.scanRegion(excl_end, region_end - excl_end);
    }

    pub fn scanRegion(self: *BlockScanner, base: PhysAddr, length: PhysAddr) void {
        const rem_base = base;
        var rem_len    = length;

        var order: usize = MAX_ORDER;
        while (order > 0) {
            order -= 1;
            const size = blockSize(order);
            if (rem_len < size) continue;

            const aligned = alignUp(rem_base, size);
            if (aligned >= rem_base + rem_len) continue;

            if (aligned > rem_base)
                self.scanRegion(rem_base, aligned - rem_base);

            const avail    = rem_base + rem_len - aligned;
            const n_blocks = avail / size;

            var i: UPtr = 0;
            while (i < n_blocks) : (i += 1)
                self.pushFree(aligned + i * size, order);

            const tail = aligned + n_blocks * size;
            if (tail < rem_base + rem_len)
                self.scanRegion(tail, rem_base + rem_len - tail);

            rem_len = 0;
            break;
        }
    }

    pub fn alloc(self: *BlockScanner, order: usize) PhysAddr {
        if (order >= MAX_ORDER) return 0;

        if (self.stacks[order].pop()) |addr| {
            self.total_free_bytes  -= blockSize(order);
            self.total_free_blocks -= 1;
            return addr;
        }

        var hi = order + 1;
        while (hi < MAX_ORDER) : (hi += 1) {
            if (!self.stacks[hi].isEmpty())
                return self.splitDown(hi, order);
        }
        return 0;
    }

    pub fn free(self: *BlockScanner, addr: PhysAddr, order: usize) void {
        if (order >= MAX_ORDER) return;
        self.total_free_bytes  += blockSize(order);
        self.total_free_blocks += 1;
        self.coalesce(addr, order);
    }

    pub fn freeBytes(self: *const BlockScanner) UPtr { return self.total_free_bytes; }
    pub fn freeBlocks(self: *const BlockScanner) u32  { return self.total_free_blocks; }

    pub fn freeBlocksAtOrder(self: *const BlockScanner, order: usize) u32 {
        return if (order < MAX_ORDER) self.stacks[order].count else 0;
    }

    inline fn pushFree(self: *BlockScanner, addr: PhysAddr, order: usize) void {
        self.stacks[order].push(addr);
        self.total_free_bytes  += blockSize(order);
        self.total_free_blocks += 1;
    }

    fn splitDown(self: *BlockScanner, src: usize, tgt: usize) PhysAddr {
        const addr = self.stacks[src].pop() orelse return 0;
        self.total_free_bytes  -= blockSize(src);
        self.total_free_blocks -= 1;

        const child      = src - 1;
        const buddy_addr = addr + blockSize(child);
        self.pushFree(buddy_addr, child);

        if (child == tgt) return addr;

        self.pushFree(addr, child);
        return self.splitDown(child, tgt);
    }

    fn coalesce(self: *BlockScanner, addr: PhysAddr, order: usize) void {
        if (order + 1 >= MAX_ORDER) {
            self.stacks[order].push(addr);
            return;
        }
        const buddy = buddyOf(addr, order);
        if (self.stacks[order].remove(buddy)) {
            self.total_free_bytes  -= blockSize(order);
            self.total_free_blocks -= 1;
            const merged = if (addr < buddy) addr else buddy;
            self.coalesce(merged, order + 1);
        } else {
            self.stacks[order].push(addr);
        }
    }
};

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------

pub inline fn buddyOf(addr: PhysAddr, order: usize) PhysAddr {
    return addr ^ blockSize(order);
}
pub inline fn alignUp(v: UPtr, alignment: UPtr) UPtr {
    return (v + alignment - 1) & ~(alignment - 1);
}
pub inline fn alignDown(v: UPtr, alignment: UPtr) UPtr {
    return v & ~(alignment - 1);
}

// ---------------------------------------------------------------------------
// Global instance + C ABI exports
// ---------------------------------------------------------------------------

var g_scanner: BlockScanner = .{};

export fn ark_bscan_init(
    mbi:          *const MultibootInfo,
    kernel_start: PhysAddr,
    kernel_end:   PhysAddr,
) void {
    g_scanner = .{};
    g_scanner.scanMultiboot(mbi, kernel_start, kernel_end);
}

export fn ark_bscan_alloc(order: u32) PhysAddr {
    return g_scanner.alloc(@intCast(order));
}

export fn ark_bscan_free(addr: PhysAddr, order: u32) void {
    g_scanner.free(addr, @intCast(order));
}

export fn ark_bscan_free_bytes() PhysAddr {
    return g_scanner.freeBytes();
}

export fn ark_bscan_free_blocks() u32 {
    return g_scanner.freeBlocks();
}

export fn ark_bscan_free_blocks_at_order(order: u32) u32 {
    return g_scanner.freeBlocksAtOrder(@intCast(order));
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

const testing = @import("std").testing;

test "multiboot offsets" {
    try testing.expectEqual(@as(usize, 44), @offsetOf(MultibootInfo, "mmap_length"));
    try testing.expectEqual(@as(usize, 48), @offsetOf(MultibootInfo, "mmap_addr"));
}
test "alignUp / alignDown" {
    try testing.expectEqual(@as(UPtr, 4096), alignUp(1, 4096));
    try testing.expectEqual(@as(UPtr, 4096), alignUp(4096, 4096));
    try testing.expectEqual(@as(UPtr, 8192), alignUp(4097, 4096));
    try testing.expectEqual(@as(UPtr, 0),    alignDown(4095, 4096));
}
test "buddyOf" {
    try testing.expectEqual(@as(PhysAddr, 0x1000), buddyOf(0x0000, 0));
    try testing.expectEqual(@as(PhysAddr, 0x0000), buddyOf(0x1000, 0));
}
test "blockSize shift type" {
    try testing.expectEqual(@as(UPtr, 4096),     blockSize(0));
    try testing.expectEqual(@as(UPtr, 0x400000), blockSize(10));
}
test "FreeStack" {
    var buf0: [PAGE_SIZE]u8 align(4096) = undefined;
    var buf1: [PAGE_SIZE]u8 align(4096) = undefined;
    const a0: PhysAddr = @intFromPtr(&buf0);
    const a1: PhysAddr = @intFromPtr(&buf1);
    var s = FreeStack{};
    s.push(a0); s.push(a1);
    try testing.expectEqual(@as(u32, 2), s.count);
    try testing.expect(s.remove(a0));
    try testing.expectEqual(a1, s.pop().?);
    try testing.expect(s.isEmpty());
}
test "BlockScanner scan + alloc + free" {
    var sc = BlockScanner{};
    sc.scanRegion(0x200000, 0x200000);
    const before = sc.freeBytes();
    const p = sc.alloc(0);
    try testing.expect(p != 0);
    try testing.expectEqual(before - PAGE_SIZE, sc.freeBytes());
    sc.free(p, 0);
    try testing.expectEqual(before, sc.freeBytes());
}
test "BlockScanner kernel exclusion" {
    var sc = BlockScanner{};
    sc.scanRegionExclude(0x100000, 0x400000, 0x200000, 0x201000);
    try testing.expectEqual(@as(UPtr, 0x400000 - PAGE_SIZE), sc.freeBytes());
}
