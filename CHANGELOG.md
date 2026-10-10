# 更新记录

## 未发布

### 待办

均已对照 2026-10-10 的代码核对，尚未处理。

- **GPS 接收机持久设置**：`gps.c` 每次上电都发 `SAVECONFIG`，每次开机写一次 UM982 非易失存储，需评估写入寿命，或只在配置变化时保存。
- **模拟器与真机配置不一致**：模拟器根目录 `lv_conf.h` 为 32 位色、LVGL 池 1 MB；真机为 RGB565、60 KiB（CCM）。界面已共用 `dashboard_view.c`，但内存预算仍无法在模拟器上验证。
- **背光点亮时机**：代码上背光在 `Dashboard_UI_Init` 末尾由 TIM14 PWM 以 100% 点亮，早于随后 `lv_refr_now` 的首帧，而 `main.c` 原注释称「首帧后开背光」。上板确认开机是否闪现未刷新的 GRAM 内容，必要时把 PWM 启动移到首帧之后。
- **未使用或用途不明的外设**：USART1、USART6 已初始化但应用层未使用；PC2（输入）、PC3（输出）`.ioc` 无标签、代码未引用。确认后可从 `.ioc` 移除或补充标签。
- **缺少自动化用例**：圈速过线、冲刺计时的几何判断没有回归用例。
- **体积较大的源文件**：`dashboard_ui.c`、`can.c`（CubeMX 生成文件的 USER 区内承载了解码、转发和 GPS 遥测）建议拆成独立模块。

## 2026-10-10 删除 0x102 心跳

`0x102` 心跳没有接收方，中央库也未登记，按项目负责人决定整体删除。

- `can.c`：删除 `CAN1_ID`、心跳计数和 `CAN1_SendHeartbeat`，以及同样发 `0x102` 且无调用方的 `User_CAN_Send` 和未被引用的 `CAN_TxData`、`CAN1_TxMail`；`freertos.c` 去掉每 500 ms 的发送，`can.h` 同步去掉声明。
- `docs/can_interface.md`、`docs/configuration.md` 删除对应条目；「与中央库尚未对齐」只剩胎温和方向盘帧。
- 绿色 LED（PD）250 ms 翻转不变，它是独立的运行指示，与 CAN 无关。
- 已验证：`./build_fw.sh` 无警告，Flash 243,136 B（61.8%）、主 SRAM 92,024 B（70.2%）、CCM 61,440 B（93.8%）；`python3 tools/run_firmware_tests.py` 通过。未验证：CubeIDE 完整构建；上板后在 CANB 上确认 `0x102` 不再出现。

## 2026-10-10 停用 IMU 接收与转发

`0x050` 的 IMU 设备已不存在，车手屏不再接收 `0x050`，也不再向 CANB 发 `0x060..0x066`。

- `can.c`：删除 CAN1 过滤器 bank3、FIFO1 回调与 IMU 解析转发；队列项去掉 `fifo` 字段。`main.c` 不再开启 FIFO1 接收通知。`dashboard_ui.h` 删除未被界面使用的 `imu_*` 字段。
- `docs/can_interface.md`：删除 IMU 的接收、发送和「尚未对齐」条目，过滤器表去掉 FIFO 列。
- 回归测试：原 `0x050` 转发断言改为「`0x050` 到达不产生发送」。
- 已验证：`./build_fw.sh` 通过且无警告，Flash 243,236 B（61.9%）、主 SRAM 92,032 B（70.2%）、CCM 61,440 B（93.8%）；`python3 tools/run_firmware_tests.py` 通过。`./run_sim_mac.sh --build-only` 无需重编（模拟器不含 `can.c`）。
- 未验证：CubeIDE 完整构建；上板后 CAN1 的 FIFO1 中断确实不再触发；`CAN1_RX1_IRQn` 仍由 `.ioc` 使能，未改。
- 中央库 `vehicle-interfaces` 同步删除 `0x050`、`0x060..0x066`（分支 `can/retire-imu-0x050`），网关 `CANRS485_G473`、`CAN2RS485` 同步删除对应处理，见各仓库的更新记录。

## 2026-10-10 仓库整理

- `.ioc`：`USART3.BaudRate` 从 9600 改为 115200。此前只手改了生成的 `usart.c`，重新生成会把 GPS 串口改回 9600，与 UM982 的 115200 不符。
- 构建产物与本机 IDE 状态改为不跟踪（本地文件保留，旧版本仍在 git 历史中）：`stm32_port/Debug/`、`stm32_display/Debug/`、`MDK-ARM/DATA_COLLECTION_INTEGRAL/` 与 `.uvguix.lenovo`、根目录 `.settings/`。拉取此提交的人，原先入库的这些文件会从工作区消失，需重新构建。
- 移入 `legacy/`：旧版 F103 无线转发板 `stm32_display/`、厂商示例 `reference/`（`reference_ips/` 与其逐字节相同，已删除）、过期的 `mlx90640_can_summary.dbc`。
- `gen_nmea.py` 移入 `tools/`；UM982 命令手册 PDF 移入 `docs/reference/`。
- 删除无引用的 `manifest.json`、`stimulator.code-workspace`，以及旧版重复的 `DATA_COLLECTION_INTEGRAL Debug (1).launch`。
- README 新增仓库结构、文档索引、第三方源码版本与用途、构建产物策略，并修正与代码不符的陈述（FreeRTOS 堆 512 MB、`USE_FREERTOS` 默认值、模拟器界面是否与固件共用）；原「CAN 数据来源」整节移入 `docs/can_interface.md`，不再两处维护。
- 新增文档，均对照当前代码和 `.ioc` 编写：`docs/hardware.md`（时钟、引脚、CAN 位时序、FSMC 读写时序及其与 `.ioc` 的差异、背光、触摸、指示灯编码）、`docs/can_interface.md`、`docs/configuration.md`、`docs/gps_lap.md`。
- `AGENTS.md` 改为只含编辑与验证规则：限定「只改 USER CODE 区」适用于 CubeMX 生成文件，补构建与测试命令、文档同步规则，屏幕与 MCU 事实移入 `docs/hardware.md`，保留 `DO NOT send optional commentary`；新增内容相同的 `CLAUDE.md`。
- 修正 `main.c` 中两处与代码不符的注释（FSMC 命令/数据选择线是 A16 而非「A17」；背光实际点亮时机）。
- 已验证：`.ioc` 与 `usart.c` 的 USART1/2/3/6 波特率一致；移动前确认被移动或删除的文件没有被构建脚本、CMake 和文档引用；`./build_fw.sh` 通过，Flash 243,628 B（程序区 384 KiB 的 62.0%）、主 SRAM 92,312 B（70.4%）、CCM 61,440 B（93.8%）。新增文档中的数值逐项对照 `.ioc`、`can.c`、`gps.c`、`gps_lap.c`、`fsmc.c` 等源码。
- 未验证：CubeMX 重新生成后的 `usart.c`、`fsmc.c` 差异；引脚、电平、极性未对照原理图；开机背光时序、方向盘实际发送方式、实车跑圈与 75 m 实测未上板；Keil 工程已确认缺少 LVGL 和应用模块，不能构建当前固件。
