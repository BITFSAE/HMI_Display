# 配置表

常改参数的位置、单位和影响。所有项都是**编译期常量**，改动后需重新构建并烧录，没有运行时命令可以覆盖，也没有保存在 Flash 里的设置。唯一的持久数据是里程（内部 Flash 扇区 7，见 [里程存储](odometer_storage.md)）和 UM982 自身保存的配置（每次上电 `SAVECONFIG`，见 [GPS 与圈速](gps_lap.md)）。外设、时钟和引脚属于 `.ioc`，见 [硬件映射](hardware.md)。

## GNSS 与速度（`gps.c`、`gps.h`）

| 设置 | 当前值 | 符号 | 单位/范围 | 影响与同步 |
| --- | --- | --- | --- | --- |
| 界面车速保持时间 | 3000 | `GPS_SPEED_UI_HOLD_MS` | ms | RMC 中断或状态为 V 后界面保持最后有效车速，超时清零；CAN 车速不受影响 |
| CAN 车速过期 | 500 | `GPS_CAN_SPEED_STALE_MS` | ms | RMC 超过此时间未更新，`0x301` 车速报 0 |
| GPS 帧发送节拍 | 50 | `GPS_CAN_SPEED_PERIOD_MS` | ms | `0x301/0x067/0x068` 周期；改动需同步中央库登记 |
| `0x7A0` 周期 | 10 拍 = 500 ms | `GPS_CAN_STATUS_TICKS` | 节拍数 | 同上 |
| `0x7A1` 周期 | 4 拍 = 200 ms | `GPS_CAN_LAP_TICKS` | 节拍数 | 同上；两周期的公倍数 `GPS_CAN_SLOW_CYCLE_TICKS` = 20 须随之调整 |
| 航向过期 | 1500 | `GPS_HPR_EXPIRE_MS` | ms | 双天线航向超过此时间未更新则无效 |
| GSV 过期 | 12000 | `GPS_GSV_EXPIRE_MS` | ms | 信噪比过期后信号等级回落到卫星数估计或无信号 |
| 航向安装偏置 | 0.0 | `GPS_HEADING_INSTALL_OFFSET_DEG` | 度，顺时针 | 从天线 1→2 基线到车头方向的夹角；影响圈速线和冲刺线方向。天线安装方向变化时必须修改 |
| 信号等级阈值（平均信噪比） | 42 / 35 / 28 / 20 | `GPS_SNR_EXCELLENT/GOOD/FAIR/WEAK` | dB-Hz，≥ 判定 | 对应等级 4/3/2/1，低于 20 为 0 |
| 信号等级回退（卫星数） | ≥16 / 12 / 8 / 4 | `gps_signal_level_from_sats` | 颗 | 仅在 GSV 缺失或过期时用 |
| DMA 接收缓冲 | 512 | `GPS_DMA_BUF_SIZE` | B | 115200 baud 下约 44 ms 绕回一圈，GPS 任务须每 10 ms 内读取 |
| 里程累加最小车速 | 100 | `GPS_ODOMETER_MIN_SPEED_CENTI` | 0.01 km/h（= 1.00 km/h） | 低于此速度不累加 |
| 里程最大采样间隔 | 2000 | `GPS_ODOMETER_MAX_GAP_MS` | ms | 间隔更长的一步丢弃 |
| 里程单步上限 | 50 | `GPS_ODOMETER_MAX_STEP_M` | m | 另有「不超过按车速估算值 3 倍 + 3 m」的合理性检查 |
| 里程存盘间隔 | 100 | `GPS_ODOMETER_SAVE_INTERVAL_M` | m | 每增加该距离写一条 Flash 记录；改小会缩短 Flash 寿命，见 [里程存储](odometer_storage.md) |
| 接收机配置命令 | 见下 | `GPS_TaskFunc` | — | UM982 基线长度 `CONFIG HEADING LENGTH 111 10` 为 111 cm ±10 cm（两个参数单位均为 cm）；天线间距变化须修改 |

UM982 启动时依次发送（每条后等待并继续读取 DMA）：`UNLOG COM2`、`MODE ROVER AUTOMOTIVE`、`CONFIG HEADING FIXLENGTH`、`CONFIG HEADING LENGTH 111 10`、`GPGGA 1`（1 s）、`GPRMC 0.05`（20 Hz）、`GPHPR 0.2`（5 Hz）、`GPGSV 5`（5 s）、`SAVECONFIG`。

## 圈速与冲刺（`gps_lap.c`、`gps_lap.h`、`dashboard_ui.c`）

