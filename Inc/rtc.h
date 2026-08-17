/**
 * @file    rtc.h
 * @brief   On-chip RTC driver (LSE 32.768kHz crystal, VBAT keeps time in power-off)
 */
#ifndef __RTC_H
#define __RTC_H

#include "main.h"

typedef struct
{
    uint16_t year;      /* e.g. 2026 */
    uint8_t  month;     /* 1..12 */
    uint8_t  day;       /* 1..31 */
    uint8_t  hour;      /* 0..23 */
    uint8_t  minute;    /* 0..59 */
    uint8_t  second;    /* 0..59 */
    uint8_t  weekday;   /* 1=Mon .. 7=Sun */
} RtcTime;

/* Init LSE + RTC (1Hz counter). Returns 0 = OK, 1 = LSE not ready */
uint8_t rtc_init(void);

/* Read current time into t */
void rtc_get_time(RtcTime *t);

/* Set time (writes RTC registers, takes effect immediately) */
void rtc_set_time(const RtcTime *t);

/* First-boot init: set time from compiler __DATE__/__TIME__ if RTC year looks invalid */
void rtc_set_from_build_time_if_needed(void);

#endif /* __RTC_H */
