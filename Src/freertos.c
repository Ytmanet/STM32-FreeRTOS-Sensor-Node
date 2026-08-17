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
#include "adc.h"
#include "usart.h"
#include "ringbuf.h"
#include "protocol.h"
#include "sht30.h"
#include "flash_param.h"
#include "lcd.h"
#include "touch.h"
#include "lv_port_indev.h"
#include "rtc.h"

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
volatile uint32_t g_proto_crc_err = 0;   /* CRC error frame counter (debug) */
osMutexId_t uart_tx_mutex = NULL;   /* serial printf mutex (shared with main.c fputc) */

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
static lv_obj_t *scr_calc = NULL;
static lv_obj_t *lbl_desk_time = NULL;  /* desktop status time */
static lv_obj_t *lbl_clock_big = NULL;  /* clock app big text */
static lv_obj_t *lbl_clock_date = NULL; /* clock app date line */
static lv_obj_t *lbl_w_temp = NULL;     /* weather app */
static lv_obj_t *lbl_w_humi = NULL;
static lv_obj_t *chart_weather = NULL;  /* weather history chart */
static lv_chart_series_t *s_w_temp = NULL;
static lv_chart_series_t *s_w_humi = NULL;
static int16_t last_temp_x10 = 0x7FFF;  /* latest values, for 1Hz chart feed */
static int16_t last_humi_x10 = 0x7FFF;
static uint16_t last_adc_mv = 0xFFFF;   /* last shown value, change-check */
/* adaptive Y range: track min/max of valid samples (x10 fixed point),
 * so small variations stay visible instead of being flattened */
static int16_t chart_tmin = 0x7FFF, chart_tmax = 0x8000;
static int16_t chart_hmin = 0x7FFF, chart_hmax = 0x8000;

static void chart_update_range(void)
{
    if (chart_tmin > chart_tmax) return;   /* no valid samples yet, keep initial range */
    lv_chart_set_range(chart_weather, LV_CHART_AXIS_PRIMARY_Y,
                       chart_tmin - 15, chart_tmax + 15);   /* +/- 1.5 C margin */
    lv_chart_set_range(chart_weather, LV_CHART_AXIS_SECONDARY_Y,
                       chart_hmin - 50, chart_hmax + 50);   /* +/- 5 % margin */
}
static lv_obj_t *lbl_adc_big = NULL;    /* adc app */
static lv_obj_t *lbl_adc_raw = NULL;

/* desktop icon -> open app page (fade in: renders one screen per frame) */
static void app_open_clock_cb(lv_event_t *e)
{ (void)e; lv_scr_load_anim(scr_clock, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false); }
static void app_open_weather_cb(lv_event_t *e)
{ (void)e; lv_scr_load_anim(scr_weather, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false); }
static void app_open_adc_cb(lv_event_t *e)
{ (void)e; lv_scr_load_anim(scr_adc, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false); }
static void app_open_calc_cb(lv_event_t *e)
{ (void)e; lv_scr_load_anim(scr_calc, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false); }

/* app page back button -> desktop */
static void app_back_cb(lv_event_t *e)
{ (void)e; lv_scr_load_anim(scr_desktop, LV_SCR_LOAD_ANIM_FADE_ON, 200, 0, false); }

/* shared button styles (one instance, all buttons reference them) */
static lv_style_t g_style_btn;
static lv_style_t g_style_btn_pressed;

static void ui_styles_init(void)
{
    lv_style_init(&g_style_btn);
    lv_style_set_bg_color(&g_style_btn, lv_color_hex(0x1E2A38));
    lv_style_set_radius(&g_style_btn, 10);
    lv_style_set_shadow_width(&g_style_btn, 0);
    lv_style_set_border_width(&g_style_btn, 0);

    lv_style_init(&g_style_btn_pressed);
    lv_style_set_bg_color(&g_style_btn_pressed, lv_color_hex(0x2A3B4D));
}

static void ui_btn_style(lv_obj_t *btn)
{
    lv_obj_add_style(btn, &g_style_btn, 0);
    lv_obj_add_style(btn, &g_style_btn_pressed, LV_STATE_PRESSED);
}

/* ===== Calculator app =====
 * Expression string + recursive-descent eval (mul/div before add/sub, left assoc) */
static lv_obj_t *lbl_calc = NULL;
static char calc_expr[32];
static uint8_t calc_len = 0;
static uint8_t calc_is_result = 0;

static const char *calc_map[] = {
    "C", LV_SYMBOL_BACKSPACE, "%", "/", "\n",
    "7", "8", "9", "*", "\n",
    "4", "5", "6", "-", "\n",
    "1", "2", "3", "+", "\n",
    "0", ".", "=", " ",
    ""
};

