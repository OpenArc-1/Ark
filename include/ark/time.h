#ifndef TIME_H
#define TIME_H

/* Use ark's own types instead of <stdint.h> so this header works in
 * both kernel code and freestanding userspace (init.bin / ark-gcc). */
#include "ark/types.h"

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
    u8  sec;
    u8  min;
    u8  hour;
    u8  day;
    u8  month;
    u16 year;
} rtc_time_t;

// Function prototypes
u8         cmos_read(u8 reg);
u8         bcd_to_bin(u8 val);
rtc_time_t read_rtc(void);
void       print_time(void);

#endif // TIME_H
