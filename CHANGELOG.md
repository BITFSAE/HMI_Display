# 更新记录

## 未发布

### 2026-10-21 开屏动画

- 上电先播放约 2.6 s 开屏（鲨鱼冲入、七个字母依次冲入、方格旗、高光，最后斜边扫屏露出仪表）；点击左上角 Logo 区重放，行驶中也允许，重放多一段 0.3 s 入场扫屏。开屏中点击任意位置只跳到退场扫屏，不触发其他触摸区。素材为 `tools/assets/BITFSAE.svg` 和新增的 `silver-shark-sky.svg`，设计与实现细节见 [仪表盘界面](docs/dashboard_ui.md)。
- 新增 `splash_view.c/.h`（纯 LVGL，固件与模拟器共用）和 `dash_splash_data.c/.h`，位图由新增的 `tools/gen_dash_splash.sh` 生成。整段动画只有一个 LVGL 对象 + 一个绘制回调，元素位置是时间的纯函数（Q10 定点），按 33 ms 步进、只失效变化区域；全黑阶段用不透明底色让 LVGL 跳过其下的仪表。位图仍是 A8（Flash 直读，不占 RAM），再按有效列范围切成条带：9 个精灵 63 个条带共 28,279 B，整块包围盒需要 47,646 B。
- 顶栏触摸区调整：x < 164（Logo 到分隔线）为新增的 `DASH_ZONE_LOGO`，只重放开屏；x ≥ 164 仍是开始/停止计时。`dashboard_ui.c` 在 `Dashboard_UI_Process()` 里先于"无待处理项"检查步进开屏；触摸只置位标志。模拟器同步：点 Logo 重放，开屏中点击跳过。
- 浏览器原型 `tools/splash_preview/splash.html` 保留，用于调时间表、看每步重绘面积。
- 验证：`./build_fw.sh clean` 无警告，Flash 293,940 B（+32,756，74.8%，剩余 99,276 B）、主 SRAM 109,624 B（+24）、CCM 61,440 B（不变）。`python3 tools/run_firmware_tests.py` 通过，新增用例覆盖：上电与重放每一步的增量帧都与整屏重绘逐像素一致、开屏结束后的画面与开屏前的仪表逐像素相同、全黑阶段不绘制仪表控件、覆盖层只占 LVGL 池 280 B（主机）、Logo 区/计时区命中范围、开屏中点击只跳过（不触发模式色块）、入场与中途跳过。`--compare-ref HEAD` 的 7 帧仪表截图与改动前逐像素一致。每步写屏面积（30 fps）：上电峰值 15.9%、均值 5.9%，重放峰值 17.5%、均值 6.8%。`./run_sim_mac.sh --build-only` 通过（仅有原有的重复库链接警告），模拟器启动 7 s 无断言。
- **未验证**：未上板。目标机上的渲染与 FSMC 写屏耗时（可看 `g_firmware_diag.flush_max_cycles`）、33 ms 步进是否流畅、开屏期间 CAN/GPS 任务是否受影响均未实测；LVGL 池在目标机上的实际余量未测（主机池峰值 59,192 → 59,240 B）。CubeIDE 完整构建未运行（新增的 `.c` 在 `Core/Src` 下，应由工程自动收录）。行驶中重放会盖住仪表约 3 s，按需求允许，未加车速限制。

### 2026-10-21 ST-Link 烧写脚本与 VS Code 任务

- 新增 `tools/flash_stlink.sh`：经 SWD、`mode=UR` 用 STM32CubeProgrammer 命令行烧写 `stm32_port/build_vscode/DATA_COLLECTION_INTEGRAL.hex`（或命令行指定的 `.hex/.elf`），烧后回读校验并复位。不做整片擦除，扇区 7 的里程记录保留。工具按 `STM32_PROGRAMMER_CLI`、PATH、STM32 VS Code 扩展 bundles、CubeProgrammer/CubeIDE 安装目录的顺序查找。参考 `BMS_MASTER_F405/Tools/flash_stlink.sh`。
- 本机 `.vscode/tasks.json`（不入库）新增 `Flash Firmware (ST-Link)` 和 `Build & Flash Firmware (ST-Link)`，用法见 [README](README.md)。
- 验证：`bash -n` 通过；`tools/flash_stlink.sh` 找到 `/Applications/.../STM32_Programmer_CLI` 并运行，无探头时报 `No debug probe detected`；文件不存在时退出码 1。
- **未验证**：未接 ST-Link，没有实际烧写、回读校验和复位；正常下载后里程是否保留仍待上板确认（见 [里程存储](docs/odometer_storage.md)）。VS Code 任务未在 UI 中点击运行。

