#!/usr/bin/env bash
# 经 ST-Link (SWD) 用 STM32CubeProgrammer 命令行烧写车手屏固件 (macOS/Linux)。
#   tools/flash_stlink.sh                 # 烧写 ./build_fw.sh 的产物 stm32_port/build_vscode/DATA_COLLECTION_INTEGRAL.hex
#   tools/flash_stlink.sh path/to/fw.hex  # 烧写指定的 .hex 或 .elf (自带地址; 不支持裸 .bin)
# 只擦程序用到的扇区, 不做整片擦除, 扇区 7 的里程记录得以保留 (见 docs/odometer_storage.md)。
# 工具优先级: $STM32_PROGRAMMER_CLI > PATH > STM32 VS Code 扩展 bundles > CubeProgrammer/CubeIDE 安装目录。
set -euo pipefail

root="$(cd "$(dirname "$0")/.." && pwd)"
firmware="${1:-$root/stm32_port/build_vscode/DATA_COLLECTION_INTEGRAL.hex}"

if [ ! -s "$firmware" ]; then
    echo "烧写文件不存在或为空: $firmware；请先运行 ./build_fw.sh。" >&2
    exit 1
fi

cli="${STM32_PROGRAMMER_CLI:-}"
[ -n "$cli" ] || cli="$(command -v STM32_Programmer_CLI || true)"

if [ -z "$cli" ]; then
    for bundles in "${CUBE_BUNDLE_PATH:-}" "$HOME/Library/Application Support/stm32cube/bundles" "$HOME/.local/share/stm32cube/bundles"; do
        [ -n "$bundles" ] || continue
        for candidate in "$bundles"/programmer/*/bin/STM32_Programmer_CLI; do   # 多版本时取排序最后一个
            [ -x "$candidate" ] && cli="$candidate"
        done
        [ -n "$cli" ] && break
    done
fi

if [ -z "$cli" ]; then
    for candidate in \
        /Applications/STMicroelectronics/STM32Cube/STM32CubeProgrammer/STM32CubeProgrammer.app/Contents/MacOs/bin/STM32_Programmer_CLI \
        /Applications/STM32CubeProgrammer.app/Contents/MacOs/bin/STM32_Programmer_CLI; do
        if [ -x "$candidate" ]; then
            cli="$candidate"
            break
        fi
    done
fi

if [ -z "$cli" ]; then
    for plugins in \
        /Applications/STMicroelectronics/STM32CubeIDE.app/Contents/Eclipse/plugins \
        /Applications/STM32CubeIDE.app/Contents/Eclipse/plugins; do
        if [ -d "$plugins" ]; then
            cli="$(find "$plugins" -type f -name STM32_Programmer_CLI -print -quit)"
            [ -z "$cli" ] || break
        fi
    done
fi

if [ -z "$cli" ] || [ ! -x "$cli" ]; then
    echo "找不到可执行的 STM32_Programmer_CLI: 请安装 STM32CubeProgrammer 或在 VS Code 的 STM32Cube 扩展里安装 Programmer，" >&2
    echo "也可设置 STM32_PROGRAMMER_CLI 为程序完整路径。" >&2
    exit 1
fi

echo "使用 $cli 烧写 $firmware"
# mode=UR: 复位状态下连接, 避免固件已把 SWD 引脚改作他用或进入低功耗而连不上; -v 烧写后回读校验; -rst 烧完复位运行。
exec "$cli" -c port=SWD mode=UR -d "$firmware" -v -rst
