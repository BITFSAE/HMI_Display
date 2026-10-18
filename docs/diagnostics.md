# 串口诊断 CLI

USART6 上的命令行，用来采样 GNSS 的实际运行情况并导出可整段复制的文本报告。日常保持静默，只在收到命令时输出。实现在 `diag_cli.c`，速度有效性在 `gps_speed.c`，统计在 `gps.c`，延迟追踪在 `speed_trace.c`。

## 连接

| 项 | 值 |
| --- | --- |
| 引脚 | PC6（TX）、PC7（RX，内部上拉），USART6，115200 8N1，无流控 |
| 电平 | MCU 引脚电平（3.3 V TTL），需 USB 转 TTL 线，不是 RS-232 |
| 终端 | 回车发 CR 或 CRLF 均可；设备回显，支持退格，`Ctrl-C` 等同 `stop` |

**PCB 是否引出 PC6/PC7、接口电平和接线未经确认**（`.ioc` 只说明 USART6 已初始化）。未引出时改用 USART1（PA9/PA10，已初始化、9600，需改 `.ioc` 波特率和 `diag_cli.c` 的串口句柄）。USART3 专用于 GNSS，USART2 是圈速差输入。

## 命令

| 命令 | 作用 |
| --- | --- |
| `help`、`version` | 命令列表；固件 Git 版本、构建时间、报告格式版本 |
| `report [N]` | 采样 N 秒（1..60，默认 10），结束后输出完整报告 |
| `gps status` | 当前速度、定位、航向有效性和数据龄期 |
| `gps config` | 向接收机发 `VERSION`、`MODE`、`CONFIG`、`UNILOGLIST`，输出启动配置的回执结果和接收机原始回复 |
| `gps capture [N]` | 记录 N 秒（1..20，默认 15）每条 RMC，输出 CSV |
| `gps rate 10\|20` | 把 RMC 输出率临时改为 10 Hz 或 20 Hz，**不保存**，重启或 `gps rate 20` 恢复，用于对照实验 |
| `gps smooth on\|off` | 开关多普勒速度平滑（`CONFIG SMOOTH PSRVEL enable\|disable`，手册 §4.12，接收机默认关闭），**不保存**；用于判断速度滞后是否来自平滑，`gps smooth off` 或接收机重新上电恢复 |
| `gps save` | 发 `SAVECONFIG`，把接收机当前设置写入其非易失存储 |
| `sys status` | 任务栈余量、堆与 LVGL 内存、CAN 与存储计数、CLI 自身计数 |
| `stop` | 中止正在采样或输出的命令 |

一次只执行一个命令，忙时回复 `busy, type stop`；参数错误回复 `error: ...`。命令发给接收机的请求由 GPS 任务执行（避免多个任务同时写 USART3），期间照常读 DMA 和发 CAN；等待最多 8 s。

用法：运行 `report 10`，复制从 `HMI-DIAG BEGIN` 到 `HMI-DIAG END` 的全部输出；需要加减速曲线时再运行 `gps capture 15` 并附上。

## 报告格式（`fmt=1`）

每行以标签开头，其后是空格分隔的 `键=值`；`a/b` 表示「采样窗口内增量/上电以来累计」。窗口时间用设备 `HAL_GetTick` 的毫秒数。

| 标签 | 字段 | 含义 |
| --- | --- | --- |
| `build` | `fw`、`built`、`uptime_ms` | Git 版本（构建时的 `git describe --dirty`；CubeIDE 构建显示 `unknown`）、构建时间、运行时间 |
| `window` | `start_ms`、`end_ms`、`length_ms` | 采样窗口 |
| `gps.rmc` | `rmc_total`、`accepted`、`rate_hz`、`interval_ms_min/avg/max`、`utc_step_ms_min/max` | 窗口内通过校验的 RMC 数、被接受数、实测输出率、相邻被接受 RMC 的 MCU 间隔、相邻 UTC 历元间隔（20 Hz 应为 50） |
| `gps.rmc.rejected` | `no_fix`、`bad_field`、`dup_epoch`、`back_epoch`、`checksum_fail` | 状态 V、速度字段缺失或非法、重复历元、倒退历元、校验失败 |
| `gps.nmea` | `bytes`、`sentences`、`gga`、`hpr`、`gsv`、`reply`、`other`、`line_overflow`、`line_truncated` | 各类语句数；`other` 非零说明白名单之外的语句仍在输出（`UNLOG` 或白名单没有生效）；`reply` 是接收机对命令的回执 |
| `gps.speed` | `valid_now`、`reason`、`age_ms`、`age_max_ms`、`limit_ms`、`period_ema_ms`、`valid_pct`、`invalid_events` | 速度当前是否有效及原因（`ok`、`no_data`、`stale`、`no_fix`）、最后一条可信 RMC 至今的龄期、窗口内龄期最大值、当前新鲜度门限、周期估计、窗口内速度有效时间占比（%）、有效变无效的次数 |
| `gps.speed.value` | `raw_kmh`、`shown_kmh`、`min_kmh`、`max_kmh`、`last_utc_ms` | 最近一条被接受的原始速度、提交给界面的整数值（`--` 表示界面显示 `--`）、窗口内极值、最近历元（UTC 当日毫秒） |
| `gps.link` | `poll_gap_max_ms`、`late_poll`、`ring_pending_max` | GPS 任务轮询间隔最大值、间隔 ≥ 44 ms 的次数（**可能**丢字节的风险计数，不是精确丢帧数）、一次轮询时 DMA 环待读字节数最大值（512 为满） |
| `gps.uart` | `pe`、`ne`、`fe`、`ore`、`dma`、`rx_restarts`、`restart_fail` | USART3 错误计数与 DMA 接收重启次数 |
| `gps.cfg` | `ack_seen`、`ack_unsupported`、`running`、`commands` | 启动配置是否收到过回执、是否因无回执而放弃确认 |
| `gps.cfg.cmd` | `i`、`cmd`、`state`、`attempts`、`reply` | 每条启动命令：`ok`、`rejected`、`no_ack`（重试后仍无回执）、`unconfirmed`（不要求或无法确认）、发送次数、回执文字 |
| `gps.cfg.request` | 同上 | 最近一次 `gps rate`/`gps save` 的结果 |
| `gps.event` | `t_ms`、`to`、`reason`、`age_ms` | 最近的速度有效性切换，最新在前，最多 8 条 |
| `latency.submit_to_apply` | `n`、`avg_ms`、`max_ms`、`last_ms` | 整数 km/h 变化的那些提交：GPS 任务提交 → Dashboard 任务交给 LVGL |
| `latency.apply_to_frame_done` | 同上，加 `superseded` | 交给 LVGL → 该帧最后一块写完 SSD1963；`superseded` 是提交后未应用就被新值覆盖的次数（界面慢于 GNSS） |
| `latency.display` | `loop_max_ms`、`flush_max_ms` | Dashboard 任务一圈最长耗时、单块刷屏最长耗时（上电以来） |
| `sys`、`sys.task`、`sys.io`、`sys.cli` | 内存、任务栈、CAN 丢帧/非法帧/队列峰值/Bus-Off、里程存储失败与擦除次数、CLI 收发丢弃数 | 同 `sys status` |

