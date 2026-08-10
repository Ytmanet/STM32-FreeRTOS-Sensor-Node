/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * File Name          : freertos.c
  * Description        : Code for freertos applications
  ******************************************************************************
  * @attention
  *
  * <h2><center>&copy; Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.</center></h2>
  *
  * This software component is licensed by ST under Ultimate Liberty license
  * SLA0044, the "License"; You may not use this file except in compliance with
  * the License. You may obtain a copy of the License at:
  *                             www.st.com/SLA0044
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Includes ------------------------------------------------------------------*/
#include "FreeRTOS.h"
#include "task.h"
#include "main.h"
#include "cmsis_os.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */
#include <stdio.h>
#include <string.h>
#include "adc.h"
#include "usart.h"
#include "ringbuf.h"
#include "protocol.h"
#include "sht30.h"
#include "flash_param.h"
#include "lcd.h"

/* USER CODE END Includes */

/* Private typedef -----------------------------------------------------------*/
/* USER CODE BEGIN PTD */

/* USER CODE END PTD */

/* Private define ------------------------------------------------------------*/
/* USER CODE BEGIN PD */

/* USER CODE END PD */

/* Private macro -------------------------------------------------------------*/
/* USER CODE BEGIN PM */

/* USER CODE END PM */

/* Private variables ---------------------------------------------------------*/
/* USER CODE BEGIN Variables */
/* 传感器数据包: 队列里传的就是这个结构体 */
typedef struct
{
    uint16_t adc_raw;   /* ADC 原始值 0~4095 */
    uint16_t adc_mv;    /* 换算后的电压, 单位 mV */
    int16_t  temp_x10;  /* 温度 ×10, 256 = 25.6°C; 0x7FFF = 无效 */
    int16_t  humi_x10;  /* 湿度 ×10, 600 = 60.0%; 0x7FFF = 无效 */
} SensorData;

volatile uint16_t g_sample_interval_ms = 200;   /* 采样周期, 可被串口命令修改 */
volatile uint8_t  g_device_id = 0x01;           /* 设备 ID (M4 再做成掉电保存) */
volatile uint32_t g_proto_crc_err = 0;          /* CRC 错误帧计数, 调试用 */

/* USER CODE END Variables */
/* Definitions for UART_Protocol_T */
osThreadId_t UART_Protocol_THandle;
const osThreadAttr_t UART_Protocol_T_attributes = {
  .name = "UART_Protocol_T",
  .stack_size = 1024 * 4,
  .priority = (osPriority_t) osPriorityAboveNormal3,
};
/* Definitions for Sensor_Task */
osThreadId_t Sensor_TaskHandle;
const osThreadAttr_t Sensor_Task_attributes = {
  .name = "Sensor_Task",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityNormal2,
};
/* Definitions for Display_Task */
osThreadId_t Display_TaskHandle;
const osThreadAttr_t Display_Task_attributes = {
  .name = "Display_Task",
  .stack_size = 512 * 4,
  .priority = (osPriority_t) osPriorityBelowNormal1,
};
/* Definitions for q_sensor2uart */
osMessageQueueId_t q_sensor2uartHandle;
const osMessageQueueAttr_t q_sensor2uart_attributes = {
  .name = "q_sensor2uart"
};
/* Definitions for q_sensor2display */
osMessageQueueId_t q_sensor2displayHandle;
const osMessageQueueAttr_t q_sensor2display_attributes = {
  .name = "q_sensor2display"
};

/* Private function prototypes -----------------------------------------------*/
/* USER CODE BEGIN FunctionPrototypes */
static void proto_handle_cmd(ProtoParser *p, uint8_t *tx_buf);

/* USER CODE END FunctionPrototypes */

void StartUARTTask(void *argument);
void StartSensorTask(void *argument);
void StartDisplayTask(void *argument);

