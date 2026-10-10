# 固件资源与性能优化

## 构建与占用

目标为 STM32F407ZET6、SSD1963 和 800×480 屏幕。Flash 为 512 KiB，主 SRAM 为 128 KiB，CCM 为 64 KiB。
基线是 `5f36b74`，两次固件构建均使用 GNU Tools for STM32 14.3.1。Flash 按 ELF 的最高装载地址计算，包含对齐和 `.data` 初值。

| 项目 | 优化前 | 优化后 |
| --- | ---: | ---: |
| Flash | 425,260 B（81.1%） | 227,704 B（43.4%） |
| 主 SRAM 预算 | 78,664 B | 96,520 B（73.6%） |
| MSP 栈预留 | 1 KiB | 8 KiB |
| Dashboard 栈 | 8 KiB | 12 KiB |
| FreeRTOS 堆 | 32 KiB | 16 KiB |
| LVGL 内存池 | CCM 中 60 KiB | CCM 中 60 KiB |

Flash 节省 197,556 B，剩余 296,584 B。

主 SRAM 预算包含静态变量、FreeRTOS 堆和链接器预留。任务栈已经计入静态变量或 FreeRTOS 堆，不应重复相加。CCM 内存池通过 `LV_MEM_ADR` 指定地址，普通 `size` 输出不会列出它；CCM 的剩余 4 KiB 尚未分配。链接器禁止其他 `.ccmram` 数据覆盖内存池，并检查主 SRAM 能否容纳堆和 MSP 预留。

```bash
./build_fw.sh
./build_fw.sh clean
FW_OPT=-O0 ./build_fw.sh  # 排查优化相关问题时使用；默认恢复为 -Os
```

命令行构建需要 Python 3 生成内存报告。CubeMX 的堆大小、Idle 栈和主栈配置已同步。CubeIDE Debug 与命令行均使用 `-Os -g3`、Cortex-M4F 硬浮点。命令行构建在参数或工具链版本变化后自动重编对象。
产物位于 `stm32_port/build_vscode/`，包括 ELF、BIN、HEX、MAP 和 `.memory.json`。BIN 的烧录地址为 `0x08000000`；HEX 自带地址。
历史提交中的 `stm32_port/Debug/` 产物未更新，使用 CubeIDE 时需重新构建。

## 执行方式

LVGL 初始化、首帧绘制和后续刷新都由 Dashboard 任务执行。采用主 SRAM 中的 800×10 行 RGB565 缓冲区，共 16,000 B；FSMC 写屏循环保留局部 `-O3`。整个工程启用 `-Os`，GT911 软件 I²C 的等待循环单独保持 `-O0`，避免优化等级改变既有时序。该等待循环的机器指令与基线一致。触摸复位等待改用任务延时。

UI 跳过内容相同的标签更新。转速和扭矩变化不会重复刷新温度、在线状态未变的轮胎图形；温度文字未变时也不再重新计算位置。模式大字采用原 Montserrat 32 的 `C/E/Q/S` 字形，保留像素、字宽和基线。关闭未使用的 Flex、Grid 和 Observer 功能，保留圆角、复杂绘制和现有颜色格式。

| 任务 | 优先级 | 栈大小 | 分配方式 |
| --- | ---: | ---: | --- |
| CAN | 26 | 4 KiB | 静态 |
| GPS | 25 | 12 KiB | 静态 |
| Dashboard | 24 | 12 KiB | 静态 |
| Touch | 24 | 4 KiB | 静态 |
| SD | 1 | 3 KiB | 静态 |
| Idle | 0 | 1 KiB | CMSIS 静态回调 |
| Timer | 2 | 1 KiB | CMSIS 静态回调 |

CubeMX 生成的 defaultTask 只负责创建 Dashboard，随后删除自身。其 4 KiB 栈和控制块暂时使用 FreeRTOS 堆。应用任务创建失败会触发断言，禁止无声缺失。

CAN 接收中断每次最多读取硬件 FIFO 中的 3 帧，将帧送入 64 项静态队列。CAN 任务解析数据、转发方向盘命令并提交 UI 更新。短帧、远程帧和扩展帧不会进入标准数据帧解析路径。队列满时丢弃新帧并计数。CAN 标识符、字节序和发送周期保持原定义。

GPS 每 10 ms 读取循环 DMA，优先级高于绘制任务。配置接收机期间，每 10 ms 继续读取 DMA。512 B 缓冲区在 115200 baud、8N1 下约 44 ms 可绕回，因此长时间不读取会记录风险。异常航向和超范围数字直接拒绝，避免归一化死循环和有符号整数溢出。圈速、冲刺状态由互斥量保护；UI 的计时值和更新标志按一个事务交换。

