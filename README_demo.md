# STM32F103 + FreeRTOS + LVGL 桌面系统 (Demo)

![Platform](https://img.shields.io/badge/platform-STM32F103ZET6-blue)
![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS%20(CMSIS--OS2)-green)
![GUI](https://img.shields.io/badge/GUI-LVGL%20v8.2-orange)
![Display](https://img.shields.io/badge/display-2.8%22%20TFTLCD%20(FSMC)-lightgrey)

> 这是 UI 桌面系统的独立说明文档(与 `README.md` 的"数据采集与通信"主题互补)。
> 两个文档可对照阅读:`README.md` 讲底层(任务/队列/协议/Flash),本文件讲上层(LVGL 桌面 + 触摸 + App)。

一个"类手机系统"的嵌入式桌面:
**开机 → 手机风格桌面(彩色图标 + 状态栏时间)→ 触摸点击进入 App → 返回键回桌面**。
四个 App 都有真实数据:时钟(RTC 实时时间)、温湿度(大字 + 60s 历史曲线)、ADC(实时电压)、计算器。

对简历的一句话讲法:**基于 STM32F103 + FreeRTOS + LVGL 的多任务传感器桌面系统 —— 触摸交互、多页面切换、实时数据刷新全都有。**

---

## 功能演示清单

| # | 功能 | 说明 |
|---|------|------|
| 1 | 手机风格桌面 | 2×2 彩色圆角图标(渐变+阴影),右上角状态栏时间 `HH:MM` |
| 2 | 页面切换动画 | FADE_ON 200ms 淡入,切换流畅无闪烁 |
| 3 | 时钟 App | RTC(LSE 32.768kHz + VBAT 掉电续走)显示 `HH:MM:SS` + 日期星期 |
| 4 | 温湿度 App | SHT30 大字实时显示 + 60s 滑动曲线(自适应 Y 轴,0.1°C 分辨率) |
| 5 | ADC App | 电位器电压大字显示 + 原始值 |
| 6 | 计算器 App | 加减乘除、优先级、小数、%、退格、除零报 Error |
| 7 | 电阻触摸 | XPT2046,校准数据存 AT24C02,开机自动加载 |

---

## UI 架构

```
Display_Task (1KB 栈 + LVGL)
 ├─ lcd_init()        FSMC 驱动 2.8" TFTLCD (240x320, 16bpp)
 ├─ lv_init()          LVGL 8.2 (LV_MEM 16KB)
 ├─ rtc_init()         RTC 实时时钟(LSE), 首次上电自动用编译时间初始化
 ├─ tp_init()          触摸校准加载
 └─ 桌面 + 4 个 App 页(lv_scr_load_anim 切换)

数据流:
Sensor_Task (200ms) ──队列──> Display_Task ──> 温湿度大字 + 曲线 + ADC 大字
RTC (1s 中断可读)   ──────> Display_Task ──> 时钟页 + 桌面状态栏时间
触摸 (XPT2046)      ──────> lv_port_indev ──> LVGL 事件 → 图标点击 / 按键
```

页面全部常驻内存(创建一次,`lv_scr_load` 切换),切换零延迟、无重建开销。

---

## LVGL 移植要点(踩坑记录)

这套移植踩过的坑,换板子移植可对照避雷:

1. **触摸 `tp_scan()` 返回值恒为 0(正点原子例程 bug)**
   `tp_scan()` 返回类型是 `uint8_t`,但内部 `return tp_dev.sta & TP_PRES_DOWN`(0x8000)会被截断成 0 —— **判断按下必须查 `tp_dev.sta`,不能信返回值**。见 `Src/lv_port_indev.c`。

2. **软件延时精度(SPI 位错乱)**
   XPT2046 读取需要 1µs 级延时,NOP 循环在 72MHz 下偏短导致 AD 值 >4095(位错位)。改用 **DWT 周期计数器** 做精确 `delay_us()`,见 `Src/lcd.c`。

3. **btnmatrix 行分隔符 `"\n"`**
   LVGL 8.2 的 `lv_btnmatrix_set_map()` 必须用 `"\n"` 字符串当行分隔符,否则 20 个按钮全挤一行(显示乱码)。见计算器的 `calc_map`。

4. **`lv_snprintf` 不支持浮点**
   该版本 `LV_SPRINTF_USE_FLOAT=0`,`%.10g`/`%f` 格式无效(结果不显示)。需要自己写整数/小数分离格式化(见 `calc_format_result`),或改用定点运算。

5. **动画类型名是 `LV_SCR_LOAD_ANIM_FADE_ON`**
   不是 LVGL 9 的 `FADE_IN`。MOVE_LEFT 动画在低端屏会一帧渲染两屏,改用 FADE_ON 流畅很多。

6. **显示缓冲越大动画越流畅**
   `DISP_BUF_SIZE = 320*16`(单个缓冲,LVGL 8.2 支持单缓冲模式)。

7. **RTC 在精简 HAL 里缺宏**
   移植 F1 RTC 时 `__HAL_PWR_ENABLE_BKUPACCESS`/`__HAL_RCC_LSE_ENABLE` 在精简库中不存在,用寄存器等价写法 `SET_BIT(PWR->CR, PWR_CR_DBP)` / `SET_BIT(RCC->BDCR, RCC_BDCR_LSEON)`。

8. **注释/编码统一为 UTF-8**
   ARMCC5 编译 UTF-8 中文注释无问题;GBK/UTF-8 混用会在串口输出乱码。所有源文件已统一 UTF-8。

---

## 内存配置(64KB SRAM 预算是关键)

| 项 | 配置 | 说明 |
|----|------|------|
| LV_MEM_SIZE | 16KB | 实测峰值 ~10.2KB,余量充足 |
| 显示缓冲 | 320×16×2B ≈ 10KB | 静态数组,不占 LV_MEM |
| 总 ZI | ~57.5KB / 64KB | 余 ~6.5KB |
| 字体 | montserrat 14 / 16 / 20 | 含 LV_SYMBOL 符号区间 |

优化手段:共享样式(4 个图标共用一套 style 对象)、页面常驻复用、值变化才刷新 label(避免无效重绘)、字符串用静态缓冲。

**中文显示可行性**:16px 全字库约 270KB Flash(6763 常用字),SRAM 增加很小;当前 512KB Flash 完全放得下,只差一个字库转换工具。本项目暂未启用。

---

## 触摸接线(XPT2046,电阻屏)

| 触摸信号 | STM32 引脚 | 备注 |
|----------|-----------|------|
| T_CLK | PB1 | 软件 SPI 时钟(与 SHT30 共用引脚,分时复用) |
| T_CS | PF11 | 片选 |
| T_MISO | PB2 | 数据读 |
| T_MOSI | PF9 | 命令/数据写 |
| T_PEN | PF10 | 按下中断检测 |

校准数据存 **AT24C02**(软件 I2C),开机自动加载,无需每次校准。

> ⚠️ 屏标注"电容屏"可能实为电阻屏 —— 移植前先跑官方例程确认芯片 ID,别信外壳标签(本项目的真实踩坑)。

---

## 构建与烧录

```bash
# Keil CLI 构建(项目根 MDK-ARM/)
/d/Keil5/UV4/UV4.exe -r -j0 -o build_log.txt FreeRTOS_Project.uvprojx
# 产物: FreeRTOS_Project/FreeRTOS_Project.hex
```

- 工程: `MDK-ARM/FreeRTOS_Project.uvprojx`(ARMCC5,1 Error/1 Warning 以下)
- 烧录: ST-Link 下载 hex,或 Keil 内 Debug → Download
- 串口: 115200-8-N-1,启动日志可见 `[LVGL]` / `[TP]` / `[RTC]` 各阶段状态

---

## 阶段路线图(Readme.txt 执行记录)

| 阶段 | 内容 | 状态 |
|------|------|------|
| 0 | 最小 UI 验证 | ✅ |
| 1 | 完整 UI(深色主题 + 三行数据) | ✅ |
| 2 | 触摸接入 | ✅(含 tp_scan 返回值 bug 排查) |
| 3 | 桌面化(图标 + 页面切换) | ✅ |
| 4 | App 填充(时钟/温湿度曲线/ADC/计算器) | ✅ |
| 5 | 打磨(动画/内存优化/值变化刷新) | ✅ |
| 6 | 收尾(README + 提交) | ✅ 本文档 |

## 目录结构

```
FreeRTOS_Project/
├─ Inc/            # 应用头文件 (lv_conf.h, touch.h, rtc.h, protocol.h ...)
├─ Src/            # 应用源码 (freertos.c 任务+UI, lv_port_disp/indev, touch.c ...)
├─ Drivers/        # STM32F1xx HAL(精简版 + 补的 hal_rtc)
├─ Middlewares/    # FreeRTOS + LVGL 8.2
└─ MDK-ARM/        # Keil 工程
```
