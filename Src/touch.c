/**
 ****************************************************************************************************
 * @file        touch.c
 * @author      ALIENTEK, adapted for this project
 * @version     V1.1 (resistive-only branch)
 * @date        2023-06-01
 * @brief       Resistive touch screen driver (XPT2046 / ADS7843 / TSC2046)
 *   @note      Original: ALIENTEK 实验26 触摸屏实验. Capacitive branches removed,
 *              calibration cache flag (g_tp_calib_ok) added for pre-scheduler load.
 ****************************************************************************************************
 */

#include <stdio.h>
#include <stdlib.h>
#include "lcd.h"
#include "touch.h"
#include "24cxx.h"
#include "main.h"


_m_tp_dev tp_dev =
{
    tp_init,
    tp_scan,
    tp_adjust,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
    0,
};

/* Calibration cache flag: set by main() pre-scheduler read, avoids re-reading
 * 24C02 from a task (PB6/PB7 shared with SHT30 soft I2C -> bus race) */
uint8_t g_tp_calib_ok = 0;



/**
 * @brief   SPI: write one byte to touch controller
 */
static void tp_write_byte(uint8_t data)
{
    uint8_t count = 0;

    for (count = 0; count < 8; count++)
    {
        if (data & 0x80)    
        {
            T_MOSI(1);
        }
        else                
        {
            T_MOSI(0);
        }

        data <<= 1;
        T_CLK(0);
        delay_us(1);
        T_CLK(1);           /**
 * @brief   SPI: read raw ADC value (12bit) from touch controller
 */
    }
}

/**
 * @brief   SPI: read raw ADC value (12bit) from touch controller
 */
static uint16_t tp_read_ad(uint8_t cmd)
{
    uint8_t count = 0;
    uint16_t num = 0;
    T_CLK(0);           
    T_MOSI(0);          /**
 * @brief   SPI: write one byte to touch controller
 */
    T_CS(0);            /**
 * @brief   SPI: write one byte to touch controller
 */
    tp_write_byte(cmd); 
    delay_us(6);        /* ADS7846 6us */
    T_CLK(0);
    delay_us(1);
    T_CLK(1);           /* BUSY */
    delay_us(1);
    T_CLK(0);

    for (count = 0; count < 16; count++)    /* 16 12 */
    {
        num <<= 1;
        T_CLK(0);       
        delay_us(1);
        T_CLK(1);

        if (T_MISO)num++;
    }

    num >>= 4;          /* 12 */
    T_CS(1);            /* sampling + median filter params */
    return num;
}

/* sampling + median filter params */
#define TP_READ_TIMES   5       
#define TP_LOST_VAL     1       /**
 * @brief   Read one axis raw AD with 5-sample median filter (drop min/max)
 */

/**
 * @brief   Read one axis raw AD with 5-sample median filter (drop min/max)
 */
static uint16_t tp_read_xoy(uint8_t cmd)
{
    uint16_t i, j;
    uint16_t buf[TP_READ_TIMES];
    uint16_t sum = 0;
    uint16_t temp;

    for (i = 0; i < TP_READ_TIMES; i++)   /**
 * @brief   SPI: read raw ADC value (12bit) from touch controller
 */
    {
        buf[i] = tp_read_ad(cmd);
    }

    for (i = 0; i < TP_READ_TIMES - 1; i++)   
    {
        for (j = i + 1; j < TP_READ_TIMES; j++)
        {
            if (buf[i] > buf[j])   
            {
                temp = buf[i];
                buf[i] = buf[j];
                buf[j] = temp;
            }
        }
    }

    sum = 0;

    for (i = TP_LOST_VAL; i < TP_READ_TIMES - TP_LOST_VAL; i++)   
    {
        sum += buf[i];  
    }

    temp = sum / (TP_READ_TIMES - 2 * TP_LOST_VAL); /**
 * @brief   Read X/Y raw AD values (X/Y order per touchtype bit0)
 */
    return temp;
}

/**
 * @brief   Read X/Y raw AD values (X/Y order per touchtype bit0)
 */
static void tp_read_xy(uint16_t *x, uint16_t *y)
{
    uint16_t xval, yval;

    if (tp_dev.touchtype & 0X01)    /**
 * @brief   Read one axis raw AD with 5-sample median filter (drop min/max)
 */
    {
        xval = tp_read_xoy(0X90);   /**
 * @brief   Read one axis raw AD with 5-sample median filter (drop min/max)
 */
        yval = tp_read_xoy(0XD0);   /* AD */
    }
    else                            /**
 * @brief   Read one axis raw AD with 5-sample median filter (drop min/max)
 */
    {
        xval = tp_read_xoy(0XD0);   /**
 * @brief   Read one axis raw AD with 5-sample median filter (drop min/max)
 */
        yval = tp_read_xoy(0X90);   /* AD */
    }

    *x = xval;
    *y = yval;
}

