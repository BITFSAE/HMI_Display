# STM32F407 固件 Makefile (由 build_fw.sh 在 stm32_port/ 下调用, 不属于 CubeIDE 托管构建)。
# 编译参数照搬 stm32_port/.cproject 的 Debug 配置; 源码目录与 LVGL 排除清单直接读 .cproject,
# 因此 CubeIDE 里调整排除项后无需改本文件。输出到 $(OUT), 不碰 Debug/。
OUT     ?= build_vscode
TARGET  := DATA_COLLECTION_INTEGRAL
LDSCRIPT := STM32F407ZETX_FLASH.ld
CC      := arm-none-eabi-gcc
SIZE    := arm-none-eabi-size

# .cproject 中 Core 源目录的排除项(相对 Core/, 以 | 分隔), 取 Debug 配置那份
EXCL := $(shell grep -o 'excluding="[^"]*"' .cproject | head -1 | sed 's/^excluding="//;s/"$$//')
SRCS := $(shell find Core FATFS Middlewares Drivers \( -name '*.c' -o -name '*.s' \) | grep -v -E '^Core/($(EXCL))(/|$$)' | sort)
OBJS := $(addprefix $(OUT)/,$(addsuffix .o,$(basename $(SRCS))))

INCS := Core/Inc Drivers/STM32F4xx_HAL_Driver/Inc Drivers/STM32F4xx_HAL_Driver/Inc/Legacy \
        Drivers/CMSIS/Device/ST/STM32F4xx/Include Drivers/CMSIS/Include FATFS/Target FATFS/App \
        Middlewares/Third_Party/FatFs/src Middlewares/Third_Party/FreeRTOS/Source/include \
        Middlewares/Third_Party/FreeRTOS/Source/CMSIS_RTOS_V2 \
        Middlewares/Third_Party/FreeRTOS/Source/portable/GCC/ARM_CM4F

MCU     := -mcpu=cortex-m4 -mfpu=fpv4-sp-d16 -mfloat-abi=hard -mthumb --specs=nano.specs
CFLAGS  := $(MCU) -std=gnu11 -g3 -DDEBUG -DUSE_HAL_DRIVER -DSTM32F407xx $(addprefix -I,$(INCS)) \
           -O0 -ffunction-sections -fdata-sections -Wall -fstack-usage -MMD -MP
ASFLAGS := $(MCU) -g3 -DDEBUG -x assembler-with-cpp -MMD -MP $(addprefix -I,$(INCS))
LDFLAGS := $(MCU) -T$(LDSCRIPT) --specs=nosys.specs -Wl,-Map=$(OUT)/$(TARGET).map -Wl,--gc-sections -static \
           -Wl,--start-group -lc -lm -Wl,--end-group

all: $(OUT)/$(TARGET).elf
	@$(SIZE) $<

$(OUT)/$(TARGET).elf: $(OBJS) $(LDSCRIPT)
	@echo "LD  $@"
	@$(CC) -o $@ $(OBJS) $(LDFLAGS)

$(OUT)/%.o: %.c
	@echo "CC  $<"
	@mkdir -p $(dir $@)
	@$(CC) $< $(CFLAGS) -c -MF$(@:.o=.d) -MT$@ -o $@

$(OUT)/%.o: %.s
	@echo "AS  $<"
	@mkdir -p $(dir $@)
	@$(CC) $< $(ASFLAGS) -c -MF$(@:.o=.d) -MT$@ -o $@

-include $(OBJS:.o=.d)

.PHONY: all
