# 硬件映射

车手屏主板：STM32F407ZET6（512 KiB Flash、128 KiB 主 SRAM、64 KiB CCM），经 FSMC 驱动 SSD1963 控制的 800×480 IPS 屏，GT911 电容触摸。外设配置源是 `stm32_port/DATA_COLLECTION_INTEGRAL.ioc`；下表依据 `.ioc` 与代码整理，**未对照原理图**，电平、极性、上拉等电气事实以硬件设计文件为准。

## 时钟

| 项 | 值 | 来源 |
| --- | --- | --- |
| HSE | 8 MHz | `.ioc` `RCC.HSE_VALUE` |
| PLL | M=8、N=336、P=2、Q=7，VCO 336 MHz | `main.c` `SystemClock_Config` |
| SYSCLK = HCLK | 168 MHz，Flash 等待 5 周期 | 同上 |
| APB1 / APB2 | 42 MHz / 84 MHz（定时器时钟 84 MHz / 168 MHz） | 同上 |
| LSE | 32.768 kHz（RTC 已初始化，应用层未使用） | `.ioc` |
| HAL 时基 | TIM8 更新中断，优先级 15；FreeRTOS 用 SysTick | `.ioc`、`main.c` |

不使用看门狗（`.ioc` 无 IWDG/WWDG）。`Error_Handler` 关中断后死循环，需断电或调试器复位。

## 引脚

| 功能 | 引脚 | 说明 |
| --- | --- | --- |
| LCD 数据 D0..D15 | PD14、PD15、PD0、PD1、PE7..PE15、PD8、PD9、PD10 | FSMC 16 位 |
| LCD 片选 / 读 / 写 | PD7（NE1）、PD4（NOE）、PD5（NWE） | Bank1 NE1，基址 `0x60000000` |
| LCD 命令/数据选择 | PD11（FSMC_A16） | 16 位总线下 A16 对应地址位 17：写 `0x60000000` 为命令，写 `0x60020000` 为数据 |
| SSD1963 复位 | PG3（`SSD1963_RST`） | 低电平复位 |
| 背光 | PF9 | 上电为 GPIO 输出；UI 初始化后改为 TIM14_CH1（AF9）PWM，见下 |
| 触摸 GT911 SCL / SDA | PB6、PB7 | 软件 I²C，不使用 I2C1 外设（初始化时 `HAL_I2C_DeInit`） |
| 触摸中断 / 复位 | PG4（`IPS_INT`，EXTI4 上升沿，下拉）、PG5（`IPS_RST`） | 复位时序同时决定 GT911 的 I²C 地址（0x5D 或 0x14，代码两者都探测） |
| 触摸 XPT2046（备用） | SPI1：PA5/PA6/PA7，CS = PC13（`TOUCH_CS`） | 仅 `TOUCH_TYPE_CAP=0` 时编译使用，当前为 1 |
| 指示灯 | PD12（红）、PD13（绿） | 代码中 SET 表示点亮；编码见下 |
| CAN1（整车 CANB） | PB8（RX，上拉）、PB9（TX） | 500 kbit/s |
| CAN2（方向盘） | PB12（RX，上拉）、PB13（TX，上拉） | 500 kbit/s，只收 |
| GNSS（UM982） | USART3：PB10（TX）、PB11（RX，上拉） | 115200 8N1，循环 DMA 接收 |
| 圈速差输入、诊断 CLI | USART2：PA2（TX）、PA3（RX） | 9600，行命令，见下；诊断命令行见 [串口诊断 CLI](diagnostics.md) |
| USART1 | PA9（TX）、PA10（RX，上拉） | 9600，已初始化，应用层未使用 |
| USART6 | PC6（TX）、PC7（RX） | 115200，已初始化，应用层未使用 |
| 调试 | PA13（SWDIO）、PA14（SWCLK） | |
| 未标注 | PC2（输入）、PC3（输出） | `.ioc` 无标签，`touch.c` 未引用，用途待查 |

## CAN 控制器

CAN1、CAN2 参数相同（`can.c`）：预分频 6，42 MHz ÷ 6 = 7 MHz，每位 14 个时间片（同步 1 + BS1 12 + BS2 1），采样点约 92.9%，SJW 1；自动 bus-off 恢复开，自动重传开，自动唤醒和时间触发关，接收 FIFO 不锁定，发送按标识符优先级。CAN1 过滤器占 bank 0..5，CAN2 为 bank 14（`SlaveStartFilterBank = 14`）。收发内容见 [CAN 接口](can_interface.md)。

## LCD（SSD1963 + FSMC）

FSMC Bank1 NE1，16 位 SRAM/NOR，访问模式 A，**读写时序分开**：

| 阶段 | 地址建立 | 地址保持 | 数据建立 | 总线周转 | 来源 |
| --- | ---: | ---: | ---: | ---: | --- |
| 读 | 15 | 15 | 60 | 0 | `.ioc`，`fsmc.c` 生成部分 |
| 写（`.ioc`） | 5 | 15 | 15 | 15 | `.ioc`、`fsmc.c` 生成部分 |
| 写（实际生效） | 3 | 2 | 8 | 1 | `fsmc.c` `FSMC_Init 2` USER 区，初始化后重写扩展时序 |

单位为 HCLK 周期（168 MHz 下约 5.95 ns）。实际生效的写时序比 `.ioc` 更短，是为缩短整屏写入时间的有意改动，且比更早试过的 2/6/0 更保守；**重新生成代码后该 USER 区必须仍在**。读时序只用于 `SSD1963_ReadID`。