void MX_FREERTOS_Init(void); /* (MISRA C 2004 rule 8.1) */

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  /* add mutexes, ... */
  /* USER CODE END RTOS_MUTEX */

  /* USER CODE BEGIN RTOS_SEMAPHORES */
  /* add semaphores, ... */
  /* USER CODE END RTOS_SEMAPHORES */

  /* USER CODE BEGIN RTOS_TIMERS */
  /* start timers, add new ones, ... */
  /* USER CODE END RTOS_TIMERS */

  /* Create the queue(s) */
  /* creation of q_sensor2uart */
  q_sensor2uartHandle = osMessageQueueNew (32, sizeof(SensorData), &q_sensor2uart_attributes);

  /* creation of q_sensor2display */
  q_sensor2displayHandle = osMessageQueueNew (32, sizeof(SensorData), &q_sensor2display_attributes);

  /* USER CODE BEGIN RTOS_QUEUES */
  /* add queues, ... */
  /* USER CODE END RTOS_QUEUES */

  /* Create the thread(s) */
  /* creation of UART_Protocol_T */
  UART_Protocol_THandle = osThreadNew(StartUARTTask, NULL, &UART_Protocol_T_attributes);

  /* creation of Sensor_Task */
  Sensor_TaskHandle = osThreadNew(StartSensorTask, NULL, &Sensor_Task_attributes);

  /* creation of Display_Task */
  Display_TaskHandle = osThreadNew(StartDisplayTask, NULL, &Display_Task_attributes);

  /* USER CODE BEGIN RTOS_THREADS */
  /* add threads, ... */
  /* USER CODE END RTOS_THREADS */

  /* USER CODE BEGIN RTOS_EVENTS */
  /* add events, ... */
  /* USER CODE END RTOS_EVENTS */

}

/* USER CODE BEGIN Header_StartUARTTask */
/**
  * @brief  Function implementing the UART_Protocol_T thread.
  * @param  argument: Not used
  * @retval None
  */
/* USER CODE END Header_StartUARTTask */
void StartUARTTask(void *argument)
{
  /* USER CODE BEGIN StartUARTTask */
  SensorData rx_data;
  uint8_t byte;
  ProtoParser parser;
  uint8_t tx_buf[PROTO_MAX_DATA_LEN + 6];

  proto_init(&parser);
  uart1_rx_start();   /* 启动 DMA 循环接收 + IDLE 中断 */

  /* 开机加载掉电保存的参数, 没有则用默认值 */
  {
      uint16_t id, interval;
      if (flash_param_load(&id, &interval) == 0)
      {
          g_device_id = (uint8_t)id;
          g_sample_interval_ms = interval;
          printf("params loaded: ID=%u interval=%ums\r\n", (unsigned)id, (unsigned)interval);
      }
      else
      {
          printf("params default: ID=1 interval=200ms\r\n");
      }
  }
  printf("Proto v1 up: use HEX mode. Frames: AA ID CMD LEN DATA CRC16\r\n");

  /* Infinite loop */
  for(;;)
  {
    /* 1. 串口字节 -> 协议状态机 */
    while (rb_read(&uart1_rx_ring, &byte))
    {
        ProtoStatus st = proto_feed(&parser, byte);

        if (st == PROTO_FRAME_OK)
        {
            proto_handle_cmd(&parser, tx_buf);
        }
        else if (st == PROTO_CRC_ERR)
        {
            g_proto_crc_err++;
            printf("CRC err! count=%lu\r\n", (unsigned long)g_proto_crc_err);
        }
        else if (st == PROTO_LEN_ERR)
        {
            printf("LEN err!\r\n");
        }
    }

    /* 2. 传感器数据 -> CMD 0x01 上报帧 (DATA: adc_mv 高/低 + adc_raw 高/低) */
    if (osMessageQueueGet(q_sensor2uartHandle, &rx_data, NULL, 50) == osOK)
    {
        uint8_t payload[8];
        uint16_t n;

        payload[0] = (uint8_t)(rx_data.adc_mv >> 8);
        payload[1] = (uint8_t)(rx_data.adc_mv & 0xFF);
        payload[2] = (uint8_t)(rx_data.adc_raw >> 8);
        payload[3] = (uint8_t)(rx_data.adc_raw & 0xFF);
        payload[4] = (uint8_t)(rx_data.temp_x10 >> 8);
        payload[5] = (uint8_t)(rx_data.temp_x10 & 0xFF);
        payload[6] = (uint8_t)(rx_data.humi_x10 >> 8);
        payload[7] = (uint8_t)(rx_data.humi_x10 & 0xFF);

        n = proto_build(g_device_id, PROTO_CMD_REPORT, payload, 8, tx_buf);
        HAL_UART_Transmit(&huart1, tx_buf, n, 100);
    }
  }
  /* USER CODE END StartUARTTask */
}

