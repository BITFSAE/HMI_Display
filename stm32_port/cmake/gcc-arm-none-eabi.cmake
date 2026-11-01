set(CMAKE_SYSTEM_NAME               Generic)
set(CMAKE_SYSTEM_PROCESSOR          arm)

set(CMAKE_C_COMPILER_ID GNU)
set(CMAKE_CXX_COMPILER_ID GNU)

# arm-none-eabi- 工具链需在 PATH 中。STM32Cube VS Code 扩展的 cube-cmake 会按 .settings/bundles.store.json
# 把 GNU Tools for STM32 加入 PATH；命令行用 ./build_fw.sh 或自行设置。
# 工具链不在 PATH 时，可用 -DARM_GNU_TOOLCHAIN_BIN=<bin 目录> 指定。
set(ARM_GNU_TOOLCHAIN_BIN "" CACHE PATH "Path to Arm GNU Toolchain bin directory")

if(ARM_GNU_TOOLCHAIN_BIN)
    set(TOOLCHAIN_PREFIX            ${ARM_GNU_TOOLCHAIN_BIN}/arm-none-eabi-)
else()
    set(TOOLCHAIN_PREFIX            arm-none-eabi-)
endif()

set(CMAKE_C_COMPILER                ${TOOLCHAIN_PREFIX}gcc)
set(CMAKE_ASM_COMPILER              ${CMAKE_C_COMPILER})
set(CMAKE_CXX_COMPILER              ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_LINKER                    ${TOOLCHAIN_PREFIX}g++)
set(CMAKE_OBJCOPY                   ${TOOLCHAIN_PREFIX}objcopy)
set(CMAKE_SIZE                      ${TOOLCHAIN_PREFIX}size)

set(CMAKE_EXECUTABLE_SUFFIX_ASM     ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_C       ".elf")
set(CMAKE_EXECUTABLE_SUFFIX_CXX     ".elf")

set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Cortex-M4F 硬浮点。nano.specs 同时影响头文件路径 (newlib-nano) 和链接库，所以编译和链接都要带；
# 链接命令已含 CMAKE_C_FLAGS，不要再放进 CMAKE_EXE_LINKER_FLAGS，重复指定会报 "attempt to rename spec"。
# 用 *_INIT 而不是追加 CMAKE_C_FLAGS：CMake 会多次读取工具链文件，追加会让每个参数重复。
set(TARGET_FLAGS "-mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb --specs=nano.specs")

set(CMAKE_C_FLAGS_INIT "${TARGET_FLAGS} -Wall -fdata-sections -ffunction-sections -fstack-usage")
set(CMAKE_ASM_FLAGS_INIT "${TARGET_FLAGS} -x assembler-with-cpp")
set(CMAKE_CXX_FLAGS_INIT "${TARGET_FLAGS} -Wall -fdata-sections -ffunction-sections -fstack-usage -fno-rtti -fno-exceptions -fno-threadsafe-statics")

# 这里只设调试信息；优化等级见 CMakeLists.txt 的 FW_OPT（与 CubeIDE 的 Debug/Release 一样都是 -Os）。
# 默认的 -O0 装不下 LVGL 与 RTOS 栈，不能照搬 CubeMX 模板的 Debug 配置。
set(CMAKE_C_FLAGS_DEBUG "-g3")
set(CMAKE_C_FLAGS_RELEASE "-g0")
set(CMAKE_CXX_FLAGS_DEBUG "-g3")
set(CMAKE_CXX_FLAGS_RELEASE "-g0")
