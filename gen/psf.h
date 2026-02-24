#ifndef PSF_H
#define PSF_H

#include <stdint.h>

/* Call once at boot before vga_load_font() */
int  font_init(void);

/* Upload embedded PSF1 font into VGA plane 2 */
void vga_load_font(void);

#endif /* PSF_H */