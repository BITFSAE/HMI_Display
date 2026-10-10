	# VSCode Simulator project for LVGL

[LVGL](https://github.com/lvgl/lvgl) is written mainly for microcontrollers and embedded systems, however you can run the library **on your PC** as well without any embedded hardware. The code written on PC can be simply copied when your are using an embedded system.

This project is pre-configured for VSCode and should work work on Windows, Linux and MacOs as well. FreeRTOS is also included and can be optionally enabled to better simulate embedded system's behavior. 

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

- 规则在 `build_fw.mk`：编译参数照搬 `stm32_port/.cproject` 的 Debug 配置（`-O0 -g3`，Cortex-M4F 硬浮点，链接 `STM32F407ZETX_FLASH.ld`），源码目录与 LVGL 排除清单直接读 `.cproject`，新增 `.c/.s` 无需改动。
- 产物 `stm32_port/build_vscode/DATA_COLLECTION_INTEGRAL.elf`（已被 `.gitignore` 忽略）。不复用 `stm32_port/Debug/` 的 makefile：其中 `Core/lvgl/**` 的 `subdir.mk` 未入库，且 `makefile` 写死了 Windows 下的链接脚本绝对路径。
- 工具链优先取 STM32 VS Code 扩展 bundles 里的 GNU Tools for STM32，否则用 PATH 中的 `arm-none-eabi-gcc`（已验证 ARM 官方 15.2.rel1 同样可构建）。
- VS Code 任务：`Build Firmware`、`Rebuild Firmware (clean)`（`Tasks: Run Task`），编译报错可点击跳转。
- 窗口内按键：空格循环模式，`S/Q/E/C` 选模式，`0`–`7` 滑移等级，`R` 圈速记录，`N` 夜间模式，`F` 切换告警，`[` `]`/`G` 信号强度，`Esc`/`X` 退出。

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

### FreeRTOS configuration
To correctly configure the project, the RTOS (Real-Time Operating System) requires a significant amount of heap memory, especially when debugging an SDL (Simple DirectMedia Layer) window application. In this project, the heap memory has been experimentally set to **512 MB**.

```c
#define configTOTAL_HEAP_SIZE ( ( size_t ) ( 512 * 1024 * 1024 ) )  // 512 MB Heap
```
This configuration ensures that the SDL window is displayed in a timely manner. If this value is reduced, it may cause significant delays in the SDL window's appearance. If the allocated heap memory is too small, the window may fail to appear altogether.
Therefore, it is crucial to allocate sufficient heap memory to ensure smooth execution and debugging experience.

### Enable FreeRTOS 
To enable the rtos part of this project select in lv_conf.h `#define LV_USE_OS   LV_OS_NONE` to `#define LV_USE_OS  LV_OS_FREERTOS`
Additionaly you have to enable the compilation of all FreeRTOS Files by turning on the `option(USE_FREERTOS "Enable FreeRTOS" OFF)` in the CMakeLists.txt file or
by enabling the same flag from the command line when bootstrapping `cmake`:

```bash
cmake -B build -DUSE_FREERTOS=ON
```

### CMake

This project uses CMake under the hood which can be used without Visula Studio Code too. Just type these in a Terminal when you are in the project's root folder:

```bash
mkdir build
cd build
cmake ..
make -j
```

## Run demos and examples

By default, the widgets demo (`lv_demo_widgets()`) will run. If you want to run a different demo or example from the LVGL library,
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

PC 模拟器（`src/freertos_main.c`）与 STM32F407 真机固件（`stm32_port/`）保持同一套 UI。
DBC 以 [vehicle-interfaces](https://github.com/BITFSAE/vehicle-interfaces) 仓库 `can/Vehicle_CanB.dbc` 为准，副本在根目录 `Vehicle_CanB.dbc`。

| 显示项 | 报文 | 说明 |
| --- | --- | --- |
| 高压总压/总流/SOC | 0x4B0 BMS_PackStatus | 右上 TOTAL V / TOTAL A；忽略 byte5 有效性标志，直接取数据位 |
| 整车功率 | 由 0x4B0 计算 | P NOW / P PEAK |
| 油门开度 | 0x305 DataLogger APS_OpenPct | 中央大速度数字右侧绿色条 |
| 刹车开度 | 0x306 BrakePedal_0x306（ECU 网关转发 CANC 0x270 IBS_Info） | 中央大速度数字左侧红色条；源信号为 WCBS AB_BrkPdlPct，20ms |
| 四电机扭矩/转速/温度 | 0x502 / 0x505 / 0x506 | 底部四列 T / N / Tm |
| 逆变器温度 | 0x507 Debug7 Inverter_temperature | 底部 Ti，按温度着色 |
| IGBT 温度 | 0x508 Debug8 | 参与右上面板 MAX T 计算 |
| 四电机报错码 | 0x503 / 0x504 Diagnostic_number | 底部 E 行，非零红色显示低 16 位 |
| 低压电压/电流/功率 | 0x5A0 PDM_LowVoltageBus | 右上 LV V / LV A / LV W |
| 低压电池侧（备用） | 0x5A1 PDM_LowVoltageBattery | 已解析，UI 显示母线值 |
| 三风扇转速 + PWM 占空比 | 0x5A2 FanController_Status | 右上 F1/F2/F3；DBC 仅两路实测占空比，F3 显示 `--` |
| MAX T | 0x506/0x507/0x508 取最大 | 电机/逆变器/IGBT 四轮最大值 |

CAN1 过滤器为 ID 白名单模式（`can.c` 中 `CAN1_RX_MSG_ID_BANK*`），新增接收 ID 时需同步更新过滤器表。


### 夜间模式

左侧面板底部的圆形眼睛图标（日间睛开/夜间闭眼）：触摸后背光降至 45%，再触恢复。真机由 PF9 改接 TIM14_CH1 1kHz PWM 控制（用户代码区初始化，不依赖 CubeMX 重生）；模拟器用全屏 55% 黑色遮罩模拟，键盘 n 键可切换。