`main.c` 的 `SSD1963_Init` 按屏厂序列配置：分辨率 800×480，水平总周期 1056、垂直总周期 525，像素接口 16 位（RGB565），PLL N=29、M=2，LSHIFT `0x03FFFF`。面板的数据宽度和极性来自屏厂序列，未对照面板手册。LVGL 通过 `lvgl_flush_cb` 设置窗口后连续写 `0x60020000`，写循环保留局部 `-O3`。

## 背光

SSD1963 自带 PWM 不用，背光由 PF9 控制。`gpio.c` 把 PF9 初始化为推挽输出并置低（背光关）。`Dashboard_UI_Init` 末尾的 `dashboard_backlight_init` 把 PF9 重配为 TIM14_CH1（AF9）PWM 并以 100% 启动：84 MHz ÷ 84 = 1 MHz 计数、周期 1000 → **1 kHz**，日间 100%（比较值 1000），夜间 45%（`BACKLIGHT_NIGHT_COMPARE` = 450）。PWM 初始化失败时回退为 GPIO 常亮。此重配不在 `.ioc` 中，`.ioc` 里 PF9 仍是普通输出。

背光因此在 `Dashboard_UI_Init` 内点亮，早于其后 `lv_refr_now` 的首帧；之后 `LVGL_Port_Init` 里的 `LCD_Backlight_On()` 只写 GPIO 输出寄存器，PF9 已是复用功能，不再起作用。`main.c` 注释称「首帧后开背光」与此不符，上板确认前不要依赖。

## 指示灯编码

启动阶段（`LED_Diag_SetBootStage`）：

| 阶段 | 红 | 绿 | 含义 |
| ---: | :-: | :-: | --- |
| 0 | 亮 | 灭 | 复位/背光关，LCD 初始化前 |
| 1 | 亮 | 亮 | SSD1963 初始化完成 |
| 2 | 灭 | 亮 | 首帧绘制完成，背光已开 |
| 3 | 亮 | 灭 | UI 初始化中，夜间图标未建 |
| 4 | 亮 | 亮 | UI 初始化已过夜间图标分配 |
| 5 | 灭 | 灭 | UI 初始化全部完成 |

3、4 与 0、1 外观相同，靠启动顺序区分。运行时绿灯每 250 ms 翻转表示 Dashboard 任务存活。故障（`freertos.c`、`stm32f4xx_it.c`，同时写入 `g_firmware_diag.fault_code`）：

| 状态 | 红 | 绿 | 故障码 |
| --- | :-: | :-: | ---: |
| `configASSERT`（含 LVGL 断言） | 亮 | 亮 | 1 |
| 任务栈溢出 | 亮 | 灭 | 2 |
| FreeRTOS 堆耗尽 | 灭 | 亮 | 3 |
| Dashboard 任务创建失败 | 亮 | 亮 | 4 |
| HardFault / MemManage / BusFault / UsageFault | 亮 | 灭 | 10 / 11 / 12 / 13 |

以上故障路径都停机等待复位；诊断字段见 [固件资源与性能优化](firmware_optimization.md)。

## 串口 USART2 圈速差输入

USART2 以单字节中断接收（`diag_cli.c` 收成行，接收出错后在 `HAL_UART_ErrorCallback` 里重启），该串口同时是诊断命令行，首个词是命令的行归 CLI，其余非空行交给圈速差解析。收到形如 `+1.23`、`-0.5` 的一行（以换行结束；除符号、数字和小数点外的字符被忽略，必须以 `+` 或 `-` 开头，最多保留两位小数）后，作为圈速差 ±秒 提交到界面（`usart.c`，`Dashboard_UI_SubmitLapDelta`）。发送方和使用场景未在仓库中记录。

## 与 `.ioc` 不一致的有意改动

重新生成代码前后必须核对：

| 项 | `.ioc` | 实际 |
| --- | --- | --- |
| FSMC 写时序 | 5/15/15/15 | 3/2/8/1，`fsmc.c` USER 区 |
| PF9 | 普通输出 | 运行时改 TIM14_CH1 PWM，`dashboard_ui.c` |
| 触摸 I²C | I2C1 外设（PB6/PB7） | 软件 I²C，`gt911.c` |
| LVGL 内存池 | 无 | 固定在 CCM `0x10000000`，64 KiB 中 60 KiB，链接脚本 `Memory_Budget` 禁止其他数据占用 |
| 程序区 | Flash 512 KiB | 链接脚本断言程序镜像不超过 384 KiB，最后一个扇区存里程，见 [里程存储](odometer_storage.md) |
| UART 回调 | 无 | `usart.c` USER 区的 `HAL_UART_RxCpltCallback`、`HAL_UART_TxCpltCallback`、`HAL_UART_ErrorCallback`，转发给 USART2 的诊断 CLI（收发与错误重启）和 USART3 的 GNSS 错误恢复；重新生成后必须仍在，否则 GNSS 接收出错后不会恢复 |

## 失败行为

- GT911 初始化失败：Touch 任务注销诊断并删除自身，**不重试**，触摸失效而显示照常。
- GPS 上电 5 s 内收不到 NMEA：仍继续发送配置命令，随后进入正常循环。
- 里程读写、CAN 队列满、GNSS 超时等行为见对应文档：[里程存储](odometer_storage.md)、[CAN 接口](can_interface.md)、[GPS 与圈速](gps_lap.md)。
