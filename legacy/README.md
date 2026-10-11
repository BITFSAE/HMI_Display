# legacy/

不参与当前车手屏固件和模拟器构建的遗留与参考资料。保留仅为追溯，新开发不要依赖这里的内容。

| 路径 | 内容 | 说明 |
| --- | --- | --- |
| `stm32_display/` | STM32F103C8T6 + ATK-MW1278D 无线串口模块的 2025 版 CAN 转无线发送板（CubeIDE 工程 `FSAE_HMI_interface_2025`），收 CAN 后经无线模块发出车速、SOC、电压、温度等 | 与本屏固件无代码依赖；仅 2026-04-02 一次提交，之后未维护。源码注释含已损坏的乱码（`锟斤拷`），无法还原。从原位置移到此处，CubeIDE 里如已导入需重新导入 |
| `reference/` | 开发板厂商的 STM32F407 标准库 TFT-LCD 与触摸示例（Keil 工程 `TOUCH`，注释为 GBK 编码） | 只用于对照液晶底层写法，不参与任何构建。原 `reference_ips/` 与它逐字节相同，已合并删除 |
| `mlx90640_can_summary.dbc` | MLX90640 胎温帧 `0x071..0x074` 的旧版 DBC | 信号布局与 `vehicle-interfaces` 的 `Vehicle_CanB.dbc` 中 `BO_113..116` 相同，只是名称和接收节点不同。已被取代，接口以 `vehicle-interfaces` 为准 |