/* USER CODE BEGIN Header_StartSensorTask */
/**
* @brief Function implementing the Sensor_Task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartSensorTask */
void StartSensorTask(void *argument)
{
  /* USER CODE BEGIN StartSensorTask */
  SensorData tx_data;

  /* Infinite loop */
  for(;;)
  {
    /* 单次采集: 启动 -> 等转换完成 -> 取值 -> 停止 */
    HAL_ADC_Start(&hadc1);
    if (HAL_ADC_PollForConversion(&hadc1, 10) == HAL_OK)
    {
        tx_data.adc_raw = HAL_ADC_GetValue(&hadc1);
    }
    else
    {
        tx_data.adc_raw = 0;
    }
    HAL_ADC_Stop(&hadc1);

    /* 换算电压: 12位ADC, 3.3V 参考电压 */
    tx_data.adc_mv = (uint16_t)((uint32_t)tx_data.adc_raw * 3300UL / 4095UL);

    /* 读 SHT30 温湿度; 没接模块会失败, 填无效值 0x7FFF */
    if (sht30_read(&tx_data.temp_x10, &tx_data.humi_x10) != 0)
    {
        tx_data.temp_x10 = 0x7FFF;
        tx_data.humi_x10 = 0x7FFF;
    }

    /* 发给 UART 任务; 队列满则阻塞等待(背压, 自动限流) */
    osMessageQueuePut(q_sensor2uartHandle, &tx_data, 0, osWaitForever);
    /* 同时发给显示任务 */
    osMessageQueuePut(q_sensor2displayHandle, &tx_data, 0, osWaitForever);

    osDelay(g_sample_interval_ms);   /* 采样周期, 可被 CMD 0x02 修改 */
  }
  /* USER CODE END StartSensorTask */
}

