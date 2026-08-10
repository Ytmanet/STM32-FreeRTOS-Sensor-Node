基于FreeRTOS的嵌入式数据采集与通信协议处理系统 – 技术蓝图
一、项目概述
本系统基于 STM32ZET6 与 FreeRTOS 实现一个多任务嵌入式数据采集与通信节点。通过 I2C 获取温湿度传感器（SHT30）数据，通过 ADC 采集模拟量（电位器），使用 TFTLCD（MCU屏）实时显示数据，通过 UART 与上位机按照自定义协议交互，支持 Flash 参数掉电保存。系统突出 DMA + IDLE 中断接收、环形缓冲区、CRC16 校验、FreeRTOS 多任务同步等嵌入式核心能力。

二、硬件清单
模块	接口	说明
STM32ZET6 最小系统板	–	主控
SHT30 温湿度传感器	I2C	I2C1 或 I2C2
电位器	ADC	接任意 ADC 通道（如 PA0）
TFTLCD 模块（MCU屏）	FSMC 或 SPI	具体驱动取决于模块型号，推荐 FSMC 方式（速度更快）
USB 转串口模块	UART	USART1，用于与 PC 通信
按键（可选）	GPIO	用于触发参数保存或显示切换
杜邦线/面包板	–	连接电路
三、系统功能
数据采集：周期读取 SHT30（温度、湿度）和 ADC（电压值）。
任务调度：FreeRTOS 管理三个任务（采集、通信、显示）。
通信协议：自定义帧结构（帧头 + ID + CMD + LEN + DATA + CRC16），支持主动上报和命令响应。
UART 接收：DMA + IDLE 中断 + 环形缓冲区，实现无阻塞接收并解决粘包。
CRC16-MODBUS 校验：保证数据传输完整性。
Flash 参数存储：设备 ID、采样周期等参数掉电保存。
LCD 显示：实时刷新温度、湿度、ADC 值、系统状态。
四、FreeRTOS 任务设计
系统共创建 3 个任务 + 空闲任务。

任务名	优先级	周期/触发方式	功能	同步方式
Sensor_Task	2（中）	200ms 定时	读取 SHT30 和 ADC，组包后通过队列发送给 UART_Task 和 Display_Task	发送到两个消息队列（QueueHandle_t q_sensor2uart, q_sensor2display）
UART_Protocol_Task	3（高）	事件驱动（队列 + UART 接收）	处理传感器数据发送协议帧；解析上位机命令（设置参数、查询等）；调用 Flash 写入函数	接收队列；信号量保护 UART 发送（可选）
Display_Task	1（低）	300ms 定时或队列接收更新	从队列获取最新数据，刷新 LCD 显示内容	接收队列
任务间数据流：

Text
Sensor_Task ——[queue]→ UART_Protocol_Task
Sensor_Task ——[queue]→ Display_Task
UART_Protocol_Task (通过环形缓冲区获取UART数据)
注意：UART_Protocol_Task 同时处理发送和接收，接收部分通过循环读取环形缓冲区完成（无阻塞等待）。CRC 校验和组帧均在该任务中执行。Flash 写入操作（耗时约几毫秒）允许在此任务内直接调用，因为 FreeRTOS 会切到其他就绪任务。

五、关键模块详细设计
5.1 协议帧格式
Text
| 0xAA(1B) | ID(1B) | CMD(1B) | LEN(1B) | DATA(LEN) | CRC16(2B) |
ID：设备编号，从 Flash 读取。
CMD：
0x01：主动上报传感器数据（上行）
0x02：设置参数（下行），如修改采样周期、设备 ID 等
0x03：查询设备信息（下行），回复包含固件版本、ID 等
LEN：DATA 字段字节数（0~64）。
CRC16：MODBUS CRC16，覆盖 ID、CMD、LEN、DATA 字段。
接收状态机（3 状态）：

Text
enum {
    FRAME_IDLE,          // 等待 0xAA
    FRAME_HEADER,        // 已收到 0xAA，等待 ID+CMD+LEN（3字节）
    FRAME_PAYLOAD,       // 根据 LEN 读 DATA，然后读 2 字节 CRC
};
在 FRAME_PAYLOAD 状态下，数据长度变量 l = LEN，每收到一个字节存入缓冲区，计数递减。当 l==0 后读取 CRC 并校验。
5.2 UART DMA + IDLE 中断 + 环形缓冲区
配置：

UART：115200-8-N-1
DMA：循环模式（Circular），数据宽度 1 字节，缓冲区大小 256 B。
IDLE 中断：当一帧数据接收完毕（总线空闲）时产生。
处理流程：