/* two-read consistency threshold */
#define TP_ERR_RANGE    50      /**
 * @brief   Read twice, accept if diff < TP_ERR_RANGE, return average
 */

/**
 * @brief   Read twice, accept if diff < TP_ERR_RANGE, return average
 */
static uint8_t tp_read_xy2(uint16_t *x, uint16_t *y)
{
    uint16_t x1, y1;
    uint16_t x2, y2;

    tp_read_xy(&x1, &y1);   /**
 * @brief   Read X/Y raw AD values (X/Y order per touchtype bit0)
 */
    tp_read_xy(&x2, &y2);   

    /* TP_ERR_RANGE */
    if (((x2 <= x1 && x1 < x2 + TP_ERR_RANGE) || (x1 <= x2 && x2 < x1 + TP_ERR_RANGE)) &&
            ((y2 <= y1 && y1 < y2 + TP_ERR_RANGE) || (y1 <= y2 && y2 < y1 + TP_ERR_RANGE)))
    {
        *x = (x1 + x2) / 2;
        *y = (y1 + y2) / 2;
        return 1;
    }

    return 0;
}

/******************************************************************************************/
/**
 * @brief   Draw one calibration cross
 */

/**
 * @brief   Draw one calibration cross
 */
static void tp_draw_touch_point(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_draw_line(x - 12, y, x + 13, y, color); 
    lcd_draw_line(x, y - 12, x, y + 13, color); 
    lcd_draw_point(x + 1, y + 1, color);
    lcd_draw_point(x - 1, y + 1, color);
    lcd_draw_point(x + 1, y - 1, color);
    lcd_draw_point(x - 1, y - 1, color);
    lcd_draw_circle(x, y, 6, color);            /**
 * @brief   Draw one 2x2 fat point
 */
}

/**
 * @brief   Draw one 2x2 fat point
 */
void tp_draw_big_point(uint16_t x, uint16_t y, uint16_t color)
{
    lcd_draw_point(x, y, color);       
    lcd_draw_point(x + 1, y, color);
    lcd_draw_point(x, y + 1, color);
    lcd_draw_point(x + 1, y + 1, color);
}

/******************************************************************************************/

/**
 * @brief   Scan touch: mode 0 = screen coords (calibration mapped), mode 1 = raw AD
 */
uint8_t tp_scan(uint8_t mode)
{
    if (T_PEN == 0)     
    {
        if (mode)       /**
 * @brief   Read twice, accept if diff < TP_ERR_RANGE, return average
 */
        {
            tp_read_xy2(&tp_dev.x[0], &tp_dev.y[0]);
        }
        else if (tp_read_xy2(&tp_dev.x[0], &tp_dev.y[0]))     
        {
            /* LCD */
            tp_dev.x[0] = (signed short)(tp_dev.x[0] - tp_dev.xc) / tp_dev.xfac + lcddev.width / 2;

            /* LCD */
            tp_dev.y[0] = (signed short)(tp_dev.y[0] - tp_dev.yc) / tp_dev.yfac + lcddev.height / 2;
        }

        if ((tp_dev.sta & TP_PRES_DOWN) == 0)   
        {
            tp_dev.sta = TP_PRES_DOWN | TP_CATH_PRES;   
            tp_dev.x[CT_MAX_TOUCH - 1] = tp_dev.x[0];   
            tp_dev.y[CT_MAX_TOUCH - 1] = tp_dev.y[0];
        }
    }
    else
    {
        if (tp_dev.sta & TP_PRES_DOWN)      
        {
            tp_dev.sta &= ~TP_PRES_DOWN;    
        }
        else     
        {
            tp_dev.x[CT_MAX_TOUCH - 1] = 0;
            tp_dev.y[CT_MAX_TOUCH - 1] = 0;
            tp_dev.x[0] = 0xffff;
            tp_dev.y[0] = 0xffff;
        }
    }

    return tp_dev.sta & TP_PRES_DOWN; /* calibration save address in 24C02 */
}

/* calibration save address in 24C02 */
#define TP_SAVE_ADDR_BASE   40

/**
 * @brief   Save calibration data to 24C02 (addr 40, 13 bytes)
 */
void tp_save_adjust_data(void)
{
    uint8_t *p = (uint8_t *)&tp_dev.xfac;   

    /* tp_dev.xfac p+4 tp_dev.yfac p+8 tp_dev.xoff p+10 tp_dev.yoff 12 p+12 0X0A 12 0X0A. */
    at24cxx_write(TP_SAVE_ADDR_BASE, p, 12);                /* 12 xfac yfac xc yc */
    at24cxx_write_one_byte(TP_SAVE_ADDR_BASE + 12, 0X0A);   /**
 * @brief   Load calibration from 24C02 (flag 0x0A at addr 52)
 */
}

