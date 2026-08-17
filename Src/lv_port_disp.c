/**
 ******************************************************************************
 * @file    lv_port_disp.c
 * @brief   LVGL display interface port (FSMC LCD)
 *
 * Render path: LVGL renders to partial buffer -> disp_flush -> FSMC write
 * Buffer: 320x16 pixels (RGB565), ~10.2KB (bigger = fewer flush chunks, smoother anim)
 * Color: 16bit RGB565
 ******************************************************************************
 */
#include "lv_port_disp.h"
#include "lcd.h"
#include <stdio.h>

#define DISP_BUF_SIZE   (320 * 16)   /* one flush up to 320x16 px */

static lv_disp_draw_buf_t draw_buf;
static lv_color_t buf_1[DISP_BUF_SIZE];

/* LVGL renders a dirty area here -> write to LCD via FSMC */
static void disp_flush(lv_disp_drv_t *disp_drv, const lv_area_t *area, lv_color_t *color_p)
{
    uint32_t w = lv_area_get_width(area);
    uint32_t h = lv_area_get_height(area);
    uint32_t i, size = w * h;

    lcd_set_window(area->x1, area->y1, w, h);
    lcd_write_ram_prepare();
    for (i = 0; i < size; i++)
    {
        lcd_wr_data(color_p[i].full);
    }
    lv_disp_flush_ready(disp_drv);
}

void lv_port_disp_init(void)
{
    static lv_disp_drv_t disp_drv;

    lv_disp_draw_buf_init(&draw_buf, buf_1, NULL, DISP_BUF_SIZE);

    lv_disp_drv_init(&disp_drv);
    printf("[LVGL] lcd %ux%u id=0x%X\r\n", (unsigned)lcddev.width, (unsigned)lcddev.height, (unsigned)lcddev.id);
    disp_drv.hor_res = lcddev.width;
    disp_drv.ver_res = lcddev.height;
    disp_drv.flush_cb = disp_flush;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);
}