### 2026-10-19 制动双开度条与 BRT 踏板编码器

- CAN1 新增接收 BRT25-C0M4096-RT1 的自动上报 `0x001`（标准帧、DLC 7），过滤器 bank4 增加该 ID。位置帧仅在 DLC=7、byte0=7、byte1/byte2=`0x01` 且小端计数小于 4096 时接受；DLC=4 应答和其他 FUNC 忽略。车手屏不向编码器发送命令。
- 新模块 `brake_pedal.c/.h`（无 HAL/RTOS 依赖）把 0..4095 计数换成 −180°..+180°，在线寻找静止位：最近 1.5 s 峰峰值不超过 2 count，且 `0x305` 油压新鲜并在 2135 ±10 count 内；零点取该窗口的中位数。更低的静止位满足 1.5 s 后立即采用，更高的必须保持 5 s，避免脚搭在踏板上不动时误校准。未找到静止位或编码器 150 ms 无新帧时不显示 0%，而显示 `--`。
- 能量回收开度按相对角 0.3° 死区、7° 满量程线性换算并限幅；机械制动继续沿用 `0x305` 油压的 2.1 kPa→0%、4.9 kPa→90% 换算。两者独立显示、不相加。CAN 超时检查周期从 100 ms 收紧到 50 ms，使编码器断流最迟约 200 ms 反映到界面。
- 中栏从左到右改为 REG（蓝）、MECH（红）、THROTTLE（绿）三条竖条；两条制动条收窄并放在车速数字左侧，避免两三位车速压住 REG 条。SOP 的 DISCH/CHG 两行整体居中到车速下方，标题和数值各列居中；原 BMS 的 REGEN 限值行改标 CHG，避免与踏板回收条混淆。驾驶模式统一为 AC Acceleration、AX Autocross、8 Skidpad、EN Endurance，大字下显示全称，CAN 的 0..3 编号不变。`dashboard_view.c` 仍不含 HAL/RTOS，模拟器同步使用三参数接口。文档与三张界面截图已更新；踏板编码器帧与中央库的未登记状态、自动上报帧头的不确定性见 [CAN 接口](docs/can_interface.md)。
- 验证：`python3 tools/run_firmware_tests.py --png /tmp/hmi-mode-spacing` 通过，覆盖合法/非法帧、1.5 s 静止窗口、低/高静止位的不对称更新、油压静止带、0.3°/7° 换算、编码器与油压超时、三开度条渲染和 AC/AX/8/EN 四种模式全称；另以 299 km/h 渲染核对三格车速与左右竖条的间距。`./build_fw.sh` 通过且无警告，Flash 261,188 B（+1,796，66.4%）、主 SRAM 109,600 B（+1,248，83.6%）、CCM 61,440 B（不变）。`./run_sim_mac.sh --build-only` 通过，仅有原有的重复库链接警告。
- **未验证**：未上板。自动上报模式是否逐字节符合 `07 01 01 + count`、约 10 ms 周期和 500 kbit/s、静止窗口在实车振动与丢帧下是否稳定、7° 满量程和 0.3° 死区是否适合最终踏板，均需实车记录核对。CubeIDE 完整构建未运行；`0x001` 尚未在 `vehicle-interfaces` 登记，未声称已同步。

### 2026-10-19 驾驶模式色块支持触摸轮转