/**
 * @brief   Load calibration from 24C02 (flag 0x0A at addr 52)
 */
uint8_t tp_get_adjust_data(void)
{
    uint8_t *p = (uint8_t *)&tp_dev.xfac;
    uint8_t temp = 0;

    /* tp_dev.xfac tp_dev.xfac */
    at24cxx_read(TP_SAVE_ADDR_BASE, p, 12);                 /* 12 */
    temp = at24cxx_read_one_byte(TP_SAVE_ADDR_BASE + 12);   

    if (temp == 0X0A)
    {
        g_tp_calib_ok = 1;
        return 1;
    }

    return 0;
}

char *const TP_REMIND_MSG_TBL = "Please use the stylus click the cross on the screen.The cross will always move until the screen adjustment is completed.";

/**
 * @brief   Show calibration progress info on LCD
 */
static void tp_adjust_info_show(uint16_t xy[5][2], double px, double py)
{
    uint8_t i;
    char sbuf[20];

    for (i = 0; i < 5; i++)   
    {
        sprintf(sbuf, "x%d:%d", i + 1, xy[i][0]);
        lcd_show_string(40, 160 + (i * 20), lcddev.width, lcddev.height, 16, sbuf, RED);
        sprintf(sbuf, "y%d:%d", i + 1, xy[i][1]);
        lcd_show_string(40 + 80, 160 + (i * 20), lcddev.width, lcddev.height, 16, sbuf, RED);
    }

    /* X/Y */
    lcd_fill(40, 160 + (i * 20), lcddev.width - 1, 16, WHITE);  /* px py */
    sprintf(sbuf, "px:%0.2f", px);
    sbuf[7] = 0; 
    lcd_show_string(40, 160 + (i * 20), lcddev.width, lcddev.height, 16, sbuf, RED);
    sprintf(sbuf, "py:%0.2f", py);
    sbuf[7] = 0; 
    lcd_show_string(40 + 80, 160 + (i * 20), lcddev.width, lcddev.height, 16, sbuf, RED);
}

/**
 * @brief   5-point calibration UI on LCD (10s timeout)
 */
void tp_adjust(void)
{
    uint16_t pxy[5][2];     
    uint8_t  cnt = 0;
    short s1, s2, s3, s4;   
    double px, py;          
    uint16_t outtime = 0;
    cnt = 0;

    lcd_clear(WHITE);       
    lcd_show_string(40, 40, 160, 100, 16, TP_REMIND_MSG_TBL, RED); /**
 * @brief   Draw one calibration cross
 */
    tp_draw_touch_point(20, 20, RED);   
    tp_dev.sta = 0;         

    while (1)               /* 10 */
    {
        tp_dev.scan(1);     

        if ((tp_dev.sta & 0xc000) == TP_CATH_PRES)   
        {
            outtime = 0;
            tp_dev.sta &= ~TP_CATH_PRES;    

            pxy[cnt][0] = tp_dev.x[0];      
            pxy[cnt][1] = tp_dev.y[0];      /**
 * @brief   Draw one calibration cross
 */
            cnt++;

            switch (cnt)
            {
                case 1:
                    tp_draw_touch_point(20, 20, WHITE);                 /**
 * @brief   Draw one calibration cross
 */
                    tp_draw_touch_point(lcddev.width - 20, 20, RED);    /**
 * @brief   Draw one calibration cross
 */
                    break;

                case 2:
                    tp_draw_touch_point(lcddev.width - 20, 20, WHITE);  /**
 * @brief   Draw one calibration cross
 */
                    tp_draw_touch_point(20, lcddev.height - 20, RED);   /**
 * @brief   Draw one calibration cross
 */
                    break;

                case 3:
                    tp_draw_touch_point(20, lcddev.height - 20, WHITE); /**
 * @brief   Draw one calibration cross
 */
                    tp_draw_touch_point(lcddev.width - 20, lcddev.height - 20, RED);    
                    break;

                case 4:
                    lcd_clear(WHITE);   /**
 * @brief   Draw one calibration cross
 */
                    tp_draw_touch_point(lcddev.width / 2, lcddev.height / 2, RED);  
                    break;

                case 5:     
                    s1 = pxy[1][0] - pxy[0][0]; /* AD */
                    s3 = pxy[3][0] - pxy[2][0]; /* AD */
                    s2 = pxy[3][1] - pxy[1][1]; /* AD */
                    s4 = pxy[2][1] - pxy[0][1]; /* AD */

                    px = (double)s1 / s3;       
                    py = (double)s2 / s4;       

                    if (px < 0)px = -px;        
                    if (py < 0)py = -py;        

                    if (px < 0.95 || px > 1.05 || py < 0.95 || py > 1.05 ||     
                            abs(s1) > 4095 || abs(s2) > 4095 || abs(s3) > 4095 || abs(s4) > 4095 || 
                            abs(s1) == 0 || abs(s2) == 0 || abs(s3) == 0 || abs(s4) == 0            /**
 * @brief   Draw one calibration cross
 */
                       )
                    {
                        cnt = 0;
                        tp_draw_touch_point(lcddev.width / 2, lcddev.height / 2, WHITE); /**
 * @brief   Draw one calibration cross
 */
                        tp_draw_touch_point(20, 20, RED);   /**
 * @brief   Show calibration progress info on LCD
 */
                        tp_adjust_info_show(pxy, px, py);   
                        continue;
                    }

                    tp_dev.xfac = (float)(s1 + s3) / (2 * (lcddev.width - 40));
                    tp_dev.yfac = (float)(s2 + s4) / (2 * (lcddev.height - 40));

                    tp_dev.xc = pxy[4][0];      
                    tp_dev.yc = pxy[4][1];      

                    lcd_clear(WHITE);   
                    lcd_show_string(35, 110, lcddev.width, lcddev.height, 16, "Touch Screen Adjust OK!", BLUE); /**
 * @brief   Save calibration data to 24C02 (addr 40, 13 bytes)
 */
                    HAL_Delay(1000);
                    tp_save_adjust_data();

                    lcd_clear(WHITE);
                    return;
            }
        }

        HAL_Delay(10);
        outtime++;

        if (outtime > 1000)
        {
            tp_get_adjust_data();
            break;
        }
    }

}

