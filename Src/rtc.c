/**
 * @file    rtc.c
 * @brief   On-chip RTC driver (LSE 32.768kHz crystal, battery-backed via VBAT)
 *
 * STM32F103 RTC: 32-bit seconds counter. First boot: time is set from the
 * compiler __DATE__/__TIME__; afterwards the VBAT battery keeps it running.
 */
#include <stdio.h>
#include <string.h>
#include "rtc.h"

RTC_HandleTypeDef hrtc;

/* days since 2000-01-01 (valid 2000..2099) */
static uint16_t days_since_2000(uint16_t y, uint8_t m, uint8_t d)
{
    static const uint8_t mdays[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    uint16_t days = 0;
    uint16_t yy;

    for (yy = 2000; yy < y; yy++)
    {
        days += (yy % 4 == 0) ? 366 : 365;
    }
    for (yy = 1; yy < m; yy++)
    {
        days += mdays[yy - 1] + ((yy == 2 && y % 4 == 0) ? 1 : 0);
    }
    days += d - 1;
    return days;
}

/* weekday: 1=Mon .. 7=Sun (2000-01-01 was a Saturday) */
static uint8_t date_to_weekday(uint16_t y, uint8_t m, uint8_t d)
{
    return (uint8_t)(((days_since_2000(y, m, d) + 5) % 7) + 1);
}

uint8_t rtc_init(void)
{
    __HAL_RCC_PWR_CLK_ENABLE();
    SET_BIT(PWR->CR, PWR_CR_DBP);           /* backup domain access (= __HAL_PWR_ENABLE_BKUPACCESS) */

    /* LSE 32.768kHz crystal, wait until stable */
    SET_BIT(RCC->BDCR, RCC_BDCR_LSEON);     /* LSE on (= __HAL_RCC_LSE_ENABLE) */
    while (__HAL_RCC_GET_FLAG(RCC_FLAG_LSERDY) == RESET) { }

    /* RTC clock source = LSE */
    __HAL_RCC_RTC_CONFIG(RCC_RTCCLKSOURCE_LSE);
    __HAL_RCC_RTC_ENABLE();

    hrtc.Instance = RTC;
    hrtc.Init.AsynchPrediv = RTC_AUTO_1_SECOND;   /* auto 1 Hz timebase */
    hrtc.Init.OutPut = RTC_OUTPUTSOURCE_NONE;
    return (HAL_RTC_Init(&hrtc) == HAL_OK) ? 0 : 1;
}

void rtc_get_time(RtcTime *t)
{
    RTC_DateTypeDef d;
    RTC_TimeTypeDef tm;

    /* HAL quirk: read TIME first, then DATE (date read latches the time) */
    HAL_RTC_GetTime(&hrtc, &tm, RTC_FORMAT_BIN);
    HAL_RTC_GetDate(&hrtc, &d, RTC_FORMAT_BIN);

    t->year = (uint16_t)(2000 + d.Year);
    t->month = d.Month;
    t->day = d.Date;
    t->weekday = d.WeekDay;
    t->hour = tm.Hours;
    t->minute = tm.Minutes;
    t->second = tm.Seconds;
}

void rtc_set_time(const RtcTime *t)
{
    RTC_DateTypeDef d;
    RTC_TimeTypeDef tm;

    d.Year = (uint8_t)(t->year - 2000);
    d.Month = t->month;
    d.Date = t->day;
    d.WeekDay = date_to_weekday(t->year, t->month, t->day);
    HAL_RTC_SetDate(&hrtc, &d, RTC_FORMAT_BIN);

    tm.Hours = t->hour;
    tm.Minutes = t->minute;
    tm.Seconds = t->second;
    HAL_RTC_SetTime(&hrtc, &tm, RTC_FORMAT_BIN);
}

void rtc_set_from_build_time_if_needed(void)
{
    RtcTime now;
    unsigned yy, dd, hh, mi, ss;
    char mon[4];
    uint8_t i;
    static const char *months[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                     "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};

    rtc_get_time(&now);
    if (now.year >= 2024 && now.year <= 2099)
    {
        return;     /* battery kept a valid time */
    }

    /* parse compiler build timestamp as initial time */
    sscanf(__DATE__, "%3s %u %u", mon, &dd, &yy);
    sscanf(__TIME__, "%u:%u:%u", &hh, &mi, &ss);
    now.year = (uint16_t)yy;
    now.month = 1;
    for (i = 0; i < 12; i++)
    {
        if (strncmp(mon, months[i], 3) == 0) { now.month = i + 1; break; }
    }
    now.day = (uint8_t)dd;
    now.hour = (uint8_t)hh;
    now.minute = (uint8_t)mi;
    now.second = (uint8_t)ss;
    now.weekday = date_to_weekday(now.year, now.month, now.day);
    rtc_set_time(&now);
    printf("[RTC] first boot, set from build time: %04u-%02u-%02u %02u:%02u:%02u\r\n",
           (unsigned)now.year, (unsigned)now.month, (unsigned)now.day,
           (unsigned)now.hour, (unsigned)now.minute, (unsigned)now.second);
}