- 左栏 84×84 驾驶模式色块新增独立触摸区，实际命中范围外扩 8 px；按下沿按 S→Q→C→E→S 轮转，200 ms 内重复按下只算一次。色块增加 1 px 边框作为可点击提示。
- 点击通过 CANB 发送既有 `0x310` 请求：byte0 为 ASCII `0`..`3`，DLC 8，byte1..7 为 0。请求成功进入发送队列后才更新界面，ECU 的 `0x509` 回报随后校正显示；队列满时保留当前模式并提示 `MODE TX QUEUE FULL`。
- 计时锁沿用现有规则：圈速分析或 S 模式冲刺进行中不发送、不换档，并提示 `MODE LOCK: TIMER ACTIVE`。轮转基准取尚未应用的目标模式，避免方向盘请求刚到达时点击回到旧档位。
- 模拟器鼠标点击同一区域会轮转模式；`docs/dashboard_ui.md`、`docs/steering_wheel.md`、`docs/can_interface.md`、`docs/can_network.md`、`docs/configuration.md` 和 `docs/steering_wheel_code.md` 已同步，界面截图已重新生成。
- 验证：`python3 tools/run_firmware_tests.py --png /tmp/hmi-mode-touch` 通过，新增命中区、四档回绕、`0x310` 内容与 DLC、200 ms 防抖、旧 `0x509` 不撤销新请求、最新方向盘请求覆盖旧请求、待应用模式轮转、计时锁和越界拒绝用例。`./build_fw.sh clean` 通过，无编译警告；Flash 259,392 B（+324）、主 SRAM 108,352 B（+8）、CCM 61,440 B（不变）。`./run_sim_mac.sh --build-only` 通过，仅出现原有的重复库链接警告。
- **未验证**：未上板，ECU 对触摸发出的 `0x310` 是否执行、执行延迟和失败后的 `0x509` 回退未实测；未在 `vehicle-interfaces` 登记“Display 自身触摸也会发送 `0x310`”的发送来源。帧 ID、DLC 和字段沿用现有定义，未声称已同步。CubeIDE 完整构建未运行。

### 2026-10-14 电机故障码改为十进制

- 四轮电机故障码改为十进制，保留四位补零格式。例如 `0x0231` 显示为 `E 0561`，零仍显示 `OK`，无效数据仍显示 `--`。直接格式化完整诊断码，移除低 16 位截断。CAN 帧和解码不变；显示格式见 [仪表盘界面](docs/dashboard_ui.md)，文档截图已更新。
- 验证：`./build_fw.sh` 通过，0 警告；Flash 244,208 B、主 SRAM 92,120 B、CCM 61,440 B，均不变。`python3 tools/run_firmware_tests.py --png /tmp/hmi-motor-decimal` 通过，已核对渲染的 `E 0561`。`./run_sim_mac.sh --build-only` 通过，链接器报告重复库警告。

### 2026-10-18 按协议文档修正 GPS 白名单命令

- `UNLOG ALL` 改为无参数 `UNLOG`：协议文档（`docs/reference/Unicore_Reference_Commands_Manual_High_Precision_V1_CH.pdf`）§8.1 只定义 `UNLOG`（停止当前串口全部输出）、`UNLOG <语句>`、`UNLOG COM<x>`、`UNLOG COM<x> <语句>`，"ALL" 不是文档写法。若原写法被接收机拒绝，出厂默认的 1 Hz 全星座流不会被清掉，GSV 突发可能再次冲垮 DMA 环、跑动中速度显示 0。
- `GNGSV 5` 改为 `GPGSV 5`：§7.1 规定请求统一用 GP 前缀（GBGSV、GLGSV 等为非法输入；§7.2.11 的 `GPHPR` 输出即为 `$GNHPR`），GN/GP/GB 是输出说话者、由接收机按参与定位的系统决定，与请求前缀无关；解析器按报文名匹配，两种说话者的 GSV 都能收。
- 其余命令逐条核对无误、未改：`MODE ROVER AUTOMOTIVE`（§3.5 车载动态模式，水平最大 100 m/s）、`CONFIG HEADING FIXLENGTH` 与 `CONFIG HEADING LENGTH 111 10`（§4.8，两参数单位均为 cm，参数 2 为容许误差）、`GPRMC 0.05`、`GPGGA 1`、`GPHPR 0.2`（§7.2.11）、`SAVECONFIG`（§8.4）。注意 §1 的更新率示例只列到 0.1 s（10 Hz），`GPRMC 0.05`（20 Hz）待上板确认。
- `docs/configuration.md` 与 `gps.c` 注释同步。
- 验证：`mingw32-make -f build_fw.mk` 全量 ARM 构建 0 警告，Flash 244,204 B（-4，即命令字符串缩短的字节数）、主 SRAM 92,120 B、CCM 61,440 B；主机回归（CAN、里程、GPS、界面四组断言）通过。主机回归在 Windows 上无 ASan/UBSan 运行库，本次去掉 `-fsanitize` 后运行，消毒器版本需在 macOS/Linux 上补跑。
- **未验证**：未上板。`UNLOG` 的实际清除效果、`GPGSV` 返回语句的说话者与条数、20 Hz RMC 是否被接收机接受，需接上模块后确认。

