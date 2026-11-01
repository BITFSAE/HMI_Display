# Windows PowerShell 5.1 / PowerShell 7；由 VS Code 以进程任务调用，避免默认终端的引号差异。
[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('BuildSimulator', 'RunSimulator', 'BuildFirmware', 'FlashFirmware')]
    [string]$Action,
    [switch]$Night,
    [switch]$Clean,
    [ValidateSet('Debug', 'Release')]
    [string]$Preset = 'Debug'
)

$ErrorActionPreference = 'Stop'
$repoRoot = Split-Path -Parent $PSScriptRoot
$bundleRoots = @($env:CUBE_BUNDLE_PATH)
if ($env:LOCALAPPDATA) {
    $bundleRoots += Join-Path $env:LOCALAPPDATA 'stm32cube/bundles'
}

function Find-Tool([string]$Name, [string]$Bundle) {
    foreach ($root in $bundleRoots) {
        if (-not $root -or -not (Test-Path -LiteralPath $root -PathType Container)) { continue }
        $candidates = @(Get-ChildItem -Path (Join-Path $root "$Bundle/*/bin/$Name") -File -ErrorAction SilentlyContinue |
            Sort-Object FullName -Descending)
        if ($candidates.Count) { return $candidates[0].FullName }
    }
    $tool = Get-Command $Name -CommandType Application -ErrorAction SilentlyContinue | Select-Object -First 1
    if ($tool) { return $tool.Source }
    throw "找不到 $Name；请在 STM32Cube 扩展中安装 $Bundle，或将工具加入 PATH。"
}

function Invoke-Tool([string]$Executable, [string[]]$ToolArgs) {
    & $Executable @ToolArgs
    if ($LASTEXITCODE -ne 0) { throw "$Executable 执行失败，退出码 $LASTEXITCODE。" }
}

Push-Location $repoRoot
try {
    if ($Action -eq 'FlashFirmware') {
        $firmware = Join-Path $repoRoot "stm32_port/build/$Preset/DATA_COLLECTION_INTEGRAL.hex"
        if (-not (Test-Path -LiteralPath $firmware -PathType Leaf) -or (Get-Item -LiteralPath $firmware).Length -eq 0) {
            throw "烧写文件不存在或为空：$firmware；请先运行 Build Firmware。"
        }
        $programmer = $env:STM32_PROGRAMMER_CLI
        if (-not $programmer) { $programmer = Find-Tool 'STM32_Programmer_CLI.exe' 'programmer' }
        # 与 flash_stlink.sh 保持一致，只擦写程序涉及的扇区，回读校验后复位。
        Invoke-Tool $programmer @('-c', 'port=SWD', 'mode=UR', '-d', $firmware, '-v', '-rst')
    }
    else {
        $cmake = Find-Tool 'cmake.exe' 'cmake'
        $ninja = Find-Tool 'ninja.exe' 'ninja'
        $env:PATH = "$(Split-Path $ninja);$(Split-Path $cmake);$env:PATH"
        $jobs = [Math]::Max(1, [Environment]::ProcessorCount)
        if ($Action -eq 'BuildFirmware') {
            $gcc = Find-Tool 'arm-none-eabi-gcc.exe' 'gnu-tools-for-stm32'
            $env:PATH = "$(Split-Path $gcc);$env:PATH"
            Set-Location (Join-Path $repoRoot 'stm32_port')
            Write-Host "固件工具链：$gcc"
            $optimization = if ($env:FW_OPT) { $env:FW_OPT } else { '-Os' }
            Invoke-Tool $cmake @('--preset', $Preset, "-DFW_OPT=$optimization", '-DARM_GNU_TOOLCHAIN_BIN=', "-DCMAKE_MAKE_PROGRAM=$ninja")
            $buildArgs = @('--build', '--preset', $Preset, '--parallel', "$jobs")
            # 由构建系统清理，无需递归删除目录；保留当前机器的配置缓存。
            if ($Clean) { $buildArgs += '--clean-first' }
            Invoke-Tool $cmake $buildArgs
        }
        else {
            $gcc = (Get-Command 'gcc.exe' -CommandType Application -ErrorAction Stop | Select-Object -First 1).Source
            $compilerBin = Split-Path $gcc
            $gxx = Join-Path $compilerBin 'g++.exe'
            $machine = & $gcc -dumpmachine
            if ($LASTEXITCODE -ne 0 -or $machine -notmatch '^x86_64-.*mingw32$') {
                throw '模拟器需要 64 位 MinGW GCC；请将其 bin 目录加入 PATH。'
            }
            $env:PATH = "$compilerBin;$env:PATH"
            Write-Host "模拟器工具链：$gcc"
            $sdlDll = Join-Path $repoRoot 'SDL2-2.32.10/x86_64-w64-mingw32/bin/SDL2.dll'
            if (-not (Test-Path -LiteralPath $sdlDll -PathType Leaf)) {
                throw '缺少 SDL2.dll；请按 README 的 Windows 构建说明补齐 SDL2 开发包。'
            }
            # 旧版 MinGW 需启用 C++ 格式宏；预先包含 stdlib.h，避免 SDL 的 SIMD 头抑制对齐分配声明。
            # 与固件及 macOS 缓存隔离，防止 CMake 误用 ARM 编译器。
            Invoke-Tool $cmake @('-S', $repoRoot, '-B', 'cmake-build-win', '-G', 'Ninja',
                '-DCMAKE_BUILD_TYPE=RelWithDebInfo', '-DCMAKE_EXPORT_COMPILE_COMMANDS=ON',
                '-DCMAKE_CXX_FLAGS=-D__STDC_FORMAT_MACROS', '-DCMAKE_C_FLAGS=-include stdlib.h',
                "-DCMAKE_C_COMPILER=$gcc", "-DCMAKE_CXX_COMPILER=$gxx", "-DCMAKE_MAKE_PROGRAM=$ninja")
            Invoke-Tool $cmake @('--build', 'cmake-build-win', '--parallel', "$jobs")
            Copy-Item -LiteralPath $sdlDll -Destination (Join-Path $repoRoot 'bin/SDL2.dll')
            if ($Action -eq 'RunSimulator') {
                $simArgs = @()
                if ($Night) { $simArgs += '--night' }
                Invoke-Tool (Join-Path $repoRoot 'bin/main.exe') $simArgs
            }
        }
    }
}
catch {
    Write-Error $_ -ErrorAction Continue
    exit 1
}
finally {
    Pop-Location
}
