#include "ark/types.h"
#include "ark/time.h"
#define ARK_IO_INLINE
#include "../io/built-in.h"

/* CMOS/RTC I/O ports */
#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

u8 cmos_read(u8 reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

u8 bcd_to_bin(u8 val) {
    return (val & 0x0F) + ((val >> 4) * 10);
}

/* read_rtc - read current time from the hardware RTC via CMOS */
rtc_time_t read_rtc(void) {
    rtc_time_t t;

    /* Wait until update-in-progress flag clears */
    while (cmos_read(0x0A) & 0x80);

    t.sec   = cmos_read(0x00);
    t.min   = cmos_read(0x02);
    t.hour  = cmos_read(0x04);
    t.day   = cmos_read(0x07);
    t.month = cmos_read(0x08);
    t.year  = cmos_read(0x09);

    /* Convert BCD to binary if needed */
    u8 regB = cmos_read(0x0B);
    if (!(regB & 0x04)) {
        t.sec   = bcd_to_bin(t.sec);
        t.min   = bcd_to_bin(t.min);
        t.hour  = bcd_to_bin(t.hour);
        t.day   = bcd_to_bin(t.day);
        t.month = bcd_to_bin(t.month);
        t.year  = bcd_to_bin(t.year);
    }

    t.year += 2000;
    return t;
}