### 2026-10-18 GPS 速度有效性统一、断流处理与串口诊断 CLI

**速度有效性**
- 新模块 `gps_speed.c/.h`（无硬件依赖）统一回答"现在有没有可信速度"，屏幕、CAN、圈速、冲刺、里程和 CLI 共用，取代原先各处分别清零、分别保持的做法。被接受的 RMC 须满足：校验和通过、状态 A 且模式指示不是 N、速度字段是合法数字、UTC 历元相对上一条前进。空速度、非数字、负数不再当作 0 km/h；重复或倒退的历元（按 UTC 当日毫秒模 24 h 比较，跨午夜、MCU 计时回绕都按前进处理）不续期；时间字段只有整秒时跳过重复检查；静默超过 1 s 后历元基准作废。
- 新鲜度门限 = 3 个实测输出周期，夹在 150..1500 ms（20 Hz 为 150 ms，10 Hz 为 300 ms）。周期为被接受 RMC 间隔的滑动平均；偶发断流不抬高门限，连续三次间隔都超过门限才认为输出率真的降低（如配置没生效退回 1 Hz）。取消界面 3 s 保持（`GPS_SPEED_UI_HOLD_MS`）：超过门限、状态 V（立即）或上电后尚无数据时界面显示 `--`，真实的 0 km/h 显示 `0`；恢复后首条新数据立即生效。`dashboard_view.c` 的 `Dash_View_SetSpeed(DASH_VIEW_INVALID)` 显示 `--`，`dashboard_ui.c` 新增 `Dashboard_UI_SubmitSpeedInvalid`。
- CAN 侧**行为不变**：`0x301` 仍在数据龄期超过 500 ms 或状态 V 时发 0，门限仍是 `GPS_CAN_SPEED_STALE_MS`，但读取共用的有效性状态，`0x068` 的 `RMC_VALID` 位与之一致。唯一差异：`POSITION_VALID` 位现在要求 GGA 与 RMC 位置都新鲜（此前只看最后一条 GGA）。CAN 失效门限和 `0x7A0` 状态帧"有效性变化时立即补发"按 `CLAUDE.md` 须先在 `vehicle-interfaces` 评审，**未改**。
- 位置与航向各有自己的过期：GGA 3.5 s（此前无过期，最后一条 GGA 的定位质量会一直保留），RMC 位置跟随速度门限，GPHPR 仍为 1.5 s（约 7.5 个周期，实测后可收紧），GSV 12 s。
- **修正语句分类 bug**：`is_sentence_type` 在整行里搜 "RMC"，接收机对 `GPRMC` 命令的回执 `$command,GPRMC 0.05,response: OK` 含该子串，会被当作状态为 V 的 RMC 解析、使车速失效。改为按地址字段末三个字符分类。

**断流后的圈速、冲刺与里程**
- 速度失效后重新接受数据时 `continuity_seq` 加一。圈速/冲刺/里程据此丢弃旧锚点：中断期间不用旧位置判定过线或插值；里程不跨断流累计。计时中速度/位置失效后恢复、或定位失效/坐标非法时，这一圈标记为不可信（显示为上一圈并弹出 `lap time uncertain`，不成为最快圈、不存参考轨迹、不计圈差）。冲刺 READY 阶段断流后恢复时车速已越过 0.5 km/h 则作废（`gps lost, run void`）；进行中恢复则重建锚点并标记 `75m done (gps gap)`；终点在中断期间越过则作废。
- 速度失效时 `speed_kmh` 为 0 但 `speed_valid` 为 0：S 模式准备冲刺、没有双天线航向时开始圈速，都要求速度有效（`gps speed invalid`），不再把失效速度当成停车。

