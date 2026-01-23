#ifndef ARK_IO_H
#define ARK_IO_H

#include <stdint.h>

typedef uint8_t  u8;
typedef uint16_t u16;
typedef uint32_t u32;

/* Input from ports */
u8  inb(u16 port);
u16 inw(u16 port);
u32 inl(u16 port);

/* Output to ports */
void outb(u16 port, u8 value);
void outw(u16 port, u16 value);
void outl(u16 port, u32 value);

/* Small delay */
void io_wait(void);

#endif
