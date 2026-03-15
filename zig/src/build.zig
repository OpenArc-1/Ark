// zig/src/build.zig
//
// Used by `zig build` for development/testing.
// The kernel Makefile calls `zig build-lib` directly — this file is
// only needed when you want `zig build test` or IDE integration.
//
// Usage:
//   zig build                        # build lib for host arch (dev only)
//   zig build test                   # run unit tests
//   zig build -Darch=x86_64          # build for x86_64 kernel target
//   zig build -Darch=x86             # build for x86 kernel target

const std = @import("std");

pub fn build(b: *std.Build) void {
    // ── Options ────────────────────────────────────────────────────────────
    const arch_opt = b.option(
        []const u8,
        "arch",
        "Target architecture: x86 or x86_64 (default: native)",
    ) orelse "native";

    // ── Resolve target ─────────────────────────────────────────────────────
    const query: std.Target.Query = blk: {
        if (std.mem.eql(u8, arch_opt, "x86_64")) {
            break :blk .{
                .cpu_arch = .x86_64,
                .os_tag   = .freestanding,
                .abi      = .none,
            };
        } else if (std.mem.eql(u8, arch_opt, "x86")) {
            break :blk .{
                .cpu_arch = .x86,
                .os_tag   = .freestanding,
                .abi      = .none,
            };
        } else {
            // Native — useful for running tests on the dev machine
            break :blk .{};
        }
    };

    const target   = b.resolveTargetQuery(query);
    const optimize = b.standardOptimizeOption(.{});

    // ── Static library ─────────────────────────────────────────────────────
    const lib = b.addStaticLibrary(.{
        .name   = "ark_zig",
        .root_source_file = b.path("lib.zig"),
        .target = target,
        .optimize = optimize,
    });
    // Freestanding: no libc, no std runtime
    lib.bundle_compiler_rt = false;
    b.installArtifact(lib);

    // ── Unit tests ─────────────────────────────────────────────────────────
    // Tests run on the host (native target) so they can use the std
    // testing harness even though the kernel is freestanding.
    const tests = b.addTest(.{
        .root_source_file = b.path("lib.zig"),
        .target           = b.resolveTargetQuery(.{}),  // always native
        .optimize         = optimize,
    });

    const run_tests = b.addRunArtifact(tests);
    const test_step = b.step("test", "Run block scanner unit tests");
    test_step.dependOn(&run_tests.step);
}