**串口与接收机配置**
- **USART3/USART2 错误恢复**：HAL 在 DMA 接收遇到任何串口错误（帧、噪声、校验、溢出）时中止接收且不自动恢复，而工程里没有 `HAL_UART_ErrorCallback`，一次错误就会使 GNSS 数据永久中断到重启（USART2 圈速差输入同理）。新增 `HAL_UART_ErrorCallback`，GPS 侧重启循环 DMA 并按位计数（100 ms 内中断里最多重启 8 次，错误风暴交给任务每 10 ms 补救），任务轮询时也检查接收状态兜底；重启后读位置从 0 开始。这是本次发现的一个可能与"速度突然不动"有关的缺陷，是否是实车现象的原因需上板 `report` 确认。
- 接收机命令确认：每条命令等 `$command,<命令>,response: OK` 回执（400 ms），无回执重发至多 2 次，非 OK 记 `rejected`；连续两条无回执后放弃确认、按原固定时间等待，启动配置最坏多耗约 2.4 s。**不再每次上电 `SAVECONFIG`**（每次开机都会重发这些命令，保存只是白写 UM982 非易失存储，解决"写入寿命未评估"待办）；需要时用 `gps save`。回执格式手册（V1 CH）没有记载，按已知的 UM98x 行为实现，需上板核对。
- 新增 `docs/diagnostics.md`。

**串口诊断 CLI**（USART2，PA2/PA3，9600 8N1，与圈速差输入共用；`diag_cli.c`）
- 命令：`help`、`version`、`report [N]`、`gps status|config|capture [N]|rate 10|20|smooth on|off|save`、`sys status`、`stop`。`report` 采样 N 秒后输出带格式版本（`fmt=1`）、Git 版本、运行时间、窗口起止和明确单位的报告：RMC 输出率与间隔、UTC 历元步长、拒绝原因、各类语句数（`other` 非零说明白名单没生效）、速度龄期/门限/有效占比/失效事件、串口与 DMA 错误和重启、迟读风险、配置回执、最近的有效性切换，以及速度"提交 → 界面应用 → 刷屏完成"的耗时（`speed_trace.c`，在 `lvgl_flush_cb` 一帧最后一块写完时取点；不等于屏幕可见，面板响应测不到）。`gps capture` 导出每条 RMC 的 CSV。
- 低优先级任务（`osPriorityLow`，栈 3 KiB，纳入栈水位监测 `FW_TASK_CLI`）；中断只收发字节；发送环满时只让 CLI 任务等待，回显和提示符丢弃并计数；RX 线路连续出错 64 次后暂停接收 1 s。GPS 任务只增加计数和固定内存记录，不打印。
- **与圈速差输入共用 USART2**（实际接线在 PA2/PA3）：`diag_cli.c` 收成行，首个词是命令的行归 CLI，其余非空行经 `Diag_CLI_SetLineFallback` 交给原圈速差解析（`usart.c` 的 `USART2_ProcessLine`，规则不变，现返回是否合法），命令执行中也照常处理。USART2 波特率仍是 9600（`report` 约 3 s，`gps capture 15` 约 15 s）。回显会发到 PA2，圈速差发送方若接着 PA2 会收到。不改 `.ioc`、`stm32f4xx_it.c`：USART2 的中断和 NVIC 原本就已启用；`usart.c` 只在 USER CODE 区改回调转发，原来的行缓冲被 CLI 的行编辑取代。
- `build_fw.mk` 生成 `build_vscode/gen/fw_version.h`（`git describe --always --dirty`），内容变化时才重编 `diag_cli.c`；CubeIDE 构建没有该头，版本显示 `unknown`。

**验证**
- `./build_fw.sh clean`：0 警告。Flash 259,068 B（+14,864）、主 SRAM 108,344 B（+16,224，余 22,728）、CCM 61,440 B 不变。
- `python3 tools/run_firmware_tests.py`（macOS，ASan/UBSan）通过，新增 7 组断言：速度有效性模块（门限边界、V、重复/倒退历元、缺失字段、跨午夜、计时回绕、门限收敛）、GPS 流水线（`--` 与真实 0、回执隔离、GGA 过期、计数）、断流下的里程/冲刺/圈速、串口错误恢复与风暴限流、配置回执/拒绝/无回执/迟到回执/CLI 请求、CLI 全部命令和异常输入/拥堵/噪声、界面 `--` 与启动门控。`test_ui` 的慢组节拍相位改为显式设置，不再依赖前面用例推进的毫秒数。`--png` 渲染七帧：第 6 帧（无数据）车速由 `0` 变为 `--`，`docs/images/dashboard-nodata.png` 已更新，其余帧不变。`./run_sim_mac.sh --build-only` 通过。
- `tools/run_firmware_tests.py --compare-ref` 与本次提交之前的版本比较会因无数据帧车速改变（以及旧 `dashboard_ui.c` 没有新函数）而失败，之后以本提交为基线即可。

