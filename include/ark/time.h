#ifndef TIME_H
#define TIME_H

#include <stdint.h>

// CMOS I/O ports
#define CMOS_ADDR 0x70
#define CMOS_DATA 0x71

// RTC Registers
#define RTC_SECONDS      0x00
#define RTC_MINUTES      0x02
#define RTC_HOURS        0x04
#define RTC_DAY          0x07
#define RTC_MONTH        0x08
#define RTC_YEAR         0x09
#define RTC_STATUS_A     0x0A
#define RTC_STATUS_B     0x0B

// RTC time structure
typedef struct {
    uint8_t sec;
    uint8_t min;
    uint8_t hour;
    uint8_t day;
    uint8_t month;
    uint16_t year;
} rtc_time_t;

// Function prototypes

/**
 * Read a byte from CMOS register.
 */
uint8_t cmos_read(uint8_t reg);

/**
 * Convert BCD value to binary.
 */
uint8_t bcd_to_bin(uint8_t val);

/**
 * Read the current time from the RTC.
 */
rtc_time_t read_rtc();

/**
 * Optional: print current time using kernel printk.
 */
void print_time();

#endif // TIME_H
