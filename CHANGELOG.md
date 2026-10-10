# 更新记录

## 未发布

### 待办

均已对照 2026-10-10 的代码核对，尚未处理。

- **CANB 接口登记**（需先在 `vehicle-interfaces` 评审，再改本仓库）
  - HMI 每 500 ms 在 CAN1 发 `0x102` 心跳（`A5 5A 计数 F4 07 40 00 01`，`can.c` `CAN1_SendHeartbeat`），未在任何 DBC 或接口文档中登记。
  - IMU 转发帧 `0x060..0x066` 原样发送 8 字节（含 `0x55` 头和子类型字节），DBC 定义为 DLC 2 或 6 且信号从 bit0 起，外部按 DBC 解码会错位。
  - IMU 角度（`0x063..0x065`）解析把子命令字节 `data[2]` 当作数值低字节，代码里已有 `TODO: verify byte offset`；UI 目前不使用 IMU 数值。
  - DBC 中 `0x050` 的发送节点写为 Display，固件实际是接收方。
- **GPS 接收机持久设置**：`gps.c` 每次上电都发 `SAVECONFIG`，每次开机写一次 UM982 非易失存储，需评估写入寿命，或只在配置变化时保存。
- **串口命令无文档**：USART2（9600）接收 `+1.23` 形式的行作为圈速差输入（`usart.c`），格式和来源未记录。
- **模拟器与真机配置不一致**：模拟器根目录 `lv_conf.h` 为 32 位色、LVGL 池 1 MB；真机为 RGB565、60 KiB（CCM）。界面已共用 `dashboard_view.c`，但内存预算仍无法在模拟器上验证。
- **缺失文档**：硬件映射（引脚、FSMC 时序、背光、LED 面包屑）、CANB 收发全表（含超时和无效值）、配置项表。`AGENTS.md` 仍含屏幕和 MCU 型号等会变的项目事实，缺 `CLAUDE.md`。
- **体积较大的源文件**：`dashboard_ui.c`、`can.c`（CubeMX 生成文件的 USER 区内承载了解码、转发和 GPS 遥测）建议拆成独立模块。

## 2026-10-10 仓库整理

- `.ioc`：`USART3.BaudRate` 从 9600 改为 115200。此前只手改了生成的 `usart.c`，重新生成会把 GPS 串口改回 9600，与 UM982 的 115200 不符。
- 构建产物与本机 IDE 状态改为不跟踪（本地文件保留，旧版本仍在 git 历史中）：`stm32_port/Debug/`、`stm32_display/Debug/`、`MDK-ARM/DATA_COLLECTION_INTEGRAL/` 与 `.uvguix.lenovo`、根目录 `.settings/`。拉取此提交的人，原先入库的这些文件会从工作区消失，需重新构建。
- 移入 `legacy/`：旧版 F103 无线转发板 `stm32_display/`、厂商示例 `reference/`（`reference_ips/` 与其逐字节相同，已删除）、过期的 `mlx90640_can_summary.dbc`。
- `gen_nmea.py` 移入 `tools/`；UM982 命令手册 PDF 移入 `docs/reference/`。
- 删除无引用的 `manifest.json`、`stimulator.code-workspace`，以及旧版重复的 `DATA_COLLECTION_INTEGRAL Debug (1).launch`。
- README 新增仓库结构、第三方源码版本与用途、构建产物策略，并修正与代码不符的陈述（FreeRTOS 堆 512 MB、`USE_FREERTOS` 默认值、模拟器界面是否与固件共用）。
- 已验证：`.ioc` 与 `usart.c` 的 USART1/2/3/6 波特率一致；移动前确认被移动或删除的文件没有被构建脚本、CMake 和文档引用。未验证：CubeMX 重新生成后的 `usart.c` 差异；Keil 工程（已确认缺少 LVGL 和应用模块，不能构建当前固件）。