**未验证 / 待办**
- **全部只在主机上用模拟的串口、DMA、接收机验证，未上板**：USART3 错误恢复的真实 HAL 行为、`$command` 回执格式、`GPRMC 0.05` 与 `CONFIG SMOOTH PSRVEL` 是否被接受、150 ms 门限在实车上是否使 `--` 闪烁、CLI 开关前后 GPS/CAN/显示是否有差异。上板后运行 `report 10` 与 `gps capture 15` 对照。
- USART2 的 PA3 为 `GPIO_NOPULL`（沿用原配置），RX 线悬空时靠错误限流兜底；圈速差发送方的实际型号和接线仍未在仓库中记录，其是否受 CLI 回显影响未确认。
- **CubeIDE 完整构建未做**（本机没有 CubeIDE 可执行程序）；本次不再改 `.ioc` 和 CubeMX 生成部分，只改 `usart.c` USER CODE 区。
- **待在 `vehicle-interfaces` 评审**：CAN 车速失效门限是否改为与本地一致；`0x7A0` 是否在有效性变化时立即补发；`POSITION_VALID` 位的新语义。
- 新鲜度门限（150 ms）、GPHPR 过期（1.5 s）、周期估计的收敛参数都是初值，待实测收敛。速度平滑（`gps smooth`）和 10/20 Hz 对照实验未做。绝对 GNSS 延迟需要时间对齐或外部速度参考，CLI 只能测 MCU 内部各段和 UTC 历元是否按期推进。

### 待办

未注明日期的条目已对照 2026-10-10 的代码核对，尚未处理。