/**
 * @brief   Init touch GPIO/SPI + load calibration; run tp_adjust if absent
 */
uint8_t tp_init(void)
{
    GPIO_InitTypeDef gpio_init_struct;
    
    tp_dev.touchtype = 0;                   
    tp_dev.touchtype |= lcddev.dir & 0X01;  /* LCD */


    /* resistive touch only (this project: 2.8-inch XPT2046) */
    {
        T_PEN_GPIO_CLK_ENABLE();    /* T_PEN */
        T_CS_GPIO_CLK_ENABLE();     /* T_CS */
        T_MISO_GPIO_CLK_ENABLE();   /* T_MISO */
        T_MOSI_GPIO_CLK_ENABLE();   /* T_MOSI */
        T_CLK_GPIO_CLK_ENABLE();    /* T_CLK */

        gpio_init_struct.Pin = T_PEN_GPIO_PIN;
        gpio_init_struct.Mode = GPIO_MODE_INPUT;                 
        gpio_init_struct.Pull = GPIO_PULLUP;                     
        gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;           
        HAL_GPIO_Init(T_PEN_GPIO_PORT, &gpio_init_struct);       /* T_PEN */

        gpio_init_struct.Pin = T_MISO_GPIO_PIN;
        HAL_GPIO_Init(T_MISO_GPIO_PORT, &gpio_init_struct);      /* T_MISO */

        gpio_init_struct.Pin = T_MOSI_GPIO_PIN;
        gpio_init_struct.Mode = GPIO_MODE_OUTPUT_PP;             
        gpio_init_struct.Pull = GPIO_PULLUP;                     
        gpio_init_struct.Speed = GPIO_SPEED_FREQ_HIGH;           
        HAL_GPIO_Init(T_MOSI_GPIO_PORT, &gpio_init_struct);      /* T_MOSI */

        gpio_init_struct.Pin = T_CLK_GPIO_PIN;
        HAL_GPIO_Init(T_CLK_GPIO_PORT, &gpio_init_struct);       /* T_CLK */

        gpio_init_struct.Pin = T_CS_GPIO_PIN;
        HAL_GPIO_Init(T_CS_GPIO_PORT, &gpio_init_struct);        /**
 * @brief   Read X/Y raw AD values (X/Y order per touchtype bit0)
 */

        tp_read_xy(&tp_dev.x[0], &tp_dev.y[0]); 
        at24cxx_init();         /**
 * @brief   Load calibration from 24C02 (flag 0x0A at addr 52)
 */

        if (g_tp_calib_ok || tp_get_adjust_data())
        {
            return 0;           
        }
        else                    /* not calibrated */
        {
            lcd_clear(WHITE);   /**
 * @brief   5-point calibration UI on LCD (10s timeout)
 */
            tp_adjust();        /**
 * @brief   Save calibration data to 24C02 (addr 40, 13 bytes)
 */
            tp_save_adjust_data();
        }

        tp_get_adjust_data();
    }

    return 1;
}









