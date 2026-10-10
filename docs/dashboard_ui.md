# 仪表盘界面

目标为 SSD1963 驱动的 800×480 IPS 屏，由 STM32F407ZET6 经 FSMC 局部刷新（RGB565，800×10 行缓冲）。

![仪表盘](images/dashboard.png)

![告警](images/dashboard-alert.png)

## 代码结构

| 文件 | 职责 |
| --- | --- |
| `stm32_port/Core/Src/dashboard_view.c/.h` | 纯 LVGL 视图：布局、样式、绘制、触摸区域判定。不含 HAL/RTOS 调用 |
| `stm32_port/Core/Src/dashboard_ui.c` | 控制器：各任务/中断提交数据的缓冲与脏标记、圈速/冲刺控制、告警队列、背光 PWM |
| `stm32_port/Core/Src/fonts/dash_font_*.c` | 由 `tools/gen_dash_fonts.sh` 生成的字库，勿手改 |
| `src/freertos_main.c` | 模拟器：编译同一份视图，以动画演示数据驱动 |

`Dash_View_*` 只能在 LVGL 线程（Dashboard 任务）调用；`Dash_View_HitTest` 是纯函数，触摸任务可直接调用。轮序沿用 `dashboard_data_t`：0=FL、1=RL、2=FR、3=RR。

## 布局

| 区域 | 坐标 (px) | 内容 |
| --- | --- | --- |
| 顶栏 | y 0–52 | BITFSAE 标识、BEST、LAST、圈差（数值+条）、LAP/RUN、LEFT/DIST M、ODO KM、GPS 信号 |
| 左栏 | x 0–200 | 驾驶模式色块（S 红/Q 黄/C 蓝/E 绿）、SLIP 等级与 7 段条、四轮胎面温度图与最高温、夜间按钮 |
| 中栏 | x 200–600 | 当前圈/冲刺用时与计时状态点、告警/READY 横幅、180 px 车速、制动/油门竖条 |
| 右栏 | x 600–800 | SOC 与电量条、HV 电压/电流、功率/峰值、TMAX、LV 电压/电流/功率、三路风扇 |
| 底栏 | y 376–480 | FL/FR/RL/RR 电机：使能（闪电黄=使能）、故障码（红底 `E xxxx`）、TQ/RPM、PWR/PEAK、MOT/INV 温度 |

触摸区域：顶栏开始/停止计时（S 模式为 75 m 冲刺）；告警横幅消除最早一条；夜间按钮把背光降到 45%。告警文字转大写，`+N` 表示还有 N 条排队。

颜色规则：MOT、INV、TMAX ≥70 °C 琥珀、≥85 °C 红（`TEMP_WARN_C`/`TEMP_HOT_C`）；SOC <50 % 琥珀、<20 % 红；圈差变快为绿色向右、变慢为红色向左，±5 s 满格。TQ 为 DBC 原始值（0.1 %Mn），PWR/PEAK 沿用原公式 `torque×rpm/12000`，均不带单位。

## 性能约束

- 纯黑底、实色填充；不用渐变、阴影、透明度。圆角只用于静态元素。
- 样式共享（`lv_style_t`），避免每个对象的局部样式分配；数值标签固定宽度，文字变化只重绘该框。
- 踏板、电量、圈差、滑移、信号、胎温均为单对象 + 绘制回调，不叠子对象。
- setter 缓存输入，文字和颜色不变时不触发重绘；控制器另有 50/200 ms 分级节流，车速 100 ms。
- 字库：Barlow Semi Condensed（OFL，`tools/fonts/`），4 bpp 不压缩，按需裁剪字符集；图标取 LVGL 自带 FontAwesome。固件已关闭 Montserrat，默认字体为 `dash_font_s`。

改版后（对比 `5860890`）：Flash 249,544 B（+22 KB，主要为速度字库），主 SRAM 不变；主机 64 位回归测得 LVGL 池峰值 55.2 KB（原 61.6 KB）。目标机余量以 `g_firmware_diag.lvgl_*` 为准。

## 标识

当前标识为文字（BIT 白 + FSAE 红），无额外开销。SVG 需开启 ThorVG（C++、数十 KB Flash 与较大运行内存），不适合本平台。若有标识图，用 `lvgl/scripts/LVGLImage.py` 离线转为 A8 或 RGB565 C 数组，再开 `LV_USE_IMAGE`：约 120×36 的 A8 图约 4 KB Flash，只在首帧绘制一次，不占 RAM 和运行时 CPU。

## 验证

```bash
python3 tools/run_firmware_tests.py --png /tmp/dash   # 编译固件 UI 与 LVGL，跑回归并输出六帧截图
./build_fw.sh                                         # ARM 构建与内存报告
./run_sim_mac.sh                                      # 模拟器
./tools/gen_dash_fonts.sh                             # 改字号/字符集后重新生成字库（需 Node.js）
```
