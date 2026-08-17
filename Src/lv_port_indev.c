/**
 * @file    lv_port_indev.c
 * @brief   LVGL 输入设备接口 (XPT2046/ADS7843 电阻触摸)
 *
 * LVGL 每 30ms 自动调用 read_cb 读取触摸状态。
 * 坐标来自 tp_scan(0) —— 屏幕坐标模式(带 24C02 出厂校准映射)。
 *
 * 若触摸方向不对, 调整下面三个开关:
 *   TP_SWAP_XY  : X/Y 互换
 *   TP_MIRROR_X : 水平镜像
 *   TP_MIRROR_Y : 垂直镜像
 */
#include <stdio.h>
#include "lvgl.h"
#include "lv_port_indev.h"
#include "touch.h"
#include "lcd.h"
#include "cmsis_os.h"   /* osKernelGetTickCount (tick source for LVGL) */

#define TP_SWAP_XY   0
#define TP_MIRROR_X  0
#define TP_MIRROR_Y  0

/* LVGL 输入回调: LVGL 周期性调用 */
static void touchpad_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;

    /* 未校准时不给坐标(避免除零); 校准则由 main() 预读 24C02 完成 */
    if (!g_tp_calib_ok)
    {
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }

    /* 注意: tp_scan() 返回值是 uint8_t, 但 return 的是 0x8000 (sta & TP_PRES_DOWN),
     * 截断后恒为 0 —— 官方例程也只查 sta, 不用返回值! */
    tp_scan(0);   /* 屏幕坐标模式(校准映射) */

    if (tp_dev.sta & TP_PRES_DOWN)
    {
        uint16_t x = tp_dev.x[0];
        uint16_t y = tp_dev.y[0];
#if TP_SWAP_XY
        uint16_t t = x; x = y; y = t;
#endif
#if TP_MIRROR_X
        x = (uint16_t)(lcddev.width - 1 - x);
#endif
#if TP_MIRROR_Y
        y = (uint16_t)(lcddev.height - 1 - y);
#endif
        data->point.x = x;
        data->point.y = y;
        data->state = LV_INDEV_STATE_PRESSED;
    }
    else
    {
        data->state = LV_INDEV_STATE_RELEASED;
    }
}

void lv_port_indev_init(void)
{
    static lv_indev_drv_t indev_drv;

    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = touchpad_read_cb;
    lv_indev_drv_register(&indev_drv);
}
