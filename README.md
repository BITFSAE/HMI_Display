	# HMI_Display — BITFSAE 车手屏

车手屏固件（STM32F407ZET6，经 FSMC 驱动 SSD1963 800×480 IPS 屏，LVGL 9.5）和 PC/macOS 模拟器。固件接收 CANB、方向盘 CAN2 与 UM982 GNSS 数据并显示，同时向 CANB 发送 GPS、里程、圈速和方向盘合并指令。仪表盘界面 `stm32_port/Core/Src/dashboard_view.c` 由固件和模拟器共用。

## 仓库结构

| 路径 | 内容 |
| --- | --- |
| `stm32_port/` | **车手屏固件**，CubeIDE 工程名仍沿用旧名 `DATA_COLLECTION_INTEGRAL`。外设配置源是 `DATA_COLLECTION_INTEGRAL.ioc`（本仓库的车手屏 `.ioc` 只有这一个），应用代码在 `Core/Src`，`Core/lvgl` 是固件用的 LVGL 副本 |
| `src/`、`CMakeLists.txt`、`config/`、`lv_conf.h`、`run_sim_mac.sh` | PC/macOS 模拟器（SDL2 + FreeRTOS） |
| `build_fw.sh`、`build_fw.mk` | 固件命令行构建 |
| `tools/` | 字体和 Logo 生成、固件内存报告、固件回归测试、GPS 测试数据生成（`gen_nmea.py`，输出 UM982 NMEA 测试语句） |
| `docs/` | 设计与参考文档，`docs/reference/` 放 Unicore（UM982）命令手册 |
| `legacy/` | 不参与构建的遗留与参考资料，含旧版 F103 板（另一个 `.ioc`），见 [legacy/README.md](legacy/README.md) |
| `lvgl/`、`FreeRTOS/`、`SDL2-2.32.10/` | 第三方源码，见下节 |
| `simulator.code-workspace`、`lv_conf.defaults`、`licence.txt` | 上游 `lv_port_pc_vscode` 遗留：前者是 Windows 旧流程的工作区文件，含作者本机路径（`cmake.cmakePath`、构建任务），需自行修改才能用；`lv_conf.defaults` 未被构建引用；`licence.txt` 是上游 MIT 许可证 |

### 第三方源码

| 目录 | 版本 | 使用方 |
| --- | --- | --- |
| `lvgl/` | 9.5.0，完整源码树 | 模拟器（CMake） |
| `stm32_port/Core/lvgl/` | 9.5.0，固件所需的裁剪子集；`src/` 与 `lvgl/src` 一致，仅 `lv_obj_style_gen.h` 一处缩进不同 | 固件（CubeIDE 与 `build_fw.sh`） |
| `FreeRTOS/` | V11.1.0 | 仅模拟器（Windows 用 MinGW 端口，其余用 POSIX 端口） |
| `stm32_port/Middlewares/Third_Party/FreeRTOS/` | V10.3.1 | 固件，由 CubeMX 管理 |
| `SDL2-2.32.10/` | 2.32.10 | 仅 Windows（MinGW）模拟器；macOS/Linux 使用系统安装的 SDL2 |

升级 LVGL 时两份一起替换，并对照模拟器的根目录 `lv_conf.h` 与固件的 `stm32_port/Core/lv_conf.h`（后者是真机内存和功能预算，二者不要互相覆盖）。

### 构建产物与本机配置不入库

`stm32_port/Debug/`、`stm32_port/build_vscode/`、`cmake-build-mac/`、`bin/` 和 Keil 的 `MDK-ARM/DATA_COLLECTION_INTEGRAL/` 都被 `.gitignore` 忽略；2026-10-10 前误入库的产物已改为不跟踪，旧版本仍可从 git 历史取回。固件请用 `./build_fw.sh` 或 CubeIDE 重新构建。`.vscode/` 和根目录 `.settings/` 是本机 IDE 配置，同样不入库。

`stm32_port/MDK-ARM/` 的 Keil 工程只含 CubeMX 生成的文件（还包括已移除的 FatFs/SD），没有 LVGL 和 `gps.c`、`dashboard_*.c`、`odometer_flash.c` 等应用模块，**不能**构建当前固件，以 CubeIDE 工程和 `build_fw.sh` 为准。

