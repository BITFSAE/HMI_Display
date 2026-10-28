# 方向盘相关代码

本文是 [方向盘与车手屏](steering_wheel.md) 的代码级补充：方向盘按钮板固件怎么实现，车手屏怎么接收和处理方向盘的帧，以及哪些行为有测试。系统层面的分工和操作流程不在这里重复。

方向盘固件在 `Steering_wheel` 仓库（本地同级目录 `../Steering_wheel/`），本文依据 2026-10-28 改为整帧发送 `0x703` 并加入退使组合键后的代码。引脚表、构建方法见该仓库的 `README.md`，这里不复制。

## 方向盘按钮板固件

### 模块

| 文件 | 内容 |
| --- | --- |
| `Core/Src/button_panel.c`、`Core/Inc/button_panel.h` | 全部业务逻辑：去抖、旋钮译码、ADC 滤波、退使组合键、两个发送函数；CAN ID、`0x703` 位定义和参数宏 |
| `tests/` | `button_panel.c` 的主机单元测试（HAL 桩），`tests/run_host_tests.sh` 运行 |
| `Core/Src/main.c` | 初始化 GPIO、ADC1、CAN1、TIM1，启动 CAN，调用 `ButtonPanel_Init()`，循环调用 `ButtonPanel_Process()` |
| `Core/Src/can.c` | 位时序、过滤器、发送头 `TxHeader`，以及遗留的接收解析 |
| `ecu_info.c`、`usart.c`、`dma.c`、`delay.c`、`sys.c`、`fsae_telemetry.pb.h` | 早前 interface 工程的模板遗留，功能不使用 |

### 运行节拍

主循环无阻塞，反复调用 `ButtonPanel_Process()`，内部按 `HAL_GetTick()` 分任务：

| 任务 | 周期 | 说明 |
| --- | --- | --- |
| 扫描按键、旋钮、电位器 | 10 ms（`BUTTON_SCAN_PERIOD_MS`） | 每次扫描依次：扫两个按键、推进退使请求、更新旋钮、采一次 ADC |
| LED2 心跳 | 500 ms 翻转 | 1 Hz |
| LED1 | 事件发送时拉低 100 ms | 低电平点亮 |

### 按键

- 输入低有效、内部上拉。去抖：连续 4 次采样（`BUTTON_DEBOUNCE_CNT`）与当前稳定电平不同才翻转，响应约 40 ms。
- 单键：稳定电平由 0（按下）变回 1（松开）时发一帧 `0x703`，KEY1 置 byte1 bit2（`LapTimerToggle`），KEY2 置 bit1（`ErrorClearActive`）。按住期间不发，没有长按、连发或 0 帧。
- 两键：只要两个键同时处于按下状态，两者都被标记为“已占用”（`key1_consumed`、`key2_consumed`），松开时不再发单键动作，标记在各自松开时清除。
- 退使组合键（`Update_Disable_Request()`）：KEY1 按下时 KEY2 已在上一次扫描中处于按下状态，才进入待触发（`chord_armed`）；两键持续按住 `CHORD_HOLD_MS`（300 ms）后开始请求，每 `DISABLE_TX_PERIOD_MS`（20 ms）发一帧 byte1 bit0=1，直到任一键松开且已发满 `DISABLE_MIN_FRAMES`（3）帧，最后补一帧 bit0=0。请求期间因滑级变化发出的帧也带 bit0=1。先按 KEY1、或同一扫描周期内同时按下，都不触发。再次请求需要松开 KEY1 再按。
- 发送函数：`Send_CAN_SteeringCmd(event_bits)` 发 `0x703`，DLC 2，byte0 为当前滑级，byte1 为本次事件位，请求进行中再或上 bit0。

### 旋钮

- S1..S4 四路输入各自去抖。恰好一路为低才有效，对应内部档位 0..3；零路或多路为低判为无效，值为 `0xFF`。
- 发送 `0x310`：DLC 8，byte0 = 档位 + `0x30`（ASCII `0`..`3`），无效时 byte0=`0xFF`，byte1..7 为 0。
- 变化检测用 `last_sent_drive_mode`，初值也是 `0xFF`，与无效值相同。所以上电后只有读到有效档位才会发出第一帧，约在上电后 30 ms 去抖完成时；没有有效档位则不发。