| 设置 | 当前值 | 符号 | 单位/范围 | 影响 |
| --- | --- | --- | --- | --- |
| 过线门半宽 | 2.5 / 3.0 / 4.0 | `GPS_LAP_GATE_HALF_RTK_FIXED_M` / `..._FLOAT_M` / `..._SINGLE_M` | m | 按 GGA 定位质量选：4（RTK 固定）、2 或 5（DGPS / RTK 浮点）、其余（单点等） |
| 过线布防距离 | 10 | `GPS_LAP_LINE_ARM_DISTANCE_M` | m | 离线超过此距离后才检测过线，避免起点误触发 |
| 接近提示距离 | 30 | `GPS_LAP_APPROACH_DISTANCE_M` | m | 诊断状态 APPROACHING |
| 过线诊断保持 | 2000 | `GPS_LAP_DIAG_HOLD_MS` | ms | 漏门、反向、已过线状态的显示保持 |
| 参考轨迹采样 | 最多 512 点；间距 ≥1 m 或间隔 ≤500 ms | `GPS_LAP_TRACK_SAMPLE_MAX`、`GPS_LAP_SAMPLE_MIN_DIST_M`、`GPS_LAP_SAMPLE_MAX_PERIOD_MS` | — | 实时圈差用；占 RAM |
| 圈时界面刷新 | 200 | `GPS_LAP_UI_UPDATE_PERIOD_MS` | ms | |
| 冲刺距离 | 75 | `GPS_SPRINT_DISTANCE_M` | m | S 模式直线计时长度；界面「LEFT」显示剩余整米 |
| 冲刺起步车速 | 0.5 | `GPS_SPRINT_START_SPEED_KMH` | km/h | 准备时车速须低于此值，越过此值视为起步 |
| 冲刺单步合理性 | 8 m + 3×估算 | `GPS_SPRINT_STEP_MARGIN_M`、`GPS_SPRINT_STEP_FACTOR` | — | 位置突跳超过此范围的采样被拒绝 |
| 无航向时起线最低车速 | 5.0 | `LAP_START_MIN_SPEED_KMH`（`dashboard_ui.c`） | km/h | 双天线航向无效时改用 RMC 航迹，需达到此车速 |

## CAN（`can.c`、`freertos.c`）

| 设置 | 当前值 | 符号 | 单位/范围 | 影响与同步 |
| --- | --- | --- | --- | --- |
| 接收超时 | BMS 2000；ECU 1000；PDM 1500；风扇 1500 | `CAN_TIMEOUT_BMS_MS/ECU_MS/PDM_MS/FAN_MS` | ms | 超时后对应数值显示 `--`。ECU 周期在中央库仍为暂定值，取宽松门限；周期确定后应收紧 |
| 超时检查间隔 | 100 | `CAN_TIMEOUT_POLL_MS` | ms | |
| 方向盘按键重复间隔 | 300 | `CAN_BUTTON_REPEAT_GAP_MS` | ms | 判定「新按下」；方向盘固件按代码每次按键只发一个 1 帧，故该值是两次按下的最小间隔，待抓包确认 |
| 接收队列 / 发送队列 | 64 / 32 | `USER_CAN_RX_QUEUE_CAPACITY`、`USER_CAN_TX_QUEUE_CAPACITY` | 帧 | 满时丢新帧 |
| 接收白名单 | 见 [CAN 接口](can_interface.md) | `CAN1_RX_MSG_ID_BANK*`、`CAN2_RX_MSG_ID_BANK0` | CAN ID | 新增接收 ID 必须同步修改，并更新 [CAN 接口](can_interface.md) |
| GPS 帧 ID | `0x067/0x068/0x7A0/0x7A1`（另有 `0x301`） | `CAN_ID_GPS_*` | CAN ID | 对外帧，改动先走中央库评审 |

## 显示与实时（`main.c`、`lv_conf.h`、`dashboard_ui.c`、`freertos.c`）

| 设置 | 当前值 | 符号 | 单位/范围 | 影响 |
| --- | --- | --- | --- | --- |
| LVGL 刷新周期 | 16 | `main.c` `lv_timer_set_period` | ms | 新车速最多等一个周期上屏；模拟器仍为 33 ms |
| LVGL 绘制缓冲 | 800×10 行 RGB565 = 16,000 | `g_lvgl_draw_buf` | B，主 SRAM | 行数增大减少刷新次数但占 RAM |
| LVGL 内存池 | 60 KiB，CCM `0x10000000` | `LV_MEM_SIZE`、`LV_MEM_ADR`（`stm32_port/Core/lv_conf.h`） | B | CCM 共 64 KiB，仅剩 4 KiB；耗尽触发 LVGL 断言停机。链接脚本禁止其他数据占用该区 |
| 夜间背光 | 45 | `BACKLIGHT_NIGHT_COMPARE` / `BACKLIGHT_PWM_PERIOD` = 450 / 1000 | 占空比，‰ | 模拟器用 55% 黑色遮罩模拟 |
| Dashboard 循环延时 | 5..20（启动期 ≤16） | `freertos.c` | ms | 取 `lv_timer_handler` 返回值并限幅 |
| 告警队列 | 6 条，每条 ≤32 字符 | `DASHBOARD_ALERT_MAX`、`DASHBOARD_ALERT_TEXT_MAX` | — | |
| 温度着色阈值 | 70 / 85 | `TEMP_WARN_C`、`TEMP_HOT_C`（`dashboard_view.c`） | ℃ | 琥珀 / 红；MOT、INV、TMAX 共用 |
| FreeRTOS 堆 | 16 KiB | `.ioc` `configTOTAL_HEAP_SIZE`，`FreeRTOSConfig.h` USER 区同值 | B | 只服务 CMSIS 对象和引导任务 |
| 任务栈与优先级 | 见 [固件资源与性能优化](firmware_optimization.md) | | | 栈水位由 `g_firmware_diag` 监测 |
