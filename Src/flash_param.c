/**
 ******************************************************************************
 * @file    flash_param.c
 * @brief   参数掉电保存 (Flash)
 *
 * 存储位置: F103ZE 的 512KB Flash 最后一个扇区 (0x08060000, 128KB)
 *   注意: 原 README 写的 0x080C0000 超出了 512KB Flash 范围, 是错的!
 *   正确地址是 0x08060000 (128KB 扇区) 或 0x08040000 (64KB 扇区)
 *
 * 写入流程: 解锁 -> 擦除整扇区 -> 写 3 个半字 -> 上锁
 * 读取流程: 指针直接访问 + CRC 校验
 *
 * 注意: 擦除 128KB 扇区需要约 1~2 秒, 期间 CPU 从 Flash 取指会被阻塞,
 *       所以保存参数时 UART 任务会卡顿一下, 这是 F103 的硬件特性。
 ******************************************************************************
 */
#include "flash_param.h"
#include "crc16.h"
#include "main.h"

#define FLASH_PARAM_ADDR   0x08060000UL   /* F103ZE 最后一个 128KB 扇区 */

typedef struct
{
    uint16_t device_id;
    uint16_t sample_interval_ms;
    uint16_t crc;               /* CRC16 覆盖前两个字段 */
} SysParam;

static uint16_t param_crc(const SysParam *p)
{
    return crc16_modbus((const uint8_t *)p, 4);
}

int flash_param_load(uint16_t *device_id, uint16_t *interval_ms)
{
    const SysParam *p = (const SysParam *)FLASH_PARAM_ADDR;

    /* CRC 对上 + 周期在合理范围, 才算有效参数 */
    if ((p->crc == param_crc(p)) && (p->sample_interval_ms != 0) && (p->sample_interval_ms <= 60000))
    {
        *device_id = p->device_id;
        *interval_ms = p->sample_interval_ms;
        return 0;
    }

    return -1;
}

int flash_param_save(uint16_t device_id, uint16_t interval_ms)
{
    SysParam p;
    FLASH_EraseInitTypeDef erase;
    uint32_t page_err = 0;
    uint32_t addr;

    p.device_id = device_id;
    p.sample_interval_ms = interval_ms;
    p.crc = param_crc(&p);

    HAL_FLASH_Unlock();

    /* 擦除整个扇区 (128KB, 约 1~2 秒) */
    erase.TypeErase = FLASH_TYPEERASE_PAGES;
    erase.PageAddress = FLASH_PARAM_ADDR;
    erase.NbPages = 1;
    if (HAL_FLASHEx_Erase(&erase, &page_err) != HAL_OK)
    {
        HAL_FLASH_Lock();
        return -1;
    }

    /* 依次写入 3 个半字 (Flash 最小写入单位是 16 位) */
    addr = FLASH_PARAM_ADDR;
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr, p.device_id);
    addr += 2;
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr, p.sample_interval_ms);
    addr += 2;
    HAL_FLASH_Program(FLASH_TYPEPROGRAM_HALFWORD, addr, p.crc);

    HAL_FLASH_Lock();
    return 0;
}
