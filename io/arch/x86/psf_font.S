/*
 * psf_font.S — embeds gen/font/font.psf directly into the kernel binary.
 *
 * Exposes two symbols:
 *   _psf_font_start  — pointer to the raw PSF1 file bytes
 *   _psf_font_end    — one byte past the end (end - start = file size)
 *
 * printk.c reads these to locate glyph bitmaps at runtime without any
 * file I/O — the font is baked into bzImage.
 */

    .section .rodata, "a"

    .global _psf_font_start
    .global _psf_font_end

_psf_font_start:
    .incbin "gen/font/font.psf"
_psf_font_end:
