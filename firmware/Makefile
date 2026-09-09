# MDX-1 firmware
#   make test     - build the portable core against the host simulator and run the test bench
#   make screens  - convert the rendered screen PBMs to PNG (needs python3 + Pillow)
#   make stm32    - cross-compile for the STM32H743 (needs arm-none-eabi-gcc and STM32CubeH7 HAL, see stm32/README.md)
CC ?= clang
CFLAGS ?= -std=c11 -O2 -Wall -Wextra -Wno-unused-parameter -Wno-unused-function -Wno-missing-field-initializers -ffast-math
CORE_SRC := $(wildcard core/dsp/*.c core/tx/*.c core/coil/*.c core/det/*.c core/audio/*.c core/ui/*.c core/log/*.c core/link/*.c core/sys/*.c)
SIM_SRC  := sim/hal_sim.c sim/main_sim.c
OBJ_DIR  := build/obj

all: test

build/mdx1_sim: $(CORE_SRC) $(SIM_SRC)
	@mkdir -p build
	$(CC) $(CFLAGS) -D_GNU_SOURCE -o $@ $(CORE_SRC) $(SIM_SRC) -lm

test: build/mdx1_sim
	cd "$(CURDIR)" && ./build/mdx1_sim

screens:
	python3 ../tools/pbm2png.py build/*.pbm

stm32:
	$(MAKE) -C stm32

clean:
	rm -rf build

.PHONY: all test screens stm32 clean
