/*
 * arch/x86_64/psf_font.S — embeds font directly into the 64-bit binary
 */

.section .rodata, "a"

.global _psf_font_start
.global _psf_font_end

.align 16
_psf_font_start:
    .incbin "gen/font/font.psf"
_psf_font_end:

/* Explicitly mark these as objects for the 64-bit linker */
.type _psf_font_start, @object
.type _psf_font_end, @object
.size _psf_font_start, . - _psf_font_start
