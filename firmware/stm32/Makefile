# Cross build for STM32H743ZI. Requires arm-none-eabi-gcc and STM32CubeH7 (HAL + CMSIS) checked out at
# ../../third_party/STM32CubeH7 (see README.md). Not exercised in the generating environment.
CUBE    ?= ../../third_party/STM32CubeH7
CC      := arm-none-eabi-gcc
OBJCOPY := arm-none-eabi-objcopy
MCU     := -mcpu=cortex-m7 -mthumb -mfpu=fpv5-d16 -mfloat-abi=hard
DEFS    := -DSTM32H743xx -DUSE_HAL_DRIVER -DHSE_VALUE=16384000U
INC     := -I. -I../core -I$(CUBE)/Drivers/STM32H7xx_HAL_Driver/Inc -I$(CUBE)/Drivers/CMSIS/Device/ST/STM32H7xx/Include -I$(CUBE)/Drivers/CMSIS/Include
CFLAGS  := $(MCU) $(DEFS) $(INC) -std=c11 -O2 -g -ffunction-sections -fdata-sections -Wall -ffast-math
LDFLAGS := $(MCU) -specs=nano.specs -specs=nosys.specs -Wl,--gc-sections -T STM32H743ZITx_FLASH.ld -Wl,-Map=build/mdx1.map
CORE    := $(wildcard ../core/dsp/*.c ../core/tx/*.c ../core/coil/*.c ../core/det/*.c ../core/audio/*.c ../core/ui/*.c ../core/log/*.c ../core/link/*.c ../core/sys/*.c)
HAL     := $(wildcard $(CUBE)/Drivers/STM32H7xx_HAL_Driver/Src/stm32h7xx_hal*.c)
SRC     := main.c hal_stm32.c bm83.c stm32h7xx_it.c system_stm32h7xx.c $(CORE) $(HAL)
STARTUP := $(CUBE)/Drivers/CMSIS/Device/ST/STM32H7xx/Source/Templates/gcc/startup_stm32h743xx.s

all: build/mdx1.bin
build/mdx1.elf: $(SRC) $(STARTUP)
	@mkdir -p build
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ -lm
build/mdx1.bin: build/mdx1.elf
	$(OBJCOPY) -O binary $< $@
flash: build/mdx1.bin
	st-flash write build/mdx1.bin 0x08000000
.PHONY: all flash