### 电位器

- ADC1 通道 1（PA1），每次扫描启动一次转换，`HAL_ADC_PollForConversion` 超时 1 ms，超时则本次不更新。
- 16 点滑动平均（`ADC_FILTER_DEPTH`），`滑级 = 平均值 × 8 ÷ 4096`，12 位量程均分为 8 档，每档 512 个计数，档间没有回差。电位器停在档界附近时，噪声会让滑级在相邻两档之间跳动，每跳一次发一帧 `0x703`，车手屏随之各转发一帧。
- 变化检测用 `last_sent_slip_level`，初值 `0xFF`，所以上电后第一次扫描必发一帧。

### CAN 配置

| 项 | 值 |
| --- | --- |
| 位时序 | 36 MHz ÷ 4 = 9 MHz，18 个时间片（同步 1 + BS1 14 + BS2 3），500 kbit/s，采样点约 83.3% |
| 模式 | 正常模式，自动重传开，**自动 bus-off 恢复关**，发送优先级按 ID |
| 中断 | 只开 FIFO0 接收通知；错误和 bus-off 通知未开，`HAL_CAN_ErrorCallback` 实际不会被调用 |
| 发送 | `HAL_CAN_AddTxMessage`，不检查返回值；三个邮箱都占用时本帧静默丢弃，不重试 |
| 过滤器 | bank 0 放 `0x401`、`0x501`、`0x502`（重复一次占位），bank 1 放 `0x503`、`0x504` |

