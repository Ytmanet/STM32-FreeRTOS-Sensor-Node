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
#include "adc.h"
#include "usart.h"
#include "ringbuf.h"
#include "protocol.h"

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
  .stack_size = 256 * 4,
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
  printf("Protocol ready: CMD 0x02=set param, 0x03=query\r\n");

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
        uint8_t payload[4];
        uint16_t n;

        payload[0] = (uint8_t)(rx_data.adc_mv >> 8);
        payload[1] = (uint8_t)(rx_data.adc_mv & 0xFF);
        payload[2] = (uint8_t)(rx_data.adc_raw >> 8);
        payload[3] = (uint8_t)(rx_data.adc_raw & 0xFF);

        n = proto_build(g_device_id, PROTO_CMD_REPORT, payload, 4, tx_buf);
        HAL_UART_Transmit(&huart1, tx_buf, n, 100);

        printf("ADC: %u mV (raw %u)\r\n", rx_data.adc_mv, rx_data.adc_raw);   /* 人眼调试 */
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

    /* 发给 UART 任务; 队列满则阻塞等待(背压, 自动限流) */
    osMessageQueuePut(q_sensor2uartHandle, &tx_data, 0, osWaitForever);

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
  /* Infinite loop */
  for(;;)
  {
    osDelay(1);
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

    switch (p->cmd)
    {
        case PROTO_CMD_SET_PARAM:               /* 0x02: 设置参数 */
        {
            uint8_t status = 0;                 /* 0=成功, 1=参数无效 */

            if (p->data_len >= 1)
            {
                if ((p->data[0] == PROTO_PARAM_INTERVAL) && (p->data_len >= 3))
                {
                    g_sample_interval_ms = (uint16_t)((p->data[1] << 8) | p->data[2]);
                }
                else if ((p->data[0] == PROTO_PARAM_DEVID) && (p->data_len >= 2))
                {
                    g_device_id = p->data[1];
                }
                else
                {
                    status = 1;
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
