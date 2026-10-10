# 更新记录

## 未发布

### 待办

均已对照 2026-10-10 的代码核对，尚未处理。

- **CANB 接口登记**：`0x102` 心跳、IMU 转发帧 DLC 与布局、IMU 角度字节偏移、`0x050` 发送节点与中央库不一致，明细见 [`docs/can_interface.md`](docs/can_interface.md#与中央库尚未对齐的部分)。涉及外部消费的帧，先在 `vehicle-interfaces` 评审，再改本仓库。
- **GPS 接收机持久设置**：`gps.c` 每次上电都发 `SAVECONFIG`，每次开机写一次 UM982 非易失存储，需评估写入寿命，或只在配置变化时保存。
- **模拟器与真机配置不一致**：模拟器根目录 `lv_conf.h` 为 32 位色、LVGL 池 1 MB；真机为 RGB565、60 KiB（CCM）。界面已共用 `dashboard_view.c`，但内存预算仍无法在模拟器上验证。
- **背光点亮时机**：代码上背光在 `Dashboard_UI_Init` 末尾由 TIM14 PWM 以 100% 点亮，早于随后 `lv_refr_now` 的首帧，而 `main.c` 原注释称「首帧后开背光」。上板确认开机是否闪现未刷新的 GRAM 内容，必要时把 PWM 启动移到首帧之后。
- **未使用或用途不明的外设**：USART1、USART6 已初始化但应用层未使用；PC2（输入）、PC3（输出）`.ioc` 无标签、代码未引用。确认后可从 `.ioc` 移除或补充标签。
- **缺少自动化用例**：圈速过线、冲刺计时的几何判断没有回归用例。
- **体积较大的源文件**：`dashboard_ui.c`、`can.c`（CubeMX 生成文件的 USER 区内承载了解码、转发和 GPS 遥测）建议拆成独立模块。

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