/* parse one term: number [(*|/) number]* */
static double calc_term(const char **p, int *err)
{
    double v = strtod(*p, (char **)p);
    while (**p == '*' || **p == '/')
    {
        char op = *(*p)++;
        double r = strtod(*p, (char **)p);
        if (op == '/' && r == 0.0) { *err = 1; return 0.0; }
        v = (op == '*') ? v * r : v / r;
    }
    return v;
}

/* parse full expression: term [(+|-) term]* */
static double calc_eval(const char *s, int *err)
{
    const char *p = s;
    double v = calc_term(&p, err);
    while (*p == '+' || *p == '-')
    {
        char op = *p++;
        double r = calc_term(&p, err);
        v = (op == '+') ? v + r : v - r;
    }
    return v;
}

static void calc_refresh(void)
{
    lv_label_set_text(lbl_calc, calc_len ? calc_expr : "0");
}

/* format double to string: max 6 decimal digits, strip trailing zeros.
 * (LVGL's lv_snprintf has no float support - LV_SPRINTF_USE_FLOAT=0) */
static void calc_format_result(double v, char *out)
{
    size_t i = 0;
    double a, fp;
    long long ip;

    if (v != v || v > 9.0e18 || v < -9.0e18)   /* NaN / overflow */
    {
        strcpy(out, "Error");
        return;
    }
    a = (v < 0) ? -v : v;
    ip = (long long)a;
    fp = a - (double)ip;

    if (v < 0) out[i++] = '-';
    if (ip == 0) out[i++] = '0';
    else
    {
        char rev[24];
        int j = 0;
        while (ip > 0 && j < 23) { rev[j++] = (char)('0' + ip % 10); ip /= 10; }
        while (j > 0) out[i++] = rev[--j];
    }
    if (fp > 1e-9)
    {
        int digits = 0;
        out[i++] = '.';
        while (fp > 1e-9 && digits < 6)
        {
            fp *= 10.0;
            int d = (int)fp;
            out[i++] = (char)('0' + d);
            fp -= (double)d;
            digits++;
        }
        while (i > 0 && out[i - 1] == '0') i--;   /* strip trailing zeros */
        if (out[i - 1] == '.') i--;               /* "1." -> "1" */
    }
    out[i] = '\0';
}

static void calc_key(char k)
{
    if (k == ' ') return;                       /* unused pad key */

    switch (k)
    {
        case 'C':
            calc_len = 0; calc_expr[0] = 0; calc_is_result = 0;
            break;

        case 'B':                               /* backspace */
            if (calc_is_result) { calc_len = 0; calc_expr[0] = 0; calc_is_result = 0; }
            else if (calc_len) { calc_expr[--calc_len] = 0; }
            break;

        case '=':
        {
            int err = 0;
            double v = calc_eval(calc_expr, &err);
            if (err || v != v)                  /* v != v catches NaN */
            {
                lv_label_set_text(lbl_calc, "Error");
                calc_len = 0; calc_expr[0] = 0; calc_is_result = 0;
            }
            else
            {
                calc_format_result(v, calc_expr);
                calc_len = (uint8_t)strlen(calc_expr);
                calc_is_result = 1;
                lv_label_set_text(lbl_calc, calc_expr);
            }
            return;
        }

        default:                                /* digits, '.', ops */
            if (calc_is_result)
            {
                if (k >= '0' && k <= '9') { calc_len = 0; calc_expr[0] = 0; }
                calc_is_result = 0;
            }
            if (calc_len >= sizeof(calc_expr) - 4) break;   /* overflow guard */
            if (k == '%') { strcpy(&calc_expr[calc_len], "/100"); calc_len += 4; }
            else { calc_expr[calc_len++] = k; calc_expr[calc_len] = 0; }
            break;
    }
    calc_refresh();
}

static void calc_btn_cb(lv_event_t *e)
{
    lv_obj_t *bm = lv_event_get_target(e);
    uint16_t sel = lv_btnmatrix_get_selected_btn(bm);
    const char *txt;

    if (sel == LV_BTNMATRIX_BTN_NONE) return;
    txt = lv_btnmatrix_get_btn_text(bm, sel);
    if (strcmp(txt, LV_SYMBOL_BACKSPACE) == 0) calc_key('B');
    else calc_key(txt[0]);
}

/* desktop app icon: colored rounded tile (phone-style) + symbol + name below.
 * The tile is a gradient rounded square with a soft shadow; the app symbol is
 * centered in white; the name sits under the tile like a phone launcher. */