DMA 自动将串口数据搬运到接收缓冲区（循环写入）。
产生 IDLE 中断后，在中断服务函数中：
关闭 DMA 循环（HAL_UART_DMAStop）。
计算本次接收的数据长度（公式：len = buff_size - __HAL_DMA_GET_COUNTER(huart->hdmarx)）。
将读取到的数据写入环形缓冲区。
重新开启 DMA 循环。
UART_Protocol_Task 循环检查环形缓冲区是否有新数据，取出后送入状态机解析。
环形缓冲区实现：

C
typedef struct {
    uint8_t buffer[256];
    volatile uint16_t head;
    volatile uint16_t tail;
} RingBuf;
写指针（head）由中断服务更新；读指针（tail）由任务更新。
当 head==tail 时缓冲区为空；当 (head+1) % size == tail 时缓冲区满（留一个空位区分空/满）。
5.3 CRC16-MODBUS
使用查表法（256 字表），计算函数原型：

C
uint16_t crc16_modbus(const uint8_t *data, uint16_t len);
多项式：0x8005
初始值：0xFFFF
输出异或：0x0000
发送前计算 CRC 并填入帧尾；接收后计算并比对。

5.4 Flash 参数存储
将系统参数定义为一个结构体：

C
typedef struct {
    uint8_t  device_id;
    uint16_t sample_interval_ms;  // 采样周期
    uint16_t crc;                 // 结构体 CRC 校验，检查有效性
} SysParam;
存储位置：选择 STM32ZET6 最后一个扇区（Sector 11，地址 0x080C0000，大小 64KB，需根据具体器件验证），或用户指定的空闲扇区。
写入操作：
解锁 Flash（HAL_FLASH_Unlock）。
擦除整个扇区（HAL_FLASHEx_Erase）。
依次写入各参数（HAL_FLASH_Program）。
锁定 Flash。
读取：直接指针访问（const SysParam *p = (const SysParam*)0x080C0000;），再校验 CRC 判断参数是否有效。
5.5 LCD 显示
TFTLCD 模块（MCU屏）驱动：

若为 FSMC 接口（如正点原子 2.8 寸 TFT），可直接使用 FSMC 时序模拟 8080 并口，速度快。
若为 SPI 接口，则使用 SPI DMA 或普通发送。
驱动内容：初始化、填色、显示字符或数字（可从开源库中提取简化版）。
显示内容（示例布局）：
Text
Sys: ON   UART: OK
Temp: 25.6 C
Humi: 60.0 %
ADC:  3.12 V
ID: 01
由于 Display_Task 优先级最低，LCD 刷新可在任务内直接调用驱动函数（无需过多保护，但如果其他任务也使用 SPI/FSMC 则需要互斥）。
六、实现路线（5 周）
周次	目标	详细内容
第1周	基础驱动与环境	1. 搭建开发环境（Keil/IAR，HAL库+FreeRTOS）。
2. 点亮板载 LED 验证最小系统。
3. 移植 FreeRTOS，创建两个简单任务（LED 闪烁）。
4. 驱动 SHT30（I2C），通过 printf 打印数据。
5. 驱动 ADC（电位器），DMA 单次采集。
第2周	LCD 驱动与显示	1. 移植 TFTLCD 驱动（FSMC 或 SPI），点亮屏幕。
2. 实现画点、画线、显示数字/字符的函数。
3. 将传感器数据（轮询）显示在屏幕上。
第3周	通信协议 + UART DMA 接收	1. 实现自定义协议组帧与发送（主动上报，固定格式）。
2. 使用 DMA 发送（UART_Transmit_DMA）。
3. 实现 IDLE 中断 + 环形缓冲区接收。
4. 实现状态机解析，检验 CRC 并回复 ACK。
第4周	FreeRTOS 多任务集成	1. 创建三个任务（Sensor、UART_Protocol、Display），用队列交换数据。
2. 调试任务优先级与堆栈大小，确保系统稳定。
3. 实现上位机命令解析（设置参数、查询信息）。
第5周	Flash 参数存储 + 系统收尾	1. 实现 Flash 写入/读取模块。
2. 系统启动时读取参数，支持通过串口命令修改并保存。
3. 添加软件看门狗（IWDG 或任务监控）。
4. 整体联调，记录测试数据，整理代码与文档。
七、测试与验证
传感器测试：用串口助手观察周期上报的温湿度、ADC 值，与真实值比对。
协议测试：发送格式错误的帧，验证 CRC 校验是否丢弃；发送正确的命令帧，等待 ACK。
LCD 显示测试：观察显示内容是否随传感器数据同步更新（允许几百毫秒延迟）。
Flash 存储测试：修改设备 ID 后重启，确认读取到的 ID 为新值；写满 Flash 扇区边界测试。
压力测试：以 20ms 间隔连续发送命令帧，观察系统是否死机或漏帧。
掉电测试：运行中突然断电，重新上电后参数仍正确。