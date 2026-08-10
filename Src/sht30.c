/**
 ******************************************************************************
 * @file    sht30.c
 * @brief   SHT30 温湿度传感器驱动 (I2C1, PB6/PB7)
 *
 * 读取流程:
 *   1. 发测量命令 0x24 0x00 (高重复性, 无时钟拉伸)
 *   2. 等待 ~15ms 测量完成
 *   3. 读 6 字节: 温度(2) + 温度CRC(1) + 湿度(2) + 湿度CRC(1)
 * 换算公式:
 *   温度 T = -45 + 175 × raw/65535
 *   湿度 RH = 100 × raw/65535
 ******************************************************************************
 */
#include "sht30.h"
#include "i2c.h"

#define SHT30_I2C_ADDR      0x88    /* 7位地址 0x44, HAL 接口要左移一位 */
#define SHT30_CMD_MEAS_H    0x24    /* 高重复性测量命令(无时钟拉伸) */
#define SHT30_CMD_MEAS_L    0x00

int sht30_read(int16_t *temp_x10, int16_t *humi_x10)
{
    uint8_t cmd[2] = {SHT30_CMD_MEAS_H, SHT30_CMD_MEAS_L};
    uint8_t buf[6];
    uint16_t t_raw, h_raw;

    /* 1. 发测量命令 */
    if (HAL_I2C_Master_Transmit(&hi2c1, SHT30_I2C_ADDR, cmd, 2, 50) != HAL_OK)
    {
        return -1;
    }

    /* 2. 等传感器完成测量 (约 15ms) */
    HAL_Delay(20);

    /* 3. 读 6 字节结果 */
    if (HAL_I2C_Master_Receive(&hi2c1, SHT30_I2C_ADDR, buf, 6, 50) != HAL_OK)
    {
        return -1;
    }

    t_raw = (uint16_t)((buf[0] << 8) | buf[1]);
    h_raw = (uint16_t)((buf[3] << 8) | buf[4]);

    /* ×10 整数换算, 避免浮点 */
    *temp_x10 = (int16_t)(-450 + (1750L * t_raw) / 65535L);
    *humi_x10 = (int16_t)((1000L * h_raw) / 65535L);

    return 0;
}