/* USER CODE BEGIN Header_StartDisplayTask */
/**
* @brief Function implementing the Display_Task thread.
* @param argument: Not used
* @retval None
*/
/* USER CODE END Header_StartDisplayTask */
void StartDisplayTask(void *argument)
{
  /* USER CODE BEGIN StartDisplayTask */
  SensorData disp;
  char buf[20];
  int16_t last_temp = 0x7FFF, last_humi = 0x7FFF;
  uint16_t last_adc = 0xFFFF, last_id = 0xFFFF, last_rate = 0xFFFF;

  lcd_init();                 /* 初始化 LCD (含上电延时, 只执行一次) */
  lcd_clear(WHITE);           /* 清屏 */

  /* ===== 静态界面: 边框/标题/标签只画一次, 之后只刷新数值 ===== */
  lcd_draw_rectangle(6, 6, 233, 313, GRAY);            /* 边框 */
  lcd_show_string(16, 12, 200, 16, 16, "STM32 Sensor Node", RED);
  lcd_draw_hline(16, 36, 208, GRAY);                   /* 分隔线 */

  lcd_show_string(16, 48, 70, 16, 16, "Temp:", BLACK);
  lcd_show_string(16, 80, 70, 16, 16, "Humi:", BLACK);
  lcd_show_string(16, 112, 70, 16, 16, "ADC:", BLACK);
  lcd_show_string(16, 144, 70, 16, 16, "ID:", BLACK);
  lcd_show_string(16, 176, 70, 16, 16, "Rate:", BLACK);

  /* Infinite loop */
  for(;;)
  {
    if (osMessageQueueGet(q_sensor2displayHandle, &disp, NULL, osWaitForever) == osOK)
    {
        /* 温度: 值变化才刷新 (SHT30 变化很慢, 不会频繁刷屏) */
        if (disp.temp_x10 != last_temp)
        {
            if (disp.temp_x10 != 0x7FFF)
                sprintf(buf, "%d.%d C", disp.temp_x10 / 10,
                        (disp.temp_x10 % 10 < 0) ? -(disp.temp_x10 % 10) : (disp.temp_x10 % 10));
            else
                strcpy(buf, "N/A");
            lcd_fill(90, 48, 228, 63, WHITE);          /* 只擦数值区 */
            lcd_show_string(90, 48, 138, 16, 16, buf, BLUE);
            last_temp = disp.temp_x10;
        }

        /* 湿度 */
        if (disp.humi_x10 != last_humi)
        {
            if (disp.humi_x10 != 0x7FFF)
                sprintf(buf, "%d.%d %%", disp.humi_x10 / 10, disp.humi_x10 % 10);
            else
                strcpy(buf, "N/A");
            lcd_fill(90, 80, 228, 95, WHITE);
            lcd_show_string(90, 80, 138, 16, 16, buf, BLUE);
            last_humi = disp.humi_x10;
        }

        /* ADC: 变化超过 20mV 才刷新 (死区, 避免悬空时乱跳刷屏) */
        if ((disp.adc_mv > last_adc) ? (disp.adc_mv - last_adc >= 20) : (last_adc - disp.adc_mv >= 20))
        {
            sprintf(buf, "%u mV", (unsigned)disp.adc_mv);
            lcd_fill(90, 112, 228, 127, WHITE);
            lcd_show_string(90, 112, 138, 16, 16, buf, BLUE);
            last_adc = disp.adc_mv;
        }

        /* 设备 ID */
        if ((uint16_t)g_device_id != last_id)
        {
            sprintf(buf, "%u", (unsigned)g_device_id);
            lcd_fill(90, 144, 228, 159, WHITE);
            lcd_show_string(90, 144, 138, 16, 16, buf, BLUE);
            last_id = g_device_id;
        }

        /* 采样周期 */
        if (g_sample_interval_ms != last_rate)
        {
            sprintf(buf, "%u ms", (unsigned)g_sample_interval_ms);
            lcd_fill(90, 176, 228, 191, WHITE);
            lcd_show_string(90, 176, 138, 16, 16, buf, BLUE);
            last_rate = g_sample_interval_ms;
        }
    }
  }
  /* USER CODE END StartDisplayTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
/* 处理一帧完整命令, 组回复帧并通过串口发出 */
static void proto_handle_cmd(ProtoParser *p, uint8_t *tx_buf)
{
    uint8_t reply_data[4];
    uint8_t len = 0;
    uint16_t n;

    /* ID 不匹配的帧直接忽略 (一主多从总线上只处理发给自己的帧) */
    if (p->id != g_device_id)
    {
        return;
    }

    switch (p->cmd)
    {
        case PROTO_CMD_SET_PARAM:               /* 0x02: 设置参数 */
        {
            uint8_t status = 0;                 /* 0=成功, 1=参数无效 */
            int need_save = 0;

            if (p->data_len >= 1)
            {
                if ((p->data[0] == PROTO_PARAM_INTERVAL) && (p->data_len >= 3))
                {
                    g_sample_interval_ms = (uint16_t)((p->data[1] << 8) | p->data[2]);
                    need_save = 1;
                }
                else if ((p->data[0] == PROTO_PARAM_DEVID) && (p->data_len >= 2))
                {
                    g_device_id = p->data[1];
                    need_save = 1;
                }
                else
                {
                    status = 1;
                }

                if (need_save)
                {
                    flash_param_save(g_device_id, g_sample_interval_ms);   /* 掉电保存 */
                }

                reply_data[0] = p->data[0];
                reply_data[1] = status;
                len = 2;
            }
            else
            {
                reply_data[0] = 0;
                reply_data[1] = 1;
                len = 2;
            }
            break;
        }

        case PROTO_CMD_QUERY:                   /* 0x03: 查询设备信息 */
            reply_data[0] = 0x01;               /* 固件版本 V1.0 */
            reply_data[1] = 0x00;
            reply_data[2] = g_device_id;
            len = 3;
            break;

        default:                                /* 未知命令: 回一个错误码 */
            reply_data[0] = 0xFF;
            len = 1;
            break;
    }

    n = proto_build(g_device_id, p->cmd, reply_data, len, tx_buf);
    HAL_UART_Transmit(&huart1, tx_buf, n, 100);
}

/* USER CODE END Application */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
