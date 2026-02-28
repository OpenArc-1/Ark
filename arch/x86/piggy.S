/*
 * piggy.S - placeholder assembly helpers
 *
 * entry/exit sequences, and architecture-specific glue.  Ark doesn't
 * really need anything special yet, but having a dedicated file called
 * "piggy.S" gives you a convenient place to drop any low‑level
 * assembly routines that should be visible to the rest of the kernel.
 *
 * The build system automatically compiles all .S files under arch/$(ARCH),
 * so simply adding functions here makes them available to the linker.
 */

    .section .text
    .global piggy_stub
piggy_stub:
    ret
