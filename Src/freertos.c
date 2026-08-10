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

  /* Infinite loop */
  for(;;)
  {
    /* 阻塞等待队列: 有数据才醒, 没有就睡觉(不占CPU) */
    if (osMessageQueueGet(q_sensor2uartHandle, &rx_data, NULL, osWaitForever) == osOK)
    {
        printf("ADC: %u mV (raw %u)\r\n", rx_data.adc_mv, rx_data.adc_raw);
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

    osDelay(200);   /* 200ms 采一次 */
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

/* USER CODE END Application */

/************************ (C) COPYRIGHT STMicroelectronics *****END OF FILE****/