## 判读

| 现象 | 说明与处理 |
| --- | --- |
| `rate_hz` 明显低于 20，或 `period_ema_ms` 不是 50 | 接收机输出率没生效。看 `gps.cfg.cmd` 里 `GPRMC 0.05` 的状态；`rejected`/`no_ack` 则修配置，`gps config` 的原始回复可看接收机实际设置 |
| `other` 非零 | 白名单之外的语句仍在输出，`UNLOG` 没有清掉出厂流 |
| `age_max_ms` 远大于 `limit_ms`、`invalid_events` > 0 | 窗口内出现过断流；同时看 `late_poll`、`ring_pending_max`、`uart` 错误、`checksum_fail`，区分接收机没发、串口出错还是读取太晚 |
| `dup_epoch`/`back_epoch` > 0 | 接收机重复或乱序输出，这些语句不续期 |
| `interval_ms_max` 大而 `utc_step_ms_max` 正常 | 数据按时产生但到达或解析被延后：调度或读取问题 |
| `utc_step_ms_max` 大 | 接收机本身输出间隔变大 |
| `latency.submit_to_apply` 大 | Dashboard 任务调度慢 |
| `latency.apply_to_frame_done` 大 | LVGL 渲染或 FSMC 刷屏慢，对照 `loop_max_ms`、`flush_max_ms` |
| 以上都正常，加减速时仍觉得滞后 | 接收机输出本身迟钝：用 `gps rate 10`/`gps rate 20`、`gps smooth on`/`off` 做对照，并对照 `gps capture` 曲线；需要绝对 GNSS 延迟时要时间对齐或外部速度参考 |

**延迟测的是什么**：UTC 历元用来检查 GNSS 历元是否按期推进，不能给出绝对延迟；「刷屏完成」是最后一块像素写入 SSD1963 的时刻，不等于屏幕上人眼可见，面板响应和 SSD1963 扫描延迟在 MCU 侧测不到。

## `gps capture` CSV

`idx,tick_ms,utc_ms,raw_kmh,shown_kmh,verdict,valid`：每条通过校验的 RMC 一行。`verdict` 为 `accepted`、`no_fix`、`bad_field`、`dup_epoch`、`back_epoch`；速度由有效变无效时额外记一行 `invalid:<原因>`。`raw_kmh` 缺失记 `-`，`shown_kmh` 为 `--` 表示界面显示 `--`。最多 420 条，写满后停止采集。

## 对运行的影响

- CLI 任务（优先级 `osPriorityLow`，栈 3 KiB，纳入栈水位监测）只在后台运行；USART6 中断只收发字节。输出由 CLI 任务按发送环空间逐行生成，发送环满时只让 CLI 任务等待，命令输出不丢；回显和提示符在环满时丢弃并计数（`sys.cli tx_drop_bytes`）。
- 采样只读统计，GPS 任务只增加计数和固定内存的记录（采集缓冲 6.7 KB、原始回复 1.5 KB），不做任何打印。
- RX 引脚悬空或干扰会产生帧/噪声错误：连续 64 次错误后停止接收，1 s 后重新打开，错误中断不会长期占用 CPU。
- 资源：Flash +约 14.9 KB，主 SRAM +约 16.2 KB（CCM 不变），数字见 [CHANGELOG](../CHANGELOG.md)。
