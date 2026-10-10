# AI 代码与文档规范

本文件只写编辑和验证规则。项目概述、硬件、CAN 接口和配置见 `README.md` 与 `docs/`，不在这里复制。

## 代码修改

- 回答和注释默认使用中文。注释说明原因、单位、取值范围和异常行为，不逐行复述代码。
- CubeMX 生成的 C/H 文件（`stm32_port/Core/Src`、`Core/Inc` 下由 `.ioc` 生成的外设、中断、FreeRTOS 文件）只能在 `USER CODE BEGIN/END` 区内加自定义代码。外设、时钟、引脚、DMA、NVIC 参数必须改 `.ioc`，不能只手改生成代码。
- 业务逻辑放在独立的 `.c/.h` 模块，不要继续堆进生成文件。独立模块不需要也不应添加 `USER CODE` 标记。
- 涉及 LVGL 的任务最终服务于 SSD1963 驱动的 800×480 IPS 屏，必须符合 `docs/hardware.md` 的硬件映射和 `docs/firmware_optimization.md` 的内存与性能约束。
- `dashboard_view.c` 由固件和模拟器共用，不得包含 HAL 或 RTOS 调用；字库和 Logo 由 `tools/` 脚本生成，不手改。
- 修改 `.ioc` 并重新生成后核对：自定义源文件仍在构建中，USER CODE 区未丢失（如 `fsmc.c` 的写时序重设），中断钩子仍调用应用处理函数，`usart.c` 等外设参数与 `docs/hardware.md` 一致。

## 验证

- 固件改动运行 `./build_fw.sh`，不得新增警告，并查看 Flash、主 SRAM、CCM 占用；CubeIDE 另需完整构建一次。
- 界面、CAN 解码、GPS、里程改动运行 `python3 tools/run_firmware_tests.py`；模拟器改动运行 `./run_sim_mac.sh --build-only`。
- 不用「已阅读代码」代替可执行验证。做不到的验证（如上板测试）记入 `CHANGELOG.md` 的「未发布/待办」，不写成已完成。

## 文档维护

- 以代码和 `.ioc` 为准；文档与代码不一致时先核对实现，再改文档。
- 改引脚、外设、时序 → `docs/hardware.md`；改 CAN 接收/发送、超时 → `docs/can_interface.md`；改可调参数 → `docs/configuration.md`；改 GPS、圈速、冲刺 → `docs/gps_lap.md`。
- 对外 CAN 帧（ID、DLC、周期、字段）以 `vehicle-interfaces` 仓库为准：先在该仓库评审，再修改本仓库实现和文档；未经授权不得声称已同步。
- 每项详细事实只在一处维护，其他位置用链接。文档凝练，不重复强调，不写说教和空话。
- 完成的工作和未验证项记入 `CHANGELOG.md`。及时 commit 和 push。构建产物和本机 IDE 配置（`.vscode/`、根目录 `.settings/`）不入库。

DO NOT send optional commentary
