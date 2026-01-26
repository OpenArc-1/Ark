#include <stdint.h>
#include "../io/built-in.h" // you'll need functions to read/write ports

// CMOS I/O ports
#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

// read a byte from CMOS
uint8_t cmos_read(uint8_t reg) {
    outb(CMOS_ADDR, reg);
    return inb(CMOS_DATA);
}

// convert BCD to binary if needed
uint8_t bcd_to_bin(uint8_t val) {
    return (val & 0x0F) + ((val >> 4) * 10);
}

typedef struct {
    uint8_t sec;
    uint8_t min;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} rtc_time_t;

rtc_time_t read_rtc() {
    rtc_time_t t;

    // wait until update-in-progress flag is clear
    while (cmos_read(0x0A) & 0x80);

    t.sec   = cmos_read(0x00);
    t.min   = cmos_read(0x02);
    t.hour  = cmos_read(0x04);
    t.day   = cmos_read(0x07);
    t.month = cmos_read(0x08);
    t.year  = cmos_read(0x09);

    // check if RTC is in BCD mode
    uint8_t regB = cmos_read(0x0B);
    if (!(regB & 0x04)) { // BCD mode
        t.sec   = bcd_to_bin(t.sec);
        t.min   = bcd_to_bin(t.min);
        t.hour  = bcd_to_bin(t.hour);
        t.day   = bcd_to_bin(t.day);
        t.month = bcd_to_bin(t.month);
        t.year  = bcd_to_bin(t.year);
    }

    t.year += 2000; // assume 21st century

    return t;
}
