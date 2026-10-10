#!/usr/bin/env bash
# STM32F407 固件命令行构建 (macOS/Linux), 实际规则见 build_fw.mk。
# 产物输出到 stm32_port/build_vscode/ (已被 .gitignore 忽略), 不改动已被 git 跟踪的 Debug/。
#   ./build_fw.sh          # 增量构建, 末尾打印 FLASH/RAM 占用
#   ./build_fw.sh clean    # 清空输出目录后全量构建
# 工具链优先用 STM32 VS Code 扩展 bundles 内的 GNU Tools for STM32, 找不到则用 PATH 中的 arm-none-eabi-gcc。
set -euo pipefail
cd "$(dirname "$0")"
ROOT="$PWD"

gcc_bin=""
for root in "${CUBE_BUNDLE_PATH:-}" "$HOME/Library/Application Support/stm32cube/bundles" "$HOME/.local/share/stm32cube/bundles"; do
    [ -n "$root" ] || continue
    for d in "$root"/gnu-tools-for-stm32/*/bin; do   # 多版本时取排序最后一个
        [ -x "$d/arm-none-eabi-gcc" ] && gcc_bin="$d"
    done
    [ -n "$gcc_bin" ] && break
done
[ -n "$gcc_bin" ] && export PATH="$gcc_bin:$PATH"
command -v arm-none-eabi-gcc >/dev/null || {
    echo "找不到 arm-none-eabi-gcc: 请在 VS Code 的 STM32Cube 扩展里安装 GNU Tools for STM32, 或把 ARM 工具链加入 PATH" >&2
    exit 1
}
echo "工具链: $(command -v arm-none-eabi-gcc) ($(arm-none-eabi-gcc -dumpfullversion))"

cd stm32_port
[ "${1:-}" = clean ] && rm -rf build_vscode
make -f "$ROOT/build_fw.mk" -j"$(sysctl -n hw.ncpu 2>/dev/null || nproc)" all
echo "产物: stm32_port/build_vscode/DATA_COLLECTION_INTEGRAL.elf"