static lv_obj_t *ui_icon_create(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                                const char *symbol, const char *name,
                                uint32_t tile_color, lv_event_cb_t cb)
{
    lv_obj_t *tile = lv_btn_create(parent);
    lv_obj_set_size(tile, 52, 52);
    lv_obj_set_pos(tile, x, y);
    lv_obj_set_style_bg_color(tile, lv_color_hex(tile_color), 0);
    lv_obj_set_style_bg_grad_color(tile, lv_color_hex(0x101820), 0);
    lv_obj_set_style_bg_grad_dir(tile, LV_GRAD_DIR_VER, 0);
    lv_obj_set_style_bg_opa(tile, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(tile, lv_color_hex(0x2A3B4D), LV_STATE_PRESSED);
    lv_obj_set_style_bg_grad_color(tile, lv_color_hex(0x2A3B4D), LV_STATE_PRESSED);
    lv_obj_set_style_radius(tile, 14, 0);
    lv_obj_set_style_border_width(tile, 0, 0);
    lv_obj_set_style_shadow_width(tile, 8, 0);
    lv_obj_set_style_shadow_color(tile, lv_color_hex(0x000000), 0);
    lv_obj_set_style_shadow_opa(tile, LV_OPA_60, 0);
    lv_obj_set_style_shadow_ofs_y(tile, 3, 0);
    lv_obj_add_event_cb(tile, cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *sym = lv_label_create(tile);
    lv_label_set_text(sym, symbol);
    lv_obj_set_style_text_color(sym, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(sym, &lv_font_montserrat_20, 0);
    lv_obj_center(sym);

    lv_obj_t *nm = lv_label_create(parent);
    lv_label_set_text(nm, name);
    lv_obj_set_style_text_color(nm, lv_color_hex(0xB0B8C4), 0);
    lv_obj_set_style_text_font(nm, &lv_font_montserrat_14, 0);
    lv_obj_align_to(nm, tile, LV_ALIGN_BOTTOM_MID, 0, 5);
    return tile;
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

  lcd_init();    /* init LCD (one-time at boot) */
  printf("[LVGL] lcd_init done\r\n");
  lv_init();    /* init LVGL */
  printf("[LVGL] lv_init done\r\n");
  lv_port_disp_init();    /* register display driver */
  printf("[LVGL] disp_init done\r\n");

  /* RTC: real clock (LSE 32.768kHz, battery-backed) */
  if (rtc_init() == 0)
  {
      rtc_set_from_build_time_if_needed();
      printf("[RTC] ready\r\n");
  }
  else
  {
      printf("[RTC] LSE FAIL - uptime mode\r\n");
  }

  /* touch: XPT2046 resistive init + register LVGL input device */
  uint8_t tp_ok = tp_init();
  printf("[TP] tp_init %s (calib %s)\r\n", tp_ok ? "UNCALIB" : "OK",
         g_tp_calib_ok ? "OK" : "NONE");
  lv_port_indev_init();



  /* ===== Desktop: title + status time + 3 app icons ===== */
  ui_styles_init();               /* shared button styles (before any widget) */
  scr_desktop = lv_scr_act();
  lv_obj_set_style_bg_color(scr_desktop, lv_color_hex(0x101820), 0);

  lv_obj_t *lbl_t = lv_label_create(scr_desktop);
  lv_label_set_text(lbl_t, "Sensor OS");
  lv_obj_set_style_text_color(lbl_t, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(lbl_t, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_t, LV_ALIGN_TOP_MID, 0, 14);

  /* status time, top-right like a phone status bar */
  lbl_desk_time = lv_label_create(scr_desktop);
  lv_label_set_text(lbl_desk_time, "00:00");
  lv_obj_set_style_text_color(lbl_desk_time, lv_color_hex(0x8AB4F8), 0);
  lv_obj_set_style_text_font(lbl_desk_time, &lv_font_montserrat_14, 0);
  lv_obj_align(lbl_desk_time, LV_ALIGN_TOP_RIGHT, -14, 18);

  /* app icons: 2x2 centered grid (52px tiles + name labels below) */
  ui_icon_create(scr_desktop, 58, 118, LV_SYMBOL_BELL,    "Clock",   0x2A5DB0, app_open_clock_cb);
  ui_icon_create(scr_desktop, 130, 118, LV_SYMBOL_REFRESH, "Weather", 0x1E8CA0, app_open_weather_cb);
  ui_icon_create(scr_desktop, 58, 214, LV_SYMBOL_CHARGE,  "ADC",     0x2E8C4E, app_open_adc_cb);
  ui_icon_create(scr_desktop, 130, 214, "+",              "Calc",    0xC07A2E, app_open_calc_cb);

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
  lbl_clock_date = lv_label_create(scr_clock);
  lv_label_set_text(lbl_clock_date, "----");
  lv_obj_set_style_text_color(lbl_clock_date, lv_color_hex(0x8AB4F8), 0);
  lv_obj_set_style_text_font(lbl_clock_date, &lv_font_montserrat_16, 0);
  lv_obj_align(lbl_clock_date, LV_ALIGN_CENTER, 0, 20);

  /* ===== Weather app page ===== */
  scr_weather = ui_app_page_create("Weather", 0x4FC3F7);
  lbl_w_temp = lv_label_create(scr_weather);
  lv_label_set_text(lbl_w_temp, "Temp: --");
  lv_obj_set_style_text_color(lbl_w_temp, lv_color_hex(0xFF8C42), 0);
  lv_obj_set_style_text_font(lbl_w_temp, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_w_temp, LV_ALIGN_TOP_MID, 0, 58);
  lbl_w_humi = lv_label_create(scr_weather);
  lv_label_set_text(lbl_w_humi, "Humi: --");
  lv_obj_set_style_text_color(lbl_w_humi, lv_color_hex(0x81C784), 0);
  lv_obj_set_style_text_font(lbl_w_humi, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_w_humi, LV_ALIGN_TOP_MID, 0, 92);

  /* history chart: temp on primary Y, humi on secondary Y, 60 points fed
   * once per second = 60s sliding window (stable lv_chart, straight lines) */
  chart_weather = lv_chart_create(scr_weather);
  lv_obj_set_size(chart_weather, 224, 168);
  lv_obj_align(chart_weather, LV_ALIGN_BOTTOM_MID, 0, -4);
  lv_chart_set_point_count(chart_weather, 60);
  /* initial ranges in x10 fixed point (250 = 25.0C); chart_update_range()
   * takes over as soon as the first valid sample arrives */
  lv_chart_set_range(chart_weather, LV_CHART_AXIS_PRIMARY_Y, 100, 400);
  lv_chart_set_range(chart_weather, LV_CHART_AXIS_SECONDARY_Y, 0, 1000);
  lv_chart_set_div_line_count(chart_weather, 4, 8);
  lv_obj_set_style_bg_color(chart_weather, lv_color_hex(0x141B24), 0);
  lv_obj_set_style_border_width(chart_weather, 1, 0);
  lv_obj_set_style_border_color(chart_weather, lv_color_hex(0x2A3B4D), 0);
  s_w_temp = lv_chart_add_series(chart_weather, lv_color_hex(0xFF8C42), LV_CHART_AXIS_PRIMARY_Y);
  s_w_humi = lv_chart_add_series(chart_weather, lv_color_hex(0x81C784), LV_CHART_AXIS_SECONDARY_Y);
  lv_obj_set_style_size(chart_weather, 0, LV_PART_INDICATOR);   /* no dots, just lines */

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

  /* ===== Calculator app page ===== */
  scr_calc = ui_app_page_create("Calc", 0xE5A64E);
  lbl_calc = lv_label_create(scr_calc);
  lv_label_set_text(lbl_calc, "0");
  lv_obj_set_style_text_color(lbl_calc, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_text_font(lbl_calc, &lv_font_montserrat_20, 0);
  lv_obj_align(lbl_calc, LV_ALIGN_TOP_RIGHT, -16, 46);

  lv_obj_t *bm = lv_btnmatrix_create(scr_calc);
  lv_btnmatrix_set_map(bm, calc_map);
  lv_obj_set_size(bm, 216, 232);
  lv_obj_align(bm, LV_ALIGN_BOTTOM_MID, 0, -8);
  lv_obj_set_style_bg_color(bm, lv_color_hex(0x141B24), 0);
  lv_obj_set_style_border_width(bm, 0, 0);
  lv_obj_set_style_pad_all(bm, 2, 0);
  lv_obj_set_style_radius(bm, 0, 0);
  lv_obj_set_style_pad_row(bm, 4, 0);
  lv_obj_set_style_pad_column(bm, 4, 0);
  lv_obj_set_style_bg_color(bm, lv_color_hex(0x1E2A38), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(bm, lv_color_hex(0x2A3B4D), LV_PART_ITEMS | LV_STATE_PRESSED);
  lv_obj_set_style_radius(bm, 8, LV_PART_ITEMS);
  lv_obj_set_style_text_color(bm, lv_color_hex(0xFFFFFF), LV_PART_ITEMS);
  lv_obj_set_style_text_font(bm, &lv_font_montserrat_20, LV_PART_ITEMS);
  lv_obj_add_event_cb(bm, calc_btn_cb, LV_EVENT_VALUE_CHANGED, NULL);

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
        /* only touch labels when the value actually changed (avoid useless redraw) */
        if (disp.temp_x10 != last_temp_x10)
        {
            if (disp.temp_x10 != 0x7FFF)
                lv_label_set_text_fmt(lbl_w_temp, "Temp: %d.%d C", disp.temp_x10 / 10,
                                      (disp.temp_x10 % 10 < 0) ? -(disp.temp_x10 % 10) : (disp.temp_x10 % 10));
            else
                lv_label_set_text(lbl_w_temp, "Temp: N/A");
            last_temp_x10 = disp.temp_x10;
        }
        if (disp.humi_x10 != last_humi_x10)
        {
            if (disp.humi_x10 != 0x7FFF)
                lv_label_set_text_fmt(lbl_w_humi, "Humi: %d.%d %%", disp.humi_x10 / 10, disp.humi_x10 % 10);
            else
                lv_label_set_text(lbl_w_humi, "Humi: N/A");
            last_humi_x10 = disp.humi_x10;
        }
        if (disp.adc_mv != last_adc_mv)
        {
            lv_label_set_text_fmt(lbl_adc_big, "%u mV", (unsigned)disp.adc_mv);
            lv_label_set_text_fmt(lbl_adc_raw, "raw: 0x%04X", (unsigned)disp.adc_raw);
            last_adc_mv = disp.adc_mv;
        }
    }

    /* RTC clock: refresh once per second (avoid useless redraw) */
    uint32_t now = osKernelGetTickCount();
    if (now - last_sec >= 1000)
    {
        static const char *wd[7] = {"Mon", "Tue", "Wed", "Thu", "Fri", "Sat", "Sun"};
        RtcTime t;
        last_sec = now;
        rtc_get_time(&t);
        lv_label_set_text_fmt(lbl_clock_big, "%02u:%02u:%02u",
                              (unsigned)t.hour, (unsigned)t.minute, (unsigned)t.second);
        lv_label_set_text_fmt(lbl_clock_date, "%04u-%02u-%02u %s",
                              (unsigned)t.year, (unsigned)t.month, (unsigned)t.day,
                              wd[t.weekday - 1]);
        lv_label_set_text_fmt(lbl_desk_time, "%02u:%02u",
                              (unsigned)t.hour, (unsigned)t.minute);

        /* weather chart: one sample/s; N/A breaks the line. Y range
         * adapts to the data so small variations are clearly visible */
        if (last_temp_x10 != 0x7FFF)
        {
            if (last_temp_x10 < chart_tmin) chart_tmin = last_temp_x10;
            if (last_temp_x10 > chart_tmax) chart_tmax = last_temp_x10;
        }
        if (last_humi_x10 != 0x7FFF)
        {
            if (last_humi_x10 < chart_hmin) chart_hmin = last_humi_x10;
            if (last_humi_x10 > chart_hmax) chart_hmax = last_humi_x10;
        }
        chart_update_range();
        lv_chart_set_next_value(chart_weather, s_w_temp,
                                (last_temp_x10 != 0x7FFF) ? last_temp_x10 : LV_CHART_POINT_NONE);
        lv_chart_set_next_value(chart_weather, s_w_humi,
                                (last_humi_x10 != 0x7FFF) ? last_humi_x10 : LV_CHART_POINT_NONE);
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

        case PROTO_CMD_SET_TIME:                /* 0x04: 校时 DATA=YY(2B) MM DD HH MM SS */
        {
            if (p->data_len >= 7)
            {
                RtcTime t;
                t.year = (uint16_t)((p->data[0] << 8) | p->data[1]);
                t.month = p->data[2];
                t.day = p->data[3];
                t.hour = p->data[4];
                t.minute = p->data[5];
                t.second = p->data[6];
                rtc_set_time(&t);
                printf("[RTC] time set: %04u-%02u-%02u %02u:%02u:%02u\r\n",
                       (unsigned)t.year, (unsigned)t.month, (unsigned)t.day,
                       (unsigned)t.hour, (unsigned)t.minute, (unsigned)t.second);
                reply_data[0] = 0;              /* 0 = success */
                len = 1;
            }
            else
            {
                reply_data[0] = 1;              /* 1 = bad length */
                len = 1;
            }
            break;
        }

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
