/**
 ****************************************************************************************************
 * @file        touch.h
 * @author      ALIENTEK, adapted for this project
 * @version     V1.1 (resistive-only branch)
 * @date        2023-06-01
 * @brief       Resistive touch screen driver header (XPT2046 / ADS7843)
 *   @note      Pins: T_PEN=PF10 T_CS=PF11 T_MISO=PB2 T_MOSI=PF9 T_CLK=PB1
 ****************************************************************************************************
 */

#ifndef __TOUCH_H
#define __TOUCH_H

#include "main.h"


/******************************************************************************************/
/* IC T_PEN/T_CS/T_MISO/T_MOSI/T_SCK */

#define T_PEN_GPIO_PORT                 GPIOF
#define T_PEN_GPIO_PIN                  GPIO_PIN_10
#define T_PEN_GPIO_CLK_ENABLE()         do{ __HAL_RCC_GPIOF_CLK_ENABLE(); }while(0)   /* IO */

#define T_CS_GPIO_PORT                  GPIOF
#define T_CS_GPIO_PIN                   GPIO_PIN_11
#define T_CS_GPIO_CLK_ENABLE()          do{ __HAL_RCC_GPIOF_CLK_ENABLE(); }while(0)   /* IO */

#define T_MISO_GPIO_PORT                GPIOB
#define T_MISO_GPIO_PIN                 GPIO_PIN_2
#define T_MISO_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)   /* IO */

#define T_MOSI_GPIO_PORT                GPIOF
#define T_MOSI_GPIO_PIN                 GPIO_PIN_9
#define T_MOSI_GPIO_CLK_ENABLE()        do{ __HAL_RCC_GPIOF_CLK_ENABLE(); }while(0)   /* IO */

#define T_CLK_GPIO_PORT                 GPIOB
#define T_CLK_GPIO_PIN                  GPIO_PIN_1
#define T_CLK_GPIO_CLK_ENABLE()         do{ __HAL_RCC_GPIOB_CLK_ENABLE(); }while(0)   /* IO */

/******************************************************************************************/

#define T_PEN           HAL_GPIO_ReadPin(T_PEN_GPIO_PORT, T_PEN_GPIO_PIN)           /* T_PEN */
#define T_MISO          HAL_GPIO_ReadPin(T_MISO_GPIO_PORT, T_MISO_GPIO_PIN)         /* T_MISO */

#define T_MOSI(x)     do{ x ? \
                          HAL_GPIO_WritePin(T_MOSI_GPIO_PORT, T_MOSI_GPIO_PIN, GPIO_PIN_SET) : \
                          HAL_GPIO_WritePin(T_MOSI_GPIO_PORT, T_MOSI_GPIO_PIN, GPIO_PIN_RESET); \
                      }while(0)     /* T_MOSI */

#define T_CLK(x)      do{ x ? \
                          HAL_GPIO_WritePin(T_CLK_GPIO_PORT, T_CLK_GPIO_PIN, GPIO_PIN_SET) : \
                          HAL_GPIO_WritePin(T_CLK_GPIO_PORT, T_CLK_GPIO_PIN, GPIO_PIN_RESET); \
                      }while(0)     /* T_CLK */

#define T_CS(x)       do{ x ? \
                          HAL_GPIO_WritePin(T_CS_GPIO_PORT, T_CS_GPIO_PIN, GPIO_PIN_SET) : \
                          HAL_GPIO_WritePin(T_CS_GPIO_PORT, T_CS_GPIO_PIN, GPIO_PIN_RESET); \
                      }while(0)     /* T_CS */


#define TP_PRES_DOWN    0x8000      
#define TP_CATH_PRES    0x4000      
#define CT_MAX_TOUCH    10          /* touch device structure */

/* touch device structure */
typedef struct
{
    uint8_t (*init)(void);      
    uint8_t (*scan)(uint8_t);   
    void (*adjust)(void);       /* current touch coords (x[0]/y[0] used) */
    uint16_t x[CT_MAX_TOUCH];   
    uint16_t y[CT_MAX_TOUCH];   /* 10 */

    uint16_t sta;               /* b15 1/ b14 b13 b10 b9 b0 */

    float xfac;                 
    float yfac;                 
    short xc;                   /* AD */
    short yc;                   /* touch type: bit0 X/Y swapped */

    /* touch type: bit0 X/Y swapped */
    uint8_t touchtype;
} _m_tp_dev;

extern _m_tp_dev tp_dev;

extern uint8_t g_tp_calib_ok;   /* calibration cache flag (set by main pre-scheduler read) */        /* internal helper functions */


/* internal helper functions */

static void tp_write_byte(uint8_t data);                /**
 * @brief   SPI: read raw ADC value (12bit) from touch controller
 */
static uint16_t tp_read_ad(uint8_t cmd);                /**
 * @brief   Read one axis raw AD with 5-sample median filter
 */
static uint16_t tp_read_xoy(uint8_t cmd);               /**
 * @brief   Read X/Y raw AD values (order per touchtype bit0)
 */
static void tp_read_xy(uint16_t *x, uint16_t *y);       /**
 * @brief   Read twice, accept if diff < TP_ERR_RANGE, return average
 */
static uint8_t tp_read_xy2(uint16_t *x, uint16_t *y);   /**
 * @brief   Draw one calibration cross
 */
static void tp_draw_touch_point(uint16_t x, uint16_t y, uint16_t color);    
static void tp_adjust_info_show(uint16_t xy[5][2], double px, double py);   /* public API */

uint8_t tp_init(void);              /**
 * @brief   Scan touch: mode 0 = screen coords, mode 1 = raw AD
 */
uint8_t tp_scan(uint8_t mode);      /**
 * @brief   5-point calibration UI on LCD (10s timeout)
 */
void tp_adjust(void);               /**
 * @brief   Save calibration to 24C02 (addr 40, 13 bytes)
 */
void tp_save_adjust_data(void);     /**
 * @brief   Load calibration from 24C02 (flag 0x0A)
 */
uint8_t tp_get_adjust_data(void);   /**
 * @brief   Draw one 2x2 fat point
 */
void tp_draw_big_point(uint16_t x, uint16_t y, uint16_t color); 

#endif

