SD 任务在调度器运行后加载里程文件，并发布缓存值。GPS 不等待 SD 文件操作，启动配置、DMA 解析和 CAN 遥测独立运行；里程累计在缓存就绪后启用一次。文件缺失或校验失败可使用另一份有效记录，两份均无效则从 0 开始；挂载、打开、读取或关闭发生 I/O 错误时暂停里程初始化与写入，每 10 s 重试，防止覆盖尚未读出的记录。

SD 的挂载、打开、同步和卸载使用互斥量串行化，挂载失败也解除卷注册；卡未就绪的轮询主动让出 CPU。保存采用双文件序号与校验，每次写入当前有效记录所在文件的另一份，成功同步、关闭后才切换文件，避免依赖序号奇偶关系。写入失败后慢速重试。

## 诊断与上板验证

在调试器中查看 `g_firmware_diag`。Dashboard 每秒采样一次，不调用串口打印，也不增加 UI 控件。

| 字段 | 含义 |
| --- | --- |
| `tasks[].stack_free_min_bytes` | 各任务历史最小剩余栈，单位为字节 |
| `stack_warning_mask` | 某任务剩余栈曾低于 512 B；位号对应 `firmware_task_id_t` |
| `heap_free_bytes` / `heap_free_min_bytes` | FreeRTOS 当前和历史最小空闲堆 |
| `lvgl_free_bytes` / `lvgl_largest_free_bytes` | LVGL 当前空闲量和最大连续空闲块 |
| `lvgl_used_peak_bytes` | LVGL 分配峰值 |
| `display_loop_max_cycles` | UI 处理与刷新循环的最长耗时，包含抢占时间 |
| `flush_max_cycles` | 一次 FSMC 局部刷新最长耗时，包含抢占时间 |
| `flushed_pixels` | 累计刷出的像素数，32 位计数可回绕 |
| `fault_code`、`cfsr/hfsr/mmfar/bfar/msp/psp/ipsr` | 故障类别和处理器现场 |

周期数除以 `SystemCoreClock` 即为秒。故障码：1 为 FreeRTOS 断言，2 为任务栈溢出，3 为 FreeRTOS 分配失败，4 为 Dashboard 创建失败，5 为 LVGL 断言；10、11、12、13 分别为 HardFault、MemManage、BusFault 和 UsageFault。原有 `g_freertos_fault_task_name` 继续保存出错任务名。

其他计数器：`g_can_rx_drop_count`、`g_can_rx_invalid_count`、`g_can_rx_queue_peak`、`g_user_can_tx_drop_count`、`g_gps_poll_gap_max_ms`、`g_gps_dma_late_poll_count`、`g_sd_odometer_load_fail_count` 和 `g_sd_odometer_save_fail_count`。GPS 的迟读计数表示可能覆盖数据，不能当作精确丢字节数。

上板时同时运行 CAN、20 Hz GPS、触摸、告警切换和 SD 保存。检查各任务栈余量、LVGL 最大空闲块、队列丢帧计数及 GPS 最长读取间隔。无卡和 SD 写失败时也应保持显示与 GPS 工作。实际帧率、栈最低余量和电气时序需要真机数据确认。

## 已执行的验证

```bash
python3 tools/subset_mode_font.py --check
python3 tools/run_firmware_tests.py --compare-ref 5f36b74
./run_sim_mac.sh --build-only
```

固件全量构建、增量构建和模拟器构建通过。切换 `-O0` 后对象会重编，恢复 `-Os` 的 BIN 与此前逐字节一致。主机回归直接编译固件应用代码与固件 LVGL，外设和 RTOS 使用模拟实现，并启用 AddressSanitizer、UndefinedBehaviorSanitizer。验证范围包括 CAN 队列饱和与异常帧；SD 启动 I/O 失败及恢复、空卡、损坏记录回退、序号回绕、短写和同步/关闭失败时保留原记录；GPS 异常数值、时钟回绕及 SD 未就绪时持续发送遥测；400 轮遥测、模式和告警更新。四种模式的 800×480 RGB565 图像与基线逐像素相同。

主机指针为 64 位，测试池使用 120 KiB；测试不代表 STM32 的 60 KiB 池余量，也不验证真实中断时序或 RTOS 竞争。目标内存余量由上板诊断确认。`.su` 报告只列单函数栈帧，不等于完整调用链所需栈。
