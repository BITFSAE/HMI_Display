#!/usr/bin/env bash
# macOS 模拟器一键构建并运行 (依赖: brew install cmake ninja sdl2)。
#   ./run_sim_mac.sh                # 构建并运行
#   ./run_sim_mac.sh --night        # 额外参数原样传给 bin/main
#   ./run_sim_mac.sh --build-only   # 仅构建 (VS Code 任务使用)
# 构建目录固定为 cmake-build-mac/, 避免与 IDE (STM32Cube 扩展) 自动生成的 build/ 冲突;
# macOS 下显式使用 Apple clang, 避免 PATH 中的 arm-none-eabi-gcc 被 CMake 误选。
set -euo pipefail
cd "$(dirname "$0")"
# VS Code 任务/Finder 启动时 PATH 可能不含 Homebrew
export PATH="/opt/homebrew/bin:/usr/local/bin:$PATH"

BUILD_DIR=cmake-build-mac
BUILD_TYPE="${BUILD_TYPE:-RelWithDebInfo}"

for tool in cmake sdl2-config; do
    command -v "$tool" >/dev/null || {
        echo "缺少 $tool, 请先安装: brew install cmake ninja sdl2" >&2
        exit 1
    }
done

cmake_args=(-S . -B "$BUILD_DIR" -DCMAKE_BUILD_TYPE="$BUILD_TYPE" -DCMAKE_EXPORT_COMPILE_COMMANDS=ON)
command -v ninja >/dev/null && cmake_args+=(-G Ninja)
[ "$(uname)" = Darwin ] && cmake_args+=(-DCMAKE_C_COMPILER=/usr/bin/cc -DCMAKE_CXX_COMPILER=/usr/bin/c++)

cmake "${cmake_args[@]}"
cmake --build "$BUILD_DIR" -j

if [ "${1:-}" = "--build-only" ]; then
    exit 0
fi
exec ./bin/main "$@"
