/**
 ******************************************************************************
 * @file    flash_param.h
 * @brief   参数掉电保存 (Flash)
 ******************************************************************************
 */
#ifndef __FLASH_PARAM_H
#define __FLASH_PARAM_H

#include <stdint.h>

/* 读取参数: 返回 0=有效(读出值), -1=无效(应使用默认值) */
int flash_param_load(uint16_t *device_id, uint16_t *interval_ms);
/* 保存参数: 返回 0=成功, -1=失败 */
int flash_param_save(uint16_t device_id, uint16_t interval_ms);

#endif /* __FLASH_PARAM_H */