车手屏 CAN2 的位时序见 [硬件映射](hardware.md#can-控制器)：42 MHz ÷ 6 = 7 MHz，14 个时间片，采样点约 92.9%。两边位率相同、采样点不同，在两个节点的短总线上属于允许范围，未做总线实测。

发送头 `TxHeader` 是全局变量。IMU 转发删除前，它在接收中断和主循环里都会被改写；现在运行中的代码只在主循环里使用它。

### 遗留代码

- `can.c` 的接收中断解析 `0x401/0x501/0x502/0x503/0x504`，写入 `ECU_INFO`，不外发。车手屏的 CAN2 不发送，方向盘总线上不会出现这些帧，所以这段代码不会被触发。
- `usart.c` 的 USART1 回调里有一条 LoRa 命令路径：收到 `M` 加一个字节和换行后，把该字节作为 byte0 发送 `0x310`。`main()` 没有初始化 USART，它不会运行；启用前须先评审，否则等于给总线加了第二个 `0x310` 发送源。
- `can.c` 里 GBK 编码的中文注释在 UTF-8 下已是乱码，不影响功能。

## 车手屏侧

### 代码位置

| 位置 | 作用 |
| --- | --- |
| `stm32_port/Core/Src/can.c` `CAN2_Filter_Config` | bank 14，ID 列表放 `0x310`、`0x703`（各两次），进 FIFO0 |
| `User_CAN_ReceiveIRQ` | 接收中断：每次最多读 3 帧，丢弃扩展帧/远程帧/DLC>8，UI 启动完成前的帧直接丢弃；其余放入接收队列，并标记来自 CAN1 还是 CAN2 |
| `User_CAN_ProcessFifo0` | CAN 任务里取帧；来自 CAN2 的帧检查 DLC ≥ 1，交给 `User_CAN_HandleSteeringWheel` |
| `User_CAN_HandleSteeringWheel` | 按 ID 分两种处理，见下 |
| `User_CAN_ButtonPressed` | 按键“新按下”判定 |
| `dashboard_ui.c` `Dashboard_UI_RequestLapToggle`、`Dashboard_UI_RequestAlertClear`、`Dashboard_UI_RequestDisableAlert` | 只置位一个待处理标志，由 Dashboard 任务在 `Dashboard_UI_Process` 里取走：计时开关走 `dashboard_toggle_lap_analysis`，清告警走 `dashboard_clear_alerts`，退使提示在清告警之后压入 `DISABLE REQUESTED` |
| `Dashboard_UI_RequestDriveMode`、`Dashboard_UI_SubmitDriveMode` | 方向盘 `0x310` 用前者覆盖待应用请求；ECU `0x509` 用后者校正显示，旧回报不撤销新请求。计时锁在两者内判断 |
| `Dashboard_UI_SubmitSlipLevel` | 提交滑级 |
| `Dashboard_UI_RequestDriveModeCycle` | 触摸模式色块只置位轮转请求；Dashboard 任务检查计时锁、发送 `0x310` 并提交下一模式 |
| `CAN_SendDriveModeRequest` | 组装 `0x310`（编码见 [CAN 接口](can_interface.md)，DLC 8）进入 CAN1 发送队列；返回是否入队 |
| `Dashboard_UI_IsTimingModeLocked` | 返回 AC 模式冲刺是否进行中，或其他模式圈速分析是否进行中 |

### 两种帧的处理

| ID | 处理 |
| --- | --- |
| `0x310` | byte0 不在 47..51 则丢弃并计入 `g_can_rx_invalid_count`。否则：锁未生效时以原 ID、原 DLC 调用 `User_CAN_SendDlc` 转发；无论是否转发，都调用 `Dashboard_UI_RequestDriveMode(byte0 − 48)`，锁生效时该函数拒绝切换并请求 MODE LOCK 告警 |
| `0x703` | DLC < 2 则丢弃并计数。否则先以原 ID、原 DLC 调用 `User_CAN_SendDlc` 转发（不看计时锁）；byte0 ≤ 7 时 `Dashboard_UI_SubmitSlipLevel`，否则计数、不提交；再对 byte1 bit2、bit1、bit0 分别用 `User_CAN_ButtonPressed` 判定上升沿，依次调用 `Dashboard_UI_RequestLapToggle`、`Dashboard_UI_RequestAlertClear`、`Dashboard_UI_RequestDisableAlert` |

车手屏不再保存滑级，也不再自己组 `0x703`；转发帧进入 32 项发送队列，队列满时与其他帧一样丢弃并计入 `g_user_can_tx_drop_count`。

`User_CAN_ButtonPressed` 的规则：该位为 1 且（上一帧该位为 0，或距上一个 1 超过 300 ms）才算新按下；每个为 1 的帧都会刷新“上一个 1”的时间，所以间隔小于 300 ms 的连续 1 不会重复触发。计时和清错位方向盘只发单帧，两次单键之间若没有滑级等其他帧，300 ms 就是两次按下的最小间隔；退使位每 20 ms 一帧，整个请求只触发一次。常量见 [配置表](configuration.md)。

### 测试

回归测试 `tools/tests/firmware_regression.c` 的 `test_steering` 覆盖：

- `0x310` 越界值丢弃并计数，合法请求原样转发；默认模式请求和计时锁由模式回归用例验证；
- CAN2 过滤器只放 `0x310`、`0x703`；`0x700..0x702` 到达也不处理、不转发；
- `0x703` 原样转发（DLC、byte0、byte1 不变），DLC 不足丢弃，滑级越界照常转发但不提交界面；
- `LapTimerToggle`（bit2）的上升沿：重复的 1 不重复触发，`0 → 1` 触发，间隔 300 ms 以内不触发、超过 300 ms 触发；只置 bit0 不触发计时；
- `ErrorClearActive`（bit1）同理；
- `DisableRequest`（bit0）：计时锁期间每帧照常转发，连续的 1 只请求一次提示，`Dashboard_UI_Process` 后告警队列里有 `DISABLE REQUESTED`，结束的 0 帧之后再次请求会再提示；
- 模式色块：命中区、五档轮转与回绕、`0x310` 内容和 DLC、长按门限边界、按住不连发、松手抖动与移出取消、计时锁拒绝，以及越界模式拒绝和发送队列满后的恢复。

未覆盖：计时位、清错位触发后界面端的实际动作（计时开始/停止、清告警）。方向盘固件的单键与组合键行为由 `Steering_wheel` 仓库的 `tests/run_host_tests.sh` 覆盖。

### 验证命令

```bash
python3 tools/run_firmware_tests.py   # 回归测试，含 test_steering
./build_fw.sh                         # 固件构建
```

方向盘固件：在 `Steering_wheel` 仓库执行 `cmake --preset Debug && cmake --build build/Debug`，单元测试为 `tests/run_host_tests.sh`。
