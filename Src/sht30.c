/**
 ******************************************************************************
 * @file    sht30.c
 * @brief   SHT30 温湿度传感器驱动 (软件模拟 I2C)
 *
 * 硬件 I2C 外设在此工程中无法产生时钟(SB=0), 改用软件模拟:
 *   PB6 = SCL (推挽输出)
 *   PB7 = SDA (开漏输出, 外部上拉已验证存在)
 *
 * 读取流程:
 *   1. 发测量命令 0x24 0x00 (高重复性, 无时钟拉伸)
 *   2. 等待 ~20ms 测量完成
 *   3. 读 6 字节: 温度(2) + 温度CRC(1) + 湿度(2) + 湿度CRC(1)
 * 换算公式:
 *   温度 T = -45 + 175 × raw/65535
 *   湿度 RH = 100 × raw/65535
 ******************************************************************************
 */
#include "sht30.h"
#include "i2c.h"
#include <stdio.h>

#define SHT30_ADDR_44      0x44    /* ADDR 接地/悬空时地址 */
#define SHT30_ADDR_45      0x45    /* ADDR 接高时地址 */
#define SHT30_CMD_MEAS_H   0x24    /* 高重复性测量命令, 无时钟拉伸 */
#define SHT30_CMD_MEAS_L   0x00

#define SCL_PIN  GPIO_PIN_6
#define SDA_PIN  GPIO_PIN_7
#define I2C_SCL_H() HAL_GPIO_WritePin(GPIOB, SCL_PIN, GPIO_PIN_SET)
#define I2C_SCL_L() HAL_GPIO_WritePin(GPIOB, SCL_PIN, GPIO_PIN_RESET)
#define I2C_SDA_H() HAL_GPIO_WritePin(GPIOB, SDA_PIN, GPIO_PIN_SET)
#define I2C_SDA_L() HAL_GPIO_WritePin(GPIOB, SDA_PIN, GPIO_PIN_RESET)
#define I2C_SDA_RD() HAL_GPIO_ReadPin(GPIOB, SDA_PIN)

static uint8_t sht30_addr = 0xFF;   /* 0xFF = 尚未探测 */

static void sw_i2c_delay(void)
{
    volatile uint32_t i;
    for (i = 0; i < 50; i++);
}

static void sw_i2c_start(void)
{
    I2C_SDA_H(); I2C_SCL_H(); sw_i2c_delay();
    I2C_SDA_L(); sw_i2c_delay();
    I2C_SCL_L();
}

static void sw_i2c_stop(void)
{
    I2C_SDA_L(); sw_i2c_delay();
    I2C_SCL_H(); sw_i2c_delay();
    I2C_SDA_H(); sw_i2c_delay();
}

/* 返回 0 = 收到 ACK, 1 = NACK */
static uint8_t sw_i2c_write_byte(uint8_t dat)
{
    uint8_t i, ack;
    for (i = 0; i < 8; i++)
    {
        if (dat & 0x80) I2C_SDA_H(); else I2C_SDA_L();
        dat <<= 1;
        sw_i2c_delay();
        I2C_SCL_H(); sw_i2c_delay(); I2C_SCL_L();
    }
    I2C_SDA_H();                /* 释放 SDA, 读从机 ACK */
    sw_i2c_delay();
    I2C_SCL_H(); sw_i2c_delay();
    ack = I2C_SDA_RD();
    I2C_SCL_L(); sw_i2c_delay();
    return ack ? 1 : 0;
}

/* ack: 0 = 主控继续给 ACK, 1 = 主控给 NACK(最后一字节) */
static uint8_t sw_i2c_read_byte(uint8_t ack)
{
    uint8_t i, dat = 0;
    I2C_SDA_H();                /* 释放 SDA, 由从机驱动 */
    for (i = 0; i < 8; i++)
    {
        dat <<= 1;
        I2C_SCL_H(); sw_i2c_delay();
        if (I2C_SDA_RD()) dat |= 0x01;
        I2C_SCL_L(); sw_i2c_delay();
    }
    if (ack) I2C_SDA_H(); else I2C_SDA_L();   /* NACK / ACK */
    sw_i2c_delay();
    I2C_SCL_H(); sw_i2c_delay(); I2C_SCL_L(); sw_i2c_delay();
    I2C_SDA_H();
    return dat;
}

