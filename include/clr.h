//this file is out of any file is because its not for any specific item

#ifndef CLR_H
#define CLR_H
#include "ark/types.h"

/* * VGA 4-bit Color Indices
 * Standard intensity (0-7) and High intensity (8-15)
 */

// Standard Colors
#define CLR_BLACK         0x00
#define CLR_BLUE          0x01
#define CLR_GREEN         0x02
#define CLR_CYAN          0x03
#define CLR_RED           0x04
#define CLR_MAGENTA       0x05
#define CLR_BROWN         0x06
#define CLR_LIGHTGRAY     0x07

// High Intensity Colors
#define CLR_GRAY          0x08
#define CLR_LIGHTBLUE     0x09
#define CLR_LIGHTGREEN    0x0A
#define CLR_LIGHTCYAN     0x0B
#define CLR_LIGHTRED      0x0C
#define CLR_LIGHTMAGENTA  0x0D
#define CLR_YELLOW        0x0E
#define CLR_WHITE         0x0F

/*
 * Helper Macro for Text Mode Attribute Bytes
 * usage: u8 attr = CLR_ATTR(CLR_BLUE, CLR_LIGHTRED);
 * Result: 0x1C (Light Red text on Blue background)
 */
#define CLR_ATTR(bg, fg) (u8)(((bg & 0x0F) << 4) | (fg & 0x0F))

#endif // CLR_H