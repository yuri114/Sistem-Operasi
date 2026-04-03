/* rtc.h — CMOS Real-Time Clock (RTC) driver.
 * Membaca waktu/tanggal saat ini dari chip RTC PC standar (port 0x70/0x71).
 * Format BCD (Binary Coded Decimal) dikonversi ke binary. */
#ifndef RTC_H
#define RTC_H
#include <stdint.h>

typedef struct {
    uint8_t  second;  /* 0-59  */
    uint8_t  minute;  /* 0-59  */
    uint8_t  hour;    /* 0-23  */
    uint8_t  day;     /* 1-31  */
    uint8_t  month;   /* 1-12  */
    uint16_t year;    /* misal 2024 */
} RtcTime;

void rtc_read(RtcTime *t);   /* baca waktu dari CMOS RTC */

#endif
