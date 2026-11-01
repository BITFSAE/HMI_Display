# 生成固件版本头 (cmake -P 脚本，每次构建都会运行)。
#   -DOUT=<fw_version.h 路径>  -DSRC_DIR=<git 仓库目录>
# 内容不变时不重写文件，因此只有提交或工作区变化才会重编引用它的 diag_cli.c。
# CubeIDE 构建没有这个头，固件里显示 unknown。
execute_process(
    COMMAND git describe --always --dirty --tags
    WORKING_DIRECTORY "${SRC_DIR}"
    OUTPUT_VARIABLE FW_GIT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    RESULT_VARIABLE git_result
    ERROR_QUIET)
if(NOT git_result EQUAL 0 OR FW_GIT_VERSION STREQUAL "")
    set(FW_GIT_VERSION "unknown")
endif()

set(content "#define FW_GIT_VERSION \"${FW_GIT_VERSION}\"\n")
set(old "")
if(EXISTS "${OUT}")
    file(READ "${OUT}" old)
endif()
if(NOT old STREQUAL content)
    file(WRITE "${OUT}" "${content}")
endif()
