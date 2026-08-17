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
#include "lvgl.h"
#include "lv_port_disp.h"
#include <stdio.h>
#include <string.h>
#include "adc.h"
#include "usart.h"
#include "ringbuf.h"
#include "protocol.h"
#include "sht30.h"
#include "flash_param.h"
#include "lcd.h"
#include "touch.h"
#include "lv_port_indev.h"

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
uint16_t adc_raw;   /* ADC raw value 0~4095 */
    uint16_t adc_mv;    /* 换算后的电压, 单位 mV */
    int16_t  temp_x10;  /* 温度 ×10, 256 = 25.6°C; 0x7FFF = 无效 */
    int16_t  humi_x10;  /* 湿度 ×10, 600 = 60.0%; 0x7FFF = 无效 */
} SensorData;

volatile uint16_t g_sample_interval_ms = 200;   /* 采样周期, 可被串口命令修改 */
volatile uint8_t  g_device_id = 0x01;           /* device ID (TODO: persist in flash) */
volatile uint32_t g_proto_crc_err = 0;
osMutexId_t uart_tx_mutex = NULL;   /* serial printf mutex (shared with main.c fputc) */          /* CRC error frame counter (debug) */

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
  .stack_size = 2048 * 4,
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
/* FreeRTOS stack overflow hook: print + loop forever */
/* FreeRTOS malloc failed hook: print + loop forever */
void vApplicationMallocFailedHook(void)
{
    printf("MALLOC FAILED!\r\n");
    while (1);
}
void vApplicationStackOverflowHook(TaskHandle_t xTask, char *pcTaskName)
{
    printf("STACK OVERFLOW: %s\r\n", pcTaskName);
    while (1);
}

/**
  * @brief  FreeRTOS initialization
  * @param  None
  * @retval None
  */
void MX_FREERTOS_Init(void) {
  /* USER CODE BEGIN Init */

  /* USER CODE END Init */

  /* USER CODE BEGIN RTOS_MUTEX */
  uart_tx_mutex = osMutexNew(NULL);
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

/* load params from flash; use defaults if absent */
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

/* sensor data -> CMD 0x01 report (DATA: adc_mv 2B + adc_raw 2B) */
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
/* one-shot: start -> wait -> read -> stop */
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

/* convert: 12-bit ADC, 3.3V reference */
    tx_data.adc_mv = (uint16_t)((uint32_t)tx_data.adc_raw * 3300UL / 4095UL);

/* SHT30 read fail -> 0x7FFF invalid */
    if (sht30_read(&tx_data.temp_x10, &tx_data.humi_x10) != 0)
    {
        tx_data.temp_x10 = 0x7FFF;
        tx_data.humi_x10 = 0x7FFF;
    }

    /* 发给 UART 任务; 队列满则阻塞等待(背压, 自动限流) */
    osMessageQueuePut(q_sensor2uartHandle, &tx_data, 0, 100);
    /* 同时发给显示任务 */
    osMessageQueuePut(q_sensor2displayHandle, &tx_data, 0, 100);

    osDelay(g_sample_interval_ms);   /* 采样周期, 可被 CMD 0x02 修改 */
  }
  /* USER CODE END StartSensorTask */
}

/* ===== App page framework =====
 * Desktop + app pages are persistent lv_obj_t screens, switched by lv_scr_load().
 * Handles are file-static so the UI refresh loop can update them anytime. */
static lv_obj_t *scr_desktop = NULL;
static lv_obj_t *scr_clock = NULL;
static lv_obj_t *scr_weather = NULL;
static lv_obj_t *scr_adc = NULL;
static lv_obj_t *lbl_desk_time = NULL;  /* desktop status time */
static lv_obj_t *lbl_clock_big = NULL;  /* clock app big text */
static lv_obj_t *lbl_w_temp = NULL;     /* weather app */
static lv_obj_t *lbl_w_humi = NULL;
static lv_obj_t *lbl_adc_big = NULL;    /* adc app */
static lv_obj_t *lbl_adc_raw = NULL;
static uint32_t g_boot_tick = 0;        /* os tick at display task start */

/* desktop icon -> open app page */
static void app_open_clock_cb(lv_event_t *e)   { (void)e; lv_scr_load(scr_clock); }
static void app_open_weather_cb(lv_event_t *e) { (void)e; lv_scr_load(scr_weather); }
static void app_open_adc_cb(lv_event_t *e)     { (void)e; lv_scr_load(scr_adc); }

/* app page back button -> desktop */
static void app_back_cb(lv_event_t *e)         { (void)e; lv_scr_load(scr_desktop); }

/* uniform dark button style */
static void ui_btn_style(lv_obj_t *btn)
{
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x1E2A38), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x2A3B4D), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
}