- **十进制故障码上板与 CubeIDE 验证（2026-10-14）**：未上板确认实际故障码和显示；本机未找到 CubeIDE 可执行程序，未运行 CubeIDE 完整构建。
- **模拟器与真机配置不一致**：模拟器根目录 `lv_conf.h` 为 32 位色、LVGL 池 1 MB；真机为 RGB565、60 KiB（CCM）。界面已共用 `dashboard_view.c`，但内存预算仍无法在模拟器上验证。
- **背光点亮时机**：代码上背光在 `Dashboard_UI_Init` 末尾由 TIM14 PWM 以 100% 点亮，早于随后 `lv_refr_now` 的首帧，而 `main.c` 原注释称「首帧后开背光」。上板确认开机是否闪现未刷新的 GRAM 内容，必要时把 PWM 启动移到首帧之后。
- **未使用或用途不明的外设**：USART1、USART6 已初始化但应用层未使用；PC2（输入）、PC3（输出）`.ioc` 无标签、代码未引用。确认后可从 `.ioc` 移除或补充标签。
- **缺少自动化用例**：正常无中断时圈速过线、冲刺计时的几何判断（插值精度）仍没有回归用例；断流下的行为已有（见 2026-10-18 GPS 速度有效性条目）。
- **体积较大的源文件**：`dashboard_ui.c`、`can.c`（CubeMX 生成文件的 USER 区内承载了解码、转发和 GPS 遥测）建议拆成独立模块。
- **方向盘上电首帧丢失**：方向盘只在变化时发滑级和模式，车手屏界面初始化前收到的帧被丢弃。若车手屏比方向盘晚就绪，界面滑级为 0，且在电位器被拨动前，计时键或消错键触发的 `0x703` 带滑级 0。需方向盘周期重发当前状态，或车手屏在收到首个 `0x700` 前不发滑级（协议暂无“未知”编码，须先在中央库评审）。由代码推出，未上板验证。
- **计时锁期间旋钮不同步**：锁定期间拨旋钮，车手屏不转发 `0x310`，方向盘解锁后不重发，旋钮位置与 ECU 实际模式不一致。
- **方向盘抓包与 ECU 确认**：`0x701/0x702` 松开单帧、`0x310` DLC 8 目前只来自方向盘固件代码，中央库按此确认，尚无抓包；ECU 是否使用 `0x703` 的计时位和清错位待 ECU 确认。
- **触摸模式请求的发送来源**：车手屏自身也会向 CANB 发送 `0x310`，与方向盘转发路径并存；需在 `vehicle-interfaces` 补充说明并确认 ECU 是否接受本地触摸请求。帧格式未改，未声称已同步。
- **踏板编码器接口与上板验证（2026-10-19）**：`0x001`、自动上报格式、10 ms 周期和编码器静止位尚未在 `vehicle-interfaces` 登记或抓包确认；静止窗口、7° 满量程和 0.3° 死区需实车数据校准。
- **机械制动油压标定与上板验证**：2.1 kPa → 0%、4.9 kPa → 90% 来自口头给定值，尚未在真机用实际油压核对；`0x305` 油压在台架上是否稳定、油压传感器未接时是否为 0（此时机械制动条显示 0% 而非 `--`）待上板确认。
- **默认模式显示**：ECU 上电后回报 `ModeFlag=-1`（默认模式），界面忽略该值并保持上一个模式，可能与 ECU 实际模式不一致。待 ECU 确认枚举后决定是否增加默认模式显示。
- **SOP 与 PK 行未上板验证**：`0x4A0/0x4A3` 的实际周期、丢帧和 BMS 停发时的界面表现未在真机确认；目标机 LVGL 池余量未用 `g_firmware_diag.lvgl_*` 实测；CubeIDE 完整构建未运行。中央库 DBC 的 `0x4A0/0x4A3` 接收方未列 Display，`CANB接口.md` 第 4 章仍写“尚未实现”，需在中央库评审后补登。
- **缺少用例**：计时锁生效时 `0x310` 不转发、界面启动完成前丢帧、`0x701/0x702` 触发后界面端动作。
- **GPS 白名单待上板确认**：无参数 `UNLOG` 的实际清除效果、`GPGSV` 返回语句的说话者和条数、`GPRMC 0.05`（20 Hz）与 `GPHPR 0.2`（5 Hz）是否被 UM982 接受，见上方 2026-10-18 条目。

## 2026-10-11 合并 ui-redesign 到 master

- `ui-redesign` 无冲突合并入 `master`，同时带入 master 上的提交 `db90653`（GPS 语句白名单：`UNLOG ALL` 清出厂日志，`GNGSV 5` 取多星座合并）。该提交只改了 `gps.c`，`docs/configuration.md` 的 UM982 启动命令此前未同步，现按代码更正。
- 验证：合并后 `python3 tools/run_firmware_tests.py` 通过；`./build_fw.sh clean` 0 警告，Flash 244,208 B、主 SRAM 92,120 B、CCM 61,440 B。GPS 白名单本身（UM982 实际输出、RMC 丢帧是否消除）未上板验证。

## 2026-10-11 刹车条临时改用油压

- 刹车踏板传感器的帧尚未就绪（中央库已删除 `0x306`，新 ID 未定），刹车条改由 `0x305` 的 `OilPressure_Kpa`（byte4..5，0.001 kPa）推算：2.1 kPa 为 0%，4.9 kPa 为 90%，线性换算并限幅到 0..100。`can.c` 新增 `can_oil_to_brake_pct`，刹车与油门同帧，共用 1000 ms 超时。
- 不再接收 `0x306`：删除其解码，CAN1 过滤器 bank4 去掉 `0x306`。`docs/can_interface.md`、`docs/can_network.md`、`docs/configuration.md` 同步。
- 回归测试新增油压到刹车条的边界用例（2099/2100/3500/4900/5200/5300/65535）和超时用例。
- 验证：`python3 tools/run_firmware_tests.py` 通过；`./build_fw.sh clean` 0 警告，Flash 244,212 B（与改前相同）、主 SRAM 92,120 B、CCM 61,440 B，均不变。未改模拟器，未运行 `--build-only`；未上板。

## 2026-10-11 界面加入 SOP 与最大放电电流

