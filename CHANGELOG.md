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
- **方向盘上电首帧丢失**：方向盘只在变化时发滑级和模式，车手屏界面初始化前收到的帧被丢弃。若车手屏比方向盘晚就绪，界面滑级为 0，且在电位器被拨动前，计时键或消错键触发的 `0x703` 带滑级 0。需方向盘周期重发当前状态，或车手屏在收到首个 `0x700` 前不发滑级（协议暂无“未知”编码，须先在中央库评审）。由代码推出，未上板验证。
- **计时锁期间旋钮不同步**：锁定期间拨旋钮，车手屏不转发 `0x310`，方向盘解锁后不重发，旋钮位置与 ECU 实际模式不一致。
- **方向盘抓包与 ECU 确认**：`0x701/0x702` 松开单帧、`0x310` DLC 8 目前只来自方向盘固件代码；ECU 是否使用 `0x703` 的计时位和清错位、`0x784` 是否保留均待确认。
- **缺少用例**：计时锁生效时 `0x310` 不转发、界面启动完成前丢帧、`0x701/0x702` 触发后界面端动作。
- **跨仓库提交未推送**：`vehicle-interfaces` 分支 `can/retire-imu-0x050`、`can/wheel-lap-timer-semantics`（后者基于前者）需走 PR；`Steering_wheel`、`CANRS485_G473`、`CAN2RS485` 为本地提交，未推送。`Steering_wheel` 根目录的 `Vehicle_CanB.dbc` 是过期快照，建议删除。

## 2026-10-11 模拟器窗口 1:1 显示

- 原先 `hal.c` 按 480×272 基准取缩放 1.667，800×480 纹理被非整数拉伸；Retina 屏上还被系统 2 倍线性放大，界面发糊。现去掉缩放，窗口固定为 800×480 逻辑像素，与真机 1:1。
- 根目录 `lv_conf.h` 新增 `LV_SDL_ALLOW_HIGHDPI`（1）；`lvgl/src/drivers/sdl/lv_sdl_window.c` 据此给窗口加 `SDL_WINDOW_ALLOW_HIGHDPI`，高 DPI 屏上纹理整数倍放大。这是对 LVGL 的本地补丁，升级 LVGL 时需保留（已写入 `README.md`）。
- 验证：`./run_sim_mac.sh --build-only` 无新增警告；macOS Retina 下实际启动并截图，窗口 800×480 点、内容 1600×960 像素（恰为 2 倍）。**未在 Windows（MinGW）模拟器上验证**，系统缩放 125%/150% 时是否仍模糊未知。
- 未改：模拟器仍为 32 位色、1 MB 池，与真机 RGB565、60 KiB 不同，见「待办」。

## 2026-10-11 方向盘按键含义更正与方向盘文档

- 更正：`0x701`（DBC 信号名 `RecorderToggle`）是圈速计时键，不是录像。此前文档把它写成“录像”，来自对 `Recorder` 的误译。信号名沿用旧称，改名会影响 ECU 等按名称生成代码的接收方，须在中央库评审。`0x702` 在车手屏上清除**全部**告警（含排队中的），此前文档写成“最早一条”，只有触摸告警条才逐条消除。
- `can.c`：方向盘处理的局部变量 `s_recorder_button`、参数 `recorder_toggle` 改为 `s_lap_button`、`lap_toggle`，注释更正并写明方向盘固件的发送方式；逻辑未变。
- 新增文档：[CAN 网络分工](docs/can_network.md)、[方向盘与车手屏](docs/steering_wheel.md)、[方向盘相关代码](docs/steering_wheel_code.md)，README 索引同步；`can_interface.md`、`gps_lap.md`、`configuration.md`、`dashboard_ui.md` 更正“录像”说法并按方向盘固件更新发送方式。
- 关联仓库：`Steering_wheel` 更正按键含义并删除 `0x50` 接收与 `0x60..0x66` 转发（本地提交 `15528ba`、`08fdd31`）；`vehicle-interfaces` 分支 `can/wheel-lap-timer-semantics`（`611c8bd`）更正 DBC 注释和接口文档。
- 已验证：`./build_fw.sh` 无警告，Flash 243,136 B（61.8%）、主 SRAM 92,024 B（70.2%）、CCM 61,440 B（93.8%）；`python3 tools/run_firmware_tests.py` 通过；`Steering_wheel` 的 `cmake --build build/Debug` 无警告（FLASH 21.9%、RAM 9.2%）；`vehicle-interfaces` 的 `validate_interfaces.py` 通过。
- 未验证：方向盘发送方式未抓包；未上板、未实车；CubeIDE 完整构建。`docs/steering_wheel.md` 中“已知限制”由代码推出，均未上板复现。

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