/* desktop icon: big symbol + small name label */
static lv_obj_t *ui_icon_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                                const char *symbol, const char *name, lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_btn_create(parent);
    lv_obj_set_size(btn, 68, 78);
    lv_obj_set_pos(btn, x, y);
    ui_btn_style(btn);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *sym = lv_label_create(btn);
    lv_label_set_text(sym, symbol);
    lv_obj_set_style_text_color(sym, lv_color_hex(0x8AB4F8), 0);
    lv_obj_set_style_text_font(sym, &lv_font_montserrat_20, 0);
    lv_obj_align(sym, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *nm = lv_label_create(btn);
    lv_label_set_text(nm, name);
    lv_obj_set_style_text_color(nm, lv_color_hex(0xCCCCCC), 0);
    lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, 0);
    lv_obj_align(nm, LV_ALIGN_BOTTOM_MID, 0, -8);
    return btn;
}

/* app page scaffold: title + back button, caller adds content */
static lv_obj_t *ui_app_page_create(const char *title, uint32_t title_color)
{
    lv_obj_t *scr = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(scr, lv_color_hex(0x101820), 0);
    lv_obj_clear_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    lv_obj_t *lbl = lv_label_create(scr);
    lv_label_set_text(lbl, title);
    lv_obj_set_style_text_color(lbl, lv_color_hex(title_color), 0);
    lv_obj_set_style_text_font(lbl, &lv_font_montserrat_20, 0);
    lv_obj_align(lbl, LV_ALIGN_TOP_MID, 0, 20);

    lv_obj_t *btn = lv_btn_create(scr);
    lv_obj_set_size(btn, 52, 34);
    lv_obj_align(btn, LV_ALIGN_TOP_LEFT, 8, 10);
    ui_btn_style(btn);
    lv_obj_add_event_cb(btn, app_back_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *b = lv_label_create(btn);
    lv_label_set_text(b, LV_SYMBOL_LEFT);
    lv_obj_set_style_text_color(b, lv_color_hex(0xFFFFFF), 0);
    lv_obj_center(b);
    return scr;
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

  g_boot_tick = osKernelGetTickCount();   /* boot moment for uptime clock */

  lcd_init();    /* init LCD (one-time at boot) */
  printf("[LVGL] lcd_init done\r\n");
  lv_init();    /* init LVGL */
  printf("[LVGL] lv_init done\r\n");
  lv_port_disp_init();    /* register display driver */
  printf("[LVGL] disp_init done\r\n");

  /* touch: XPT2046 resistive init + register LVGL input device */
  uint8_t tp_ok = tp_init();
  printf("[TP] tp_init %s (calib %s)\r\n", tp_ok ? "UNCALIB" : "OK",
         g_tp_calib_ok ? "OK" : "NONE");
  lv_port_indev_init();



  /* ===== Desktop: title + status time + 3 app icons ===== */
  scr_desktop = lv_scr_act();
  lv_obj_set_style_bg_color(scr_desktop, lv_color_hex(0x101820), 0);

  lv_obj_t *lbl_t = lv_label_create(scr_desktop);
  lv_label_set_text(lbl_t, "Sensor OS");
  lv_obj_set_style_text_color(lbl_t, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(lbl_t, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_t, LV_ALIGN_TOP_MID, 0, 14);

  /* status time (uptime) */
  lbl_desk_time = lv_label_create(scr_desktop);
  lv_label_set_text(lbl_desk_time, "00:00:00");
  lv_obj_set_style_text_color(lbl_desk_time, lv_color_hex(0x8AB4F8), 0);
  lv_obj_set_style_text_font(lbl_desk_time, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_desk_time, LV_ALIGN_TOP_MID, 0, 46);

  /* separator */
  lv_obj_t *sep = lv_obj_create(scr_desktop);
  lv_obj_set_size(sep, 200, 2);
  lv_obj_set_style_bg_color(sep, lv_color_hex(0x3A4A5A), 0);
  lv_obj_set_style_border_width(sep, 0, 0);
  lv_obj_set_style_pad_all(sep, 0, 0);
  lv_obj_set_style_radius(sep, 0, 0);
  lv_obj_clear_flag(sep, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_align(sep, LV_ALIGN_TOP_MID, 0, 64);

  /* app icons: one row of 3 */
  ui_icon_create(scr_desktop, 12, 110, LV_SYMBOL_BELL,    "Clock",   app_open_clock_cb);
  ui_icon_create(scr_desktop, 86, 110, LV_SYMBOL_REFRESH, "Weather", app_open_weather_cb);
  ui_icon_create(scr_desktop, 160, 110, LV_SYMBOL_CHARGE, "ADC",     app_open_adc_cb);

  lv_obj_t *ver = lv_label_create(scr_desktop);
  lv_label_set_text(ver, "FreeRTOS + LVGL v1.0");
  lv_obj_set_style_text_color(ver, lv_color_hex(0x556070), 0);
  lv_obj_set_style_text_font(ver, &lv_font_montserrat_14, 0);
  lv_obj_align(ver, LV_ALIGN_BOTTOM_MID, 0, -8);

  /* ===== Clock app page ===== */
  scr_clock = ui_app_page_create("Clock", 0x4FC3F7);
  lbl_clock_big = lv_label_create(scr_clock);
  lv_label_set_text(lbl_clock_big, "00:00:00");
  lv_obj_set_style_text_color(lbl_clock_big, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(lbl_clock_big, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_clock_big, LV_ALIGN_CENTER, 0, -20);
  lv_obj_t *clk_note = lv_label_create(scr_clock);
  lv_label_set_text(clk_note, "UPTIME (HH:MM:SS)");
  lv_obj_set_style_text_color(clk_note, lv_color_hex(0x556070), 0);
  lv_obj_set_style_text_font(clk_note, &lv_font_montserrat_14, 0);
  lv_obj_align(clk_note, LV_ALIGN_CENTER, 0, 20);

  /* ===== Weather app page ===== */
  scr_weather = ui_app_page_create("Weather", 0x4FC3F7);
  lbl_w_temp = lv_label_create(scr_weather);
  lv_label_set_text(lbl_w_temp, "Temp: --");
  lv_obj_set_style_text_color(lbl_w_temp, lv_color_hex(0xFF8C42), 0);
  lv_obj_set_style_text_font(lbl_w_temp, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_w_temp, LV_ALIGN_CENTER, 0, -30);
  lbl_w_humi = lv_label_create(scr_weather);
  lv_label_set_text(lbl_w_humi, "Humi: --");
  lv_obj_set_style_text_color(lbl_w_humi, lv_color_hex(0x81C784), 0);
  lv_obj_set_style_text_font(lbl_w_humi, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_w_humi, LV_ALIGN_CENTER, 0, 20);

  /* ===== ADC app page ===== */
  scr_adc = ui_app_page_create("ADC", 0x81C784);
  lbl_adc_big = lv_label_create(scr_adc);
  lv_label_set_text(lbl_adc_big, "-- mV");
  lv_obj_set_style_text_color(lbl_adc_big, lv_color_hex(0x81C784), 0);
  lv_obj_set_style_text_font(lbl_adc_big, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_adc_big, LV_ALIGN_CENTER, 0, -30);
  lbl_adc_raw = lv_label_create(scr_adc);
  lv_label_set_text(lbl_adc_raw, "raw: ----");
  lv_obj_set_style_text_color(lbl_adc_raw, lv_color_hex(0x556070), 0);
  lv_obj_set_style_text_font(lbl_adc_raw, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_adc_raw, LV_ALIGN_CENTER, 0, 20);

  printf("[LVGL] UI created\r\n");
  {
      lv_mem_monitor_t lv_mon;
      lv_mem_monitor(&lv_mon);
      printf("[LVGL] mem total=%u free=%u frag=%u%%\r\n", (unsigned)lv_mon.total_size, (unsigned)lv_mon.free_size, (unsigned)lv_mon.frag_pct);
  }

/* idle: update UI + call LVGL timer handler */
  uint32_t last_sec = 0;
  for(;;)
  {
    if (osMessageQueueGet(q_sensor2displayHandle, &disp, NULL, 5) == osOK)
    {
        if (disp.temp_x10 != 0x7FFF)
            lv_label_set_text_fmt(lbl_w_temp, "Temp: %d.%d C", disp.temp_x10 / 10,
                                  (disp.temp_x10 % 10 < 0) ? -(disp.temp_x10 % 10) : (disp.temp_x10 % 10));
        else
            lv_label_set_text(lbl_w_temp, "Temp: N/A");

        if (disp.humi_x10 != 0x7FFF)
            lv_label_set_text_fmt(lbl_w_humi, "Humi: %d.%d %%", disp.humi_x10 / 10, disp.humi_x10 % 10);
        else
            lv_label_set_text(lbl_w_humi, "Humi: N/A");

        lv_label_set_text_fmt(lbl_adc_big, "%u mV", (unsigned)disp.adc_mv);
        lv_label_set_text_fmt(lbl_adc_raw, "raw: 0x%04X", (unsigned)disp.adc_raw);
    }

    /* uptime clock: refresh once per second (avoid useless redraw) */
    uint32_t now = osKernelGetTickCount();
    if (now - last_sec >= 1000)
    {
        last_sec = now;
        uint32_t el = (now - g_boot_tick) / 1000;
        lv_label_set_text_fmt(lbl_desk_time, "%02u:%02u:%02u",
                              (unsigned)(el / 3600), (unsigned)((el % 3600) / 60), (unsigned)(el % 60));
        lv_label_set_text_fmt(lbl_clock_big, "%02u:%02u:%02u",
                              (unsigned)(el / 3600), (unsigned)((el % 3600) / 60), (unsigned)(el % 60));
    }

    lv_timer_handler();    /* LVGL render */
    osDelay(5);
  }
/* USER CODE END StartDisplayTask */
}

/* Private application code --------------------------------------------------*/
/* USER CODE BEGIN Application */
/* handle one complete frame: build reply and send */
static void proto_handle_cmd(ProtoParser *p, uint8_t *tx_buf)
{
    uint8_t reply_data[4];
    uint8_t len = 0;
    uint16_t n;

    /* ignore frames with unmatched ID (multi-slave bus) */
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