- 新增接收 BMS SOP V2 的 `0x4A0`（放电/回收功率上限，带单帧 CRC）和 `0x4A3`（放电/回充电流上限），CAN1 过滤器占用 bank 3，解码在新模块 `sop.c/.h`。功率超时 300 ms、电流超时 1500 ms；超时、帧非法（DLC、版本、保留位、CRC）或 BMS 自报无效时显示 `--`。不接收 ECU 的 `0x4A4`。
- 中栏车速下方新增 DISCH/REGEN 两行（kW 与 A）；放电功率或电流上限为 0 时红色。
- 右栏新增 PK 行：左为上电以来的整车放电功率峰值（kW），右为放电电流峰值（A，整安培），由 `0x4B0` 的电压电流计算，充电和回收不计，不清零。PWR 行只保留实时功率；此前 PWR 行里的 `PK 46` 用数值字体、不带单位，与其他行不一致，现统一为“灰色小字标题 + 带单位数值”，上电后尚无有效数据时显示 `-- kW`/`-- A`（原先显示 `PK 0`）。为多放一行，右栏行距由 24 px 改为 22 px。
- 统一峰值含义：界面上标 PK 的值都是上电以来的最大值，不清零。底栏电机的 `PEAK` 改名 `PK`，并**改变行为**：此前功率回到 ≤0（松油门、回收）就清零、只代表本次加速，现改为不清零。TMAX 仍是此刻最热的一处，不是峰值。
- 回归测试新增：中央库 `CANB接口.md` 4.4 的 SOP 样例帧、CRC 校验串 `4B`、非法帧拒收、超时、PK 行电流峰值、电机 PK 不被松油门清零；渲染增加上电无数据帧，共七帧；`docs/images` 三张截图按新界面重新生成。
- 验证：`python3 tools/run_firmware_tests.py` 通过；`./build_fw.sh clean` 0 警告，Flash 244,212 B（+1,076）、主 SRAM 92,120 B（+96）、CCM 不变；`./run_sim_mac.sh --build-only` 通过，并在 macOS 上启动模拟器目视确认；主机 64 位 LVGL 池峰值 54,864 → 57,576 B。
- **未验证 / 待办**：未上板，真机 BMS 两帧的实际周期和丢帧未知；目标机 LVGL 池余量未用 `g_firmware_diag.lvgl_*` 实测；CubeIDE 完整构建未运行；中央库 DBC 的 `0x4A0/0x4A3` 接收方没有 Display，`CANB接口.md` 第 4 章仍写“尚未实现”，需在中央库评审后更新，本次未改动该仓库。

## 2026-10-11 模拟器窗口 1:1 显示

- 原先 `hal.c` 按 480×272 基准取缩放 1.667，800×480 纹理被非整数拉伸；Retina 屏上还被系统 2 倍线性放大，界面发糊。现去掉缩放，窗口固定为 800×480 逻辑像素，与真机 1:1。
- 根目录 `lv_conf.h` 新增 `LV_SDL_ALLOW_HIGHDPI`（1）；`lvgl/src/drivers/sdl/lv_sdl_window.c` 据此给窗口加 `SDL_WINDOW_ALLOW_HIGHDPI`，高 DPI 屏上纹理整数倍放大。这是对 LVGL 的本地补丁，升级 LVGL 时需保留（已写入 `README.md`）。
- 验证：`./run_sim_mac.sh --build-only` 无新增警告；macOS Retina 下实际启动并截图，窗口 800×480 点、内容 1600×960 像素（恰为 2 倍）。**未在 Windows（MinGW）模拟器上验证**，系统缩放 125%/150% 时是否仍模糊未知。
- 未改：模拟器仍为 32 位色、1 MB 池，与真机 RGB565、60 KiB 不同，见「待办」。

## 2026-10-11 对齐中央库确认结果（仅文档）

- 中央库 `vehicle-interfaces`（`main` 的 `8dbc47e`）确认 Display 的 `0x310` 转发和 `0x700..0x703`，删除 `0x784` 与 `0x306`，`0x067/0x068/0x071..0x074` 保持现有 ID、不后移。
- `docs/can_interface.md` 的“与中央库尚未对齐”改为胎温和 `0x306`；`docs/steering_wheel.md` 去掉 `0x784` 待确认项；`docs/can_network.md` 更正优先级描述。固件代码和行为不变。

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
