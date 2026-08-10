/**
 ******************************************************************************
 * @file    crc16.h
 * @brief   CRC16-MODBUS 校验 (查表法)
 *          多项式 0x8005(反射 0xA001), 初始值 0xFFFF, 结果无异或
 ******************************************************************************
 */
#ifndef __CRC16_H
#define __CRC16_H

#include <stdint.h>

uint16_t crc16_modbus(const uint8_t *data, uint16_t len);

#endif /* __CRC16_H */
