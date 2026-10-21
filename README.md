# HMI_Display — BITFSAE 车手屏

车手屏固件（STM32F407ZET6，经 FSMC 驱动 SSD1963 800×480 IPS 屏，LVGL 9.5）和 PC/macOS 模拟器。固件接收 CANB、方向盘 CAN2 与 UM982 GNSS 数据并显示，同时向 CANB 发送 GPS、里程、圈速、驾驶模式请求和方向盘合并指令。仪表盘界面 `stm32_port/Core/Src/dashboard_view.c` 由固件和模拟器共用。

## 仓库结构

| 路径 | 内容 |
| --- | --- |
| `stm32_port/` | **车手屏固件**，CubeIDE 工程名仍沿用旧名 `DATA_COLLECTION_INTEGRAL`。外设配置源是 `DATA_COLLECTION_INTEGRAL.ioc`（本仓库的车手屏 `.ioc` 只有这一个），应用代码在 `Core/Src`，`Core/lvgl` 是固件用的 LVGL 副本 |
| `src/`、`CMakeLists.txt`、`config/`、`lv_conf.h`、`run_sim_mac.sh` | PC/macOS 模拟器（SDL2 + FreeRTOS） |
| `build_fw.sh`、`build_fw.mk` | 固件命令行构建 |
| `tools/` | 字体和 Logo 生成、固件内存报告、固件回归测试、GPS 测试数据生成（`gen_nmea.py`，输出 UM982 NMEA 测试语句）、ST-Link 烧写（`flash_stlink.sh`） |
| `docs/` | 设计与参考文档，索引见下节 |
| `legacy/` | 不参与构建的遗留与参考资料，含旧版 F103 板（另一个 `.ioc`），见 [legacy/README.md](legacy/README.md) |
| `lvgl/`、`FreeRTOS/`、`SDL2-2.32.10/` | 第三方源码，见下节 |
| `simulator.code-workspace`、`lv_conf.defaults`、`licence.txt` | 上游 `lv_port_pc_vscode` 遗留：前者是 Windows 旧流程的工作区文件，含作者本机路径（`cmake.cmakePath`、构建任务），需自行修改才能用；`lv_conf.defaults` 未被构建引用；`licence.txt` 是上游 MIT 许可证 |

### 文档索引

| 文档 | 内容 |
| --- | --- |
| [硬件映射](docs/hardware.md) | 时钟、引脚、CAN 位时序、FSMC 读写时序、背光、触摸、指示灯编码、与 `.ioc` 的有意差异 |
| [CAN 网络分工](docs/can_network.md) | 两条总线、各节点收发什么、车手屏做哪三类事（显示、发送、方向盘网关） |
| [方向盘与车手屏](docs/steering_wheel.md) | 方向盘按钮板和车手屏各做什么、每个控件的作用、操作的完整过程、已知限制 |
| [方向盘相关代码](docs/steering_wheel_code.md) | 方向盘固件和车手屏侧处理的代码级细节与测试覆盖 |
| [CAN 接口](docs/can_interface.md) | CAN1/CAN2 收发全表、超时与有效性、方向盘帧处理、发送队列、与中央库未对齐项 |
| [配置表](docs/configuration.md) | GPS、圈速、CAN、显示与实时的可调常量：位置、单位、影响 |
| [GPS、圈速与冲刺](docs/gps_lap.md) | NMEA 处理、信号等级、里程、过线检测、75 m 冲刺、模式锁、降级行为 |
| [串口诊断 CLI](docs/diagnostics.md) | USART2 命令行：`report`、`gps capture` 等命令、报告字段与判读、延迟测量范围 |
| [仪表盘界面](docs/dashboard_ui.md) | 布局、触摸区域、绘制约束、字库与 Logo、验证命令 |
| [固件资源与性能优化](docs/firmware_optimization.md) | Flash/SRAM/CCM 预算、任务与栈、运行诊断 |
| [里程存储](docs/odometer_storage.md) | 内部 Flash 扇区 7 的记录格式与掉电行为 |
| [更新记录](CHANGELOG.md) | 完成的变更、未验证项与待办 |
| `docs/reference/` | Unicore（UM982）高精度产品命令手册 PDF |

### 第三方源码

| 目录 | 版本 | 使用方 |
| --- | --- | --- |
| `lvgl/` | 9.5.0，完整源码树 | 模拟器（CMake） |
| `stm32_port/Core/lvgl/` | 9.5.0，固件所需的裁剪子集；`src/` 与 `lvgl/src` 一致，仅 `lv_obj_style_gen.h` 一处缩进不同 | 固件（CubeIDE 与 `build_fw.sh`） |
| `FreeRTOS/` | V11.1.0 | 仅模拟器（Windows 用 MinGW 端口，其余用 POSIX 端口） |
| `stm32_port/Middlewares/Third_Party/FreeRTOS/` | V10.3.1 | 固件，由 CubeMX 管理 |
| `SDL2-2.32.10/` | 2.32.10 | 仅 Windows（MinGW）模拟器；macOS/Linux 使用系统安装的 SDL2 |

升级 LVGL 时两份一起替换，并对照模拟器的根目录 `lv_conf.h` 与固件的 `stm32_port/Core/lv_conf.h`（后者是真机内存和功能预算，二者不要互相覆盖）。

模拟器对 LVGL 有一处本地补丁：`lvgl/src/drivers/sdl/lv_sdl_window.c` 按 `LV_SDL_ALLOW_HIGHDPI`（根目录 `lv_conf.h`）给窗口加 `SDL_WINDOW_ALLOW_HIGHDPI`，使高 DPI 屏按物理像素渲染而不发糊。替换 LVGL 后需补回。

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
- ST-Link 烧写：`tools/flash_stlink.sh [文件]` 用 STM32CubeProgrammer 命令行经 SWD 烧写 `build_vscode/DATA_COLLECTION_INTEGRAL.hex` 并回读校验、复位运行；只擦程序用到的扇区，里程记录保留。找工具的顺序见脚本头注释，也可用 `STM32_PROGRAMMER_CLI` 指定路径。VS Code 任务 `Flash Firmware (ST-Link)`、`Build & Flash Firmware (ST-Link)`（先 `Build Firmware` 再烧写）需在本机 `.vscode/tasks.json` 中配置。
- 模拟器与固件共用 `stm32_port/Core/Src/dashboard_view.c` 界面，只是数据换成动画演示，设计与约束见 [仪表盘界面](docs/dashboard_ui.md)。
- 窗口内按键：空格循环模式，`A/X/8/E` 选 Acceleration/Autocross/Skidpad/Endurance，`0`–`7` 滑移等级，`R` 圈速计时，`N` 夜间模式，`F` 推入演示告警，`Y` READY 横幅，`[` `]`/`G` 信号强度，`Esc` 退出。鼠标点击区域与触摸屏相同：顶栏开关计时，驾驶模式色块轮转模式，告警条消除一条，眼睛按钮切换夜间。

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
