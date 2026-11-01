cmake_minimum_required(VERSION 3.22)

# 车手屏固件 (STM32F407ZET6)，与 CubeIDE 工程 (.cproject) 并存，二者互不影响。
# 本文件手写维护，不由 CubeMX 生成：.ioc 的 Toolchain 保持 STM32CubeIDE，重新生成代码不会覆盖它。
# 源码目录与 LVGL 排除清单直接读 .cproject，在 CubeIDE 里调整排除项后无需改本文件。

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE "Debug")
endif()

# 产物名沿用 CubeIDE 工程名，tools/flash_stlink.sh 和文档按此名引用
set(CMAKE_PROJECT_NAME DATA_COLLECTION_INTEGRAL)

# 供 clangd 做代码索引
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

project(${CMAKE_PROJECT_NAME} C ASM)
message("Build type: " ${CMAKE_BUILD_TYPE})

# 优化等级。CubeIDE 的 Debug/Release 都是 -Os，Flash 和主 SRAM 预算按它核算 (docs/firmware_optimization.md)；
# 排查优化相关问题时可 -DFW_OPT=-O0 (可能装不下 Flash)。
set(FW_OPT "-Os" CACHE STRING "Compiler optimization level")

set(LINKER_SCRIPT ${CMAKE_CURRENT_SOURCE_DIR}/STM32F407ZETX_FLASH.ld)
get_filename_component(REPO_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/.. ABSOLUTE)

# ---- 源文件 --------------------------------------------------------------------------------
# .cproject 里 Core 源目录的排除项 (相对 Core/，以 | 分隔)，取第一份 (Debug 配置)
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${CMAKE_CURRENT_SOURCE_DIR}/.cproject)
file(READ ${CMAKE_CURRENT_SOURCE_DIR}/.cproject CPROJECT_TEXT)
string(REGEX MATCH "excluding=\"([^\"]*)\"" _unused "${CPROJECT_TEXT}")
string(REPLACE "|" ";" CORE_EXCLUDES "${CMAKE_MATCH_1}")
if(NOT CORE_EXCLUDES)
    message(FATAL_ERROR "读不到 .cproject 的 Core 排除清单，LVGL 会被整包编入")
endif()

file(GLOB_RECURSE FW_SOURCES CONFIGURE_DEPENDS RELATIVE ${CMAKE_CURRENT_SOURCE_DIR}
    Core/*.c Core/*.s Middlewares/*.c Middlewares/*.s Drivers/*.c Drivers/*.s)
list(SORT FW_SOURCES)

foreach(exclude IN LISTS CORE_EXCLUDES)
    string(REPLACE "." "\\." exclude "${exclude}")   # 排除项里的 .c 按字面匹配
    list(FILTER FW_SOURCES EXCLUDE REGEX "^Core/${exclude}(/|$)")
endforeach()

# ---- 目标 ----------------------------------------------------------------------------------
add_executable(${CMAKE_PROJECT_NAME} ${FW_SOURCES})

# 固件版本头：每次构建运行一次，内容不变则不重写 (见 cmake/fw_version.cmake)
set(FW_GEN_DIR ${CMAKE_BINARY_DIR}/gen)
add_custom_target(fw_version
    COMMAND ${CMAKE_COMMAND} -DOUT=${FW_GEN_DIR}/fw_version.h -DSRC_DIR=${REPO_ROOT}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/fw_version.cmake
    BYPRODUCTS ${FW_GEN_DIR}/fw_version.h
    COMMENT "Generating fw_version.h"
    VERBATIM)
add_dependencies(${CMAKE_PROJECT_NAME} fw_version)

target_include_directories(${CMAKE_PROJECT_NAME} PRIVATE
    ${FW_GEN_DIR}
    Core/Inc
    Drivers/STM32F4xx_HAL_Driver/Inc
    Drivers/STM32F4xx_HAL_Driver/Inc/Legacy
    Drivers/CMSIS/Device/ST/STM32F4xx/Include
    Drivers/CMSIS/Include
    Middlewares/Third_Party/FreeRTOS/Source/include
    Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2
    Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F
)

target_compile_definitions(${CMAKE_PROJECT_NAME} PRIVATE
    USE_HAL_DRIVER
    STM32F407xx
    $<$<CONFIG:Debug>:DEBUG>
)

# -Wstack-usage 与 -fstack-usage (后者在工具链文件里) 供 tools/firmware_report.py 汇总栈帧
target_compile_options(${CMAKE_PROJECT_NAME} PRIVATE ${FW_OPT} -Wstack-usage=2048)

target_link_options(${CMAKE_PROJECT_NAME} PRIVATE
    -T${LINKER_SCRIPT}
    --specs=nosys.specs
    -Wl,-Map=${CMAKE_PROJECT_NAME}.map
    -Wl,--gc-sections
    -static
)
# 静态库须放在目标文件之后
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE -Wl,--start-group -lc -lm -Wl,--end-group)
set_target_properties(${CMAKE_PROJECT_NAME} PROPERTIES
    LINK_DEPENDS ${LINKER_SCRIPT}
    ADDITIONAL_CLEAN_FILES "${CMAKE_PROJECT_NAME}.map;${CMAKE_PROJECT_NAME}.hex;${CMAKE_PROJECT_NAME}.bin")

# ---- 链接后：BIN/HEX、段大小、Flash/SRAM/CCM 占用与最大栈帧 -------------------------------------
# 工具链前缀取编译器解析后的完整路径，构建时 PATH 里没有 arm-none-eabi- 也能找到 objcopy 等。
get_filename_component(TOOLCHAIN_BIN_DIR "${CMAKE_C_COMPILER}" DIRECTORY)
if(TOOLCHAIN_BIN_DIR)
    set(ARM_TOOL_PREFIX "${TOOLCHAIN_BIN_DIR}/arm-none-eabi-")
else()
    set(ARM_TOOL_PREFIX "arm-none-eabi-")
endif()

add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
    COMMAND ${ARM_TOOL_PREFIX}objcopy -O ihex $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.hex
    COMMAND ${ARM_TOOL_PREFIX}objcopy -O binary $<TARGET_FILE:${CMAKE_PROJECT_NAME}> ${CMAKE_BINARY_DIR}/${CMAKE_PROJECT_NAME}.bin
    COMMAND ${ARM_TOOL_PREFIX}size $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
    COMMENT "Generating ${CMAKE_PROJECT_NAME}.hex/.bin"
    VERBATIM)

find_package(Python3 QUIET COMPONENTS Interpreter)
if(Python3_FOUND)
    add_custom_command(TARGET ${CMAKE_PROJECT_NAME} POST_BUILD
        COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/tools/firmware_report.py $<TARGET_FILE:${CMAKE_PROJECT_NAME}>
                --tool-prefix ${ARM_TOOL_PREFIX} --stack-dir ${CMAKE_BINARY_DIR}
        WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
        COMMENT "Firmware memory report"
        VERBATIM)
else()
    message(WARNING "找不到 Python3，跳过 Flash/SRAM/CCM 占用报告 (tools/firmware_report.py)")
endif()