## Get started

### Install SDL and the build tools

- **Windows (vcpkg):** `vcpkg install sdl2`  (`vcpkg` can be installed from [https://github.com/microsoft/vcpkg](https://github.com/microsoft/vcpkg)) Also install either MinGW or another compiler and `cmake`.
- **macOS (Homebrew):** `brew install sdl2 cmake make`  
- **Linux:**  
  - **Debian/Ubuntu:** `sudo apt install build-essential cmake libsdl2-dev`  
  - **Arch:** `sudo pacman -S base-devel cmake sdl2`  
  - **Fedora:** `sudo dnf install @development-tools cmake SDL2-devel`  
- **Manual Installation of SDL:** Download from [SDL’s website](https://github.com/libsdl-org/SDL/releases) and place headers/libraries in your project.
- **Verify Installation:** `sdl2-config --version`, `cmake --version`, `gcc --version`, `g++ --version` (should return the installed version).  

### Get the PC project

```bash
git clone https://github.com/BITFSAE/HMI_Display.git
```

LVGL 与 FreeRTOS 已作为普通文件纳入本仓库（非 git 子模块），无需 `--recursive`。

## Usage

### macOS 命令行（推荐）

依赖：`brew install cmake ninja sdl2`。在仓库根目录执行：

```bash
./run_sim_mac.sh              # 构建并运行 800x480 模拟器窗口
./run_sim_mac.sh --night      # 以夜间模式启动（额外参数原样传给 bin/main）
./run_sim_mac.sh --build-only # 仅构建
```

- 构建目录固定为 `cmake-build-mac/`，产物 `bin/main`。不要使用 `build/`：VS Code 的 STM32Cube 扩展会用 ARM 工具链自动配置该目录，与模拟器无关。
- macOS 要求窗口与事件循环在主线程，故 macOS 下 `main()` 直接在主线程运行 LVGL 循环，不启动 FreeRTOS 调度器，且 `lv_conf.h` 中 `LV_USE_OS` 为 `LV_OS_NONE`；其他平台仍走 FreeRTOS 任务。
- 若 `cmake-build-mac/` 曾被 VS Code 的 CMake Tools/STM32Cube 扩展用 ARM 工具链配置过，缓存里会残留 `arm-none-eabi-ar`，链接报 `archive member '//' not a mach-o file`；`run_sim_mac.sh` 检测到后会自动清目录重配，并显式指定 Apple 的 `ar/ranlib`。
- VS Code（macOS）：直接打开仓库文件夹，在本机 `.vscode/`（已被 `.gitignore` 忽略）中配置 `Build Simulator`（⇧⌘B）、`Run Simulator`（任务 `Tasks: Run Test Task`）与 F5 `Debug Simulator (lldb)`，三者均调用 `run_sim_mac.sh`；需装 C/C++ 扩展，`cmake.configureOnOpen` 须关闭，且 `cmake.buildDirectory` 指向 `cmake-build-mac`，否则 STM32Cube 扩展会用 ARM 工具链污染 `build/`。首次 lldb 调试若提示开发者工具授权，按提示输入密码或执行 `sudo DevToolsSecurity -enable`。状态栏的 CMake Tools「Build/▷」按钮可能选到 ARM kit，请改用上述任务。

### 固件命令行构建（macOS/Linux）

不依赖 CubeIDE，在仓库根目录执行：

```bash
./build_fw.sh         # 增量构建 STM32F407 固件，打印 text/data/bss
./build_fw.sh clean   # 全量重编
```

- 规则在 `build_fw.mk`：编译参数与 `stm32_port/.cproject` 的 Debug 配置一致（`-Os -g3`，Cortex-M4F 硬浮点，链接 `STM32F407ZETX_FLASH.ld`），源码目录与 LVGL 排除清单直接读 `.cproject`，新增 `.c/.s` 无需改动。
- 产物位于 `stm32_port/build_vscode/`，包括 ELF、BIN、HEX、MAP 和内存报告（已被 `.gitignore` 忽略）。构建会打印 Flash、主 SRAM、CCM 占用和最大单函数栈帧。参数或工具链变化时自动重编。不复用 CubeIDE 在 `stm32_port/Debug/` 生成的 makefile：旧版入库的那份缺 `Core/lvgl/**` 的 `subdir.mk`，且写死了 Windows 下的链接脚本绝对路径。
- 固件优化、任务栈、运行诊断和回归验证见 [固件资源与性能优化](docs/firmware_optimization.md)。CubeIDE 的 `Debug/` 不入库，克隆后需在 CubeIDE 里完整构建一次。
- 工具链优先取 STM32 VS Code 扩展 bundles 里的 GNU Tools for STM32，否则用 PATH 中的 `arm-none-eabi-gcc`（已验证 ARM 官方 15.2.rel1 同样可构建）。
- VS Code 任务：`Build Firmware`、`Rebuild Firmware (clean)`（`Tasks: Run Task`），编译报错可点击跳转。
- 模拟器与固件共用 `stm32_port/Core/Src/dashboard_view.c` 界面，只是数据换成动画演示，设计与约束见 [仪表盘界面](docs/dashboard_ui.md)。
- 窗口内按键：空格循环模式，`S/Q/E/C` 选模式，`0`–`7` 滑移等级，`R` 圈速计时，`N` 夜间模式，`F` 推入演示告警，`Y` READY 横幅，`[` `]`/`G` 信号强度，`Esc`/`X` 退出。鼠标点击区域与触摸屏相同：顶栏开关计时，告警条消除一条，眼睛按钮切换夜间。

### Visual Studio Code

1. Be sure you have installed [SDL and the build tools](#install-sdl-and-the-build-tools)
2. Open the project by double clicking on `simulator.code-workspace` or opening it with `File/Open Workspace from File`
3. Install the recommended plugins
4. Click the Run and Debug page on the left, and select `Debug LVGL demo with gdb` from the drop-down on the top. Like this:
![image](https://github.com/lvgl/lv_port_pc_vscode/assets/7599318/f527b235-5718-4949-b5f0-bd807b3a64ba)
5. Click the Play button or hit F5 to start debugging.

#### ArchLinux User

VSCode does not officially provide an installation package under Arch, you need to use the AUR manager `paru` to install it.
The command is as follows:

```bash
paru -S visual-studio-code-bin
```

#### macOS

Apple's default clang does not support the `-fsanitize=leak` flag.

to build using the latest version of clang from homebrew, do the following:

1. `brew install llvm`

2. cmd+shift+p and run `Cmake: select a kit`, then `[Scan for kits]`

3. then cmd+shift+p and run `Cmake: select a kit`, select the version of clang you just installed from homebrew (it should say `Using compilers C=/opt/homebrew/opt/llvm/bin/clang ...`)

4. reconfigure by running cmd+shift+p `Cmake: Configure`

5. build using [step 4 above](#visual-studio-code)

### 模拟器的 FreeRTOS 配置

模拟器默认启用 FreeRTOS（`CMakeLists.txt` 中 `USE_FREERTOS` 为 `ON`，入口 `src/freertos_main.c`；设为 `OFF` 则回到上游的 `src/main.c` 演示入口）。`lv_conf.h` 里 macOS 的 `LV_USE_OS` 为 `LV_OS_NONE`，其余平台为 `LV_OS_FREERTOS`。FreeRTOS 堆在 `config/FreeRTOSConfig.h`，当前 96 KB。该配置与真机（`stm32_port/Core/Inc/FreeRTOSConfig.h`，由 CubeMX 管理）相互独立，改一边不影响另一边。

### CMake

This project uses CMake under the hood which can be used without Visula Studio Code too. Just type these in a Terminal when you are in the project's root folder:

```bash
mkdir build
cd build
cmake ..
make -j
```

## Run demos and examples

本节仅适用于 `USE_FREERTOS=OFF` 时的上游入口 `src/main.c`（默认的 `src/freertos_main.c` 显示仪表盘）。By default, the widgets demo (`lv_demo_widgets()`) will run. If you want to run a different demo or example from the LVGL library,
simply replace the demo function call in the code with another one—such as `lv_demo_benchmark()` or `lv_example_label_1()`.

```c
int main(int argc, char **argv)
{
  /* ... */
  /* Run the default demo */
  /* To try a different demo or example, replace this with one of: */
  /* - lv_demo_benchmark(); */
  /* - lv_demo_stress(); */
  /* - lv_example_label_1(); */
  /* - etc. */
  lv_demo_widgets(); 

  while(1) {
      /* ... */
  }
  return 0;
}
```

## Optional library

There are also FreeType and FFmpeg support. You can install these according to the followings:

### Linux

```bash
# FreeType support
wget https://kumisystems.dl.sourceforge.net/project/freetype/freetype2/2.13.2/freetype-2.13.2.tar.xz
tar -xf freetype-2.13.2.tar.xz
cd freetype-2.13.2
make
make install
```

```bash
# FFmpeg support
git clone https://git.ffmpeg.org/ffmpeg.git ffmpeg
cd ffmpeg
git checkout release/6.0
./configure --disable-all --disable-autodetect --disable-podpages --disable-asm --enable-avcodec --enable-avformat --enable-decoders --enable-encoders --enable-demuxers --enable-parsers --enable-protocol='file' --enable-swscale --enable-zlib
make
sudo make install
```
### (RT)OS support
Works with any OS like pthred, Windows, FreeRTOS, etc. It has build in support for FreeRTOS. 

## Test
This project is configured for [VSCode](https://code.visualstudio.com) and is tested on: 
- Ubuntu Linux 
- Windows WSL (Ubuntu Linux)

It requires a working version of GCC, GDB and make in your path.

To allow debugging inside VSCode you will also require a GDB [extension](https://marketplace.visualstudio.com/items?itemName=webfreak.debug) or other suitable debugger. All the requirements, build and debug settings have been pre-configured in the [.workspace](simulator.code-workspace) file.

The project can use **SDL** but it can be easily relaced by any other built-in LVGL dirvers.

## Integration with LVGL Pro

This project supports integration with LVGL Pro projects for UI development.

### Setup

1. Configure CMake with your LVGL Pro project folder:

```bash
cmake -B build -DLVGL_PRO_PROJECT_DIR=<path-to-lvgl-pro-project>
```

Build your project:

```bash
cmake --build build
```

### Usage in Code

In your main.c, include the UI header from your LVGL Pro project and replace the default demo with your screen.

```c
#include "ui.h"

int main(void) {

    /*Initialization code for LVGL*/
    
    /* Initialize the LVGL Pro UI */
    ui_init("<path-to-lvgl-pro-project>");
    
    /* ... rest of your application ...*/
}
```

## 仪表盘显示项与 CAN 数据来源

PC 模拟器（`src/freertos_main.c`，只负责演示数据和窗口）与 STM32F407 真机固件（`stm32_port/`）共用 `dashboard_view.c` 这一套界面。
CAN 接口以 [vehicle-interfaces](https://github.com/BITFSAE/vehicle-interfaces) 仓库 `can/Vehicle_CanB.dbc` 和 `docs/CANB接口.md` 为准，与本仓库同级存放时路径为 `../vehicle-interfaces/can/Vehicle_CanB.dbc`；本仓库不再保留 DBC 副本。Display 发送的 GPS 帧（`0x301/0x067/0x068` 每 50 ms，`0x7A0` 每 500 ms，`0x7A1` 每 200 ms）已在该仓库登记为 Display 已确认，对应分支 `can/display-gps-telemetry`，合并后以 `main` 提交号为准。

| 显示项 | 报文 | 说明 |
| --- | --- | --- |
| 高压总压/总流/SOC | 0x4B0 BMS_PackStatus | 右上 HV / SOC；按 byte5 bit0/1/2 分别判定电压/电流/SOC 有效，无效显示 `--`（bit3/4 为单体、温度完整位，与这三项无关）。电流充电为正、放电为负，显示原值 |
| 整车功率 | 由 0x4B0 计算 | PWR / PK；P = −V×I（放电为正），充电和回收显示 0；电压、电流任一无效则 `--` |
| 油门开度 | 0x305 DataLogger APS_OpenPct | 中央大速度数字右侧绿色条 |
| 刹车开度 | 0x306 BrakePedal_0x306（ECU 网关转发 CANC 0x270 IBS_Info） | 中央大速度数字左侧红色条；源信号为 WCBS AB_BrkPdlPct，20ms |
| 四电机扭矩/转速/温度 | 0x502 / 0x505 / 0x506 | 底部四列 T / N / Tm |
| 逆变器温度 | 0x507 Debug7 Inverter_temperature | 底部 Ti，按温度着色 |
| IGBT 温度 | 0x508 Debug8 | 参与右上面板 MAX T 计算 |
| 四电机报错码 | 0x503 / 0x504 Diagnostic_number | 底部 E 行，非零红色显示低 16 位。`Diagnostic_number_1..4` = AMK1..4 = RL/RR/FL/FR，与扭矩、转速同一套轮位重映射 |
| 低压电压/电流/功率 | 0x5A0 PDM_LowVoltageBus | 右上 LV V / LV A / LV W；四个字段同时为占位值（0x7FFF/0x7FFF/0xFFFF/0xFFFF）表示 INA226 离线，显示 `--` |
| 低压电池侧（备用） | 0x5A1 PDM_LowVoltageBattery | 已解析，UI 显示母线值 |
| 三风扇转速 + PWM 占空比 | 0x5A2 FanController_Status | 右上 F1/F2/F3；DBC 仅两路实测占空比，F3 显示 `--` |
| MAX T | 0x506/0x507/0x508 取最大 | 电机/逆变器/IGBT 四轮最大值 |

CAN1 过滤器为 ID 白名单模式（`can.c` 中 `CAN1_RX_MSG_ID_BANK*`），新增接收 ID 时需同步更新过滤器表。

### 数据有效性与超时

`dashboard_data_t.valid_mask` 按报文族记录有效位（`DASH_VALID_*`）。上电时全部为 0，对应数值显示 `--`（踏板条清空），收到有效帧后才显示；源帧自报无效（0x4B0 有效位、PDM 离线占位）或超时同样回到 `--`。CAN 任务至少每 100 ms 唤醒一次检查超时：

| 报文 | 超时 |
| --- | --- |
| 0x4B0（500 ms） | 2000 ms |
| 0x5A0、0x5A2（100 ms） | 1500 ms（`CANB接口.md` 第 8.2 节的失联门限） |
| ECU 0x305/0x306/0x502..0x509 | 1000 ms（周期仍为暂定值，故取宽松门限） |

TMAX 只统计新鲜的电机/逆变器/IGBT 温度，全部失效时显示 `--`。胎温 0x071..0x074 周期未登记，暂无超时。

### 方向盘帧（CAN2）

- `0x310`：只接受 byte0 为 ASCII `0`..`3`（48..51），其余丢弃，不转发也不显示。
- `0x700`：滑级超出 0..7 的帧丢弃，不发 `0x703`。
- `0x701/0x702`：只在上升沿动作（DBC 约定）。上一帧为 0，或上一个 1 帧距今超过 300 ms，才算一次新按下；按住期间的重复帧不再触发。方向盘实际是单帧还是重复发送、是否发 0 帧尚未确认，300 ms 是兼容两种发送方式的假设。
- `0x703` 的滑级在收到第一个有效 `0x700` 之前为 0；协议没有「滑级未知」的编码。

### CAN 控制器

CAN1、CAN2 均为 500 kbit/s，开启自动重传（NART 关）和自动 bus-off 恢复（ABOM），在 CubeMX 工程里设置。总线繁忙时低优先级帧（`0x703`、`0x7A0/0x7A1`）仲裁失败会自动重发，不再直接丢失；bus-off 由硬件恢复，`g_can_bus_off_count` 记录发生次数。无节点应答（台架单机）时发送邮箱会持续重试，软件队列满后丢新帧并计入 `g_user_can_tx_drop_count`，总线接上后先发出积压的旧帧。


### 夜间模式

左侧面板底部的圆形眼睛图标（日间睛开/夜间闭眼）：触摸后背光降至 45%，再触恢复。真机由 PF9 改接 TIM14_CH1 1kHz PWM 控制（用户代码区初始化，不依赖 CubeMX 重生）；模拟器用全屏 55% 黑色遮罩模拟，键盘 n 键可切换。
