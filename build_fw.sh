#!/usr/bin/env bash
# STM32F407 固件命令行构建 (macOS/Linux)。与 VS Code 的 STM32Cube 扩展共用同一个 CMake 工程 stm32_port/CMakeLists.txt,
# 等价于 `cmake --preset Debug && cmake --build --preset Debug`。
# 产物输出到 stm32_port/build/<预设>/ (已被 .gitignore 忽略), 不改动已被 git 跟踪的 Debug/。
#   ./build_fw.sh                  # 增量构建, 输出 ELF/BIN/HEX 和 FLASH/SRAM/CCM/栈帧报告
#   ./build_fw.sh clean            # 清空输出目录后全量构建
#   PRESET=Release ./build_fw.sh   # 换预设 (Debug 或 Release)
#   FW_OPT=-O0 ./build_fw.sh       # 排查优化相关问题; 不设时固定为 -Os
# 工具优先用 STM32 VS Code 扩展 bundles 里的 CMake、Ninja 和 GNU Tools for STM32, 找不到则用 PATH 中的同名工具。
set -euo pipefail
cd "$(dirname "$0")/stm32_port"

preset="${PRESET:-Debug}"

for root in "${CUBE_BUNDLE_PATH:-}" "$HOME/Library/Application Support/stm32cube/bundles" "$HOME/.local/share/stm32cube/bundles"; do
    [ -n "$root" ] && [ -d "$root" ] || continue
    for tool in cmake ninja gnu-tools-for-stm32; do
        bin=""
        for d in "$root/$tool"/*/bin "$root/$tool"/*/CMake.app/Contents/bin; do   # 多版本时取排序最后一个
            [ -d "$d" ] && bin="$d"
        done
        [ -z "$bin" ] || PATH="$bin:$PATH"
    done
    break
done
export PATH

for tool in cmake ninja arm-none-eabi-gcc; do
    command -v "$tool" >/dev/null || {
        echo "找不到 $tool: 请在 VS Code 的 STM32Cube 扩展里安装 CMake、Ninja 和 GNU Tools for STM32, 或把它们加入 PATH" >&2
        exit 1
    }
done
echo "工具链: $(command -v arm-none-eabi-gcc) ($(arm-none-eabi-gcc -dumpfullversion))"

[ "${1:-}" = clean ] && rm -rf "build/$preset"
cmake --preset "$preset" -DFW_OPT="${FW_OPT:--Os}"
cmake --build --preset "$preset" --parallel "$(sysctl -n hw.ncpu 2>/dev/null || nproc)"
echo "产物: stm32_port/build/$preset/DATA_COLLECTION_INTEGRAL.{elf,bin,hex,map,memory.json}"