/* 将 PB6/PB7 从 I2C 复用改为普通 GPIO: SCL 推挽, SDA 开漏(靠外部上拉) */
static void sht30_sw_init(void)
{
    GPIO_InitTypeDef g = {0};
    __HAL_RCC_GPIOB_CLK_ENABLE();
    g.Pin = SCL_PIN;
    g.Mode = GPIO_MODE_OUTPUT_PP;
    g.Speed = GPIO_SPEED_FREQ_HIGH;
    HAL_GPIO_Init(GPIOB, &g);
    g.Pin = SDA_PIN;
    g.Mode = GPIO_MODE_OUTPUT_OD;
    HAL_GPIO_Init(GPIOB, &g);
    I2C_SCL_H(); I2C_SDA_H();
    printf("SW-I2C ready (SCL=PB6 PP, SDA=PB7 OD)\r\n");
}

/* 探测从机地址: 返回 0 = 有应答 */
static int sht30_probe_addr(uint8_t addr7)
{
    uint8_t ack;
    sw_i2c_start();
    ack = sw_i2c_write_byte((addr7 << 1) | 0);   /* 写方向 */
    sw_i2c_stop();
    return (ack == 0) ? 0 : -1;
}

int sht30_read(int16_t *temp_x10, int16_t *humi_x10)
{
    uint8_t buf[6];
    uint16_t t_raw, h_raw;
    int i;

    /* 首次调用: 初始化引脚 + 探测地址 */
    if (sht30_addr == 0xFF)
    {
        sht30_sw_init();
        if (sht30_probe_addr(SHT30_ADDR_44) == 0)
        {
            sht30_addr = SHT30_ADDR_44;
            printf("SW-I2C found @0x44\r\n");
        }
        else if (sht30_probe_addr(SHT30_ADDR_45) == 0)
        {
            sht30_addr = SHT30_ADDR_45;
            printf("SW-I2C found @0x45\r\n");
        }
        else
        {
            printf("SW-I2C no device on bus\r\n");
            return -1;
        }
    }

    /* 1. 发测量命令 */
    sw_i2c_start();
    if (sw_i2c_write_byte((sht30_addr << 1) | 0) != 0)   /* 写方向 */
    {
        sw_i2c_stop();
        printf("SHT30 no ACK (write)\r\n");
        return -1;
    }
    sw_i2c_write_byte(SHT30_CMD_MEAS_H);
    sw_i2c_write_byte(SHT30_CMD_MEAS_L);
    sw_i2c_stop();

    /* 2. 等传感器完成测量 */
    HAL_Delay(20);

    /* 3. 读 6 字节结果 */
    sw_i2c_start();
    if (sw_i2c_write_byte((sht30_addr << 1) | 1) != 0)   /* 读方向 */
    {
        sw_i2c_stop();
        printf("SHT30 no ACK (read)\r\n");
        return -1;
    }
    for (i = 0; i < 6; i++)
    {
        buf[i] = sw_i2c_read_byte((i == 5) ? 1 : 0);     /* 最后一字节 NACK */
    }
    sw_i2c_stop();

    t_raw = (uint16_t)((buf[0] << 8) | buf[1]);
    h_raw = (uint16_t)((buf[3] << 8) | buf[4]);

    /* ×10 整数换算, 避免浮点 */
    *temp_x10 = (int16_t)(-450 + (1750L * t_raw) / 65535L);
    *humi_x10 = (int16_t)((1000L * h_raw) / 65535L);

    return 0;
}
