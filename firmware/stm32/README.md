# MDX-1 firmware: STM32H743 target

Status: **written against the STM32CubeH7 HAL API but not compiled or run on hardware** (no ARM
toolchain or HAL sources were available in the environment that generated it). The portable core
in `../core` is compiled and tested on the host (`make test`, 43 checks). Expect a normal bring-up
pass on this directory: peripheral init details, DMA stream/channel numbers and the SAI slot
configuration for the ADS127L01 frame-sync mode must be verified on the bench.

## Toolchain

```bash
brew install --cask gcc-arm-embedded          # arm-none-eabi-gcc
git clone https://github.com/STMicroelectronics/STM32CubeH7 ../../third_party/STM32CubeH7
make -C firmware stm32                         # see Makefile: expects HAL under third_party/
```

Flashing: `st-flash write build/mdx1.bin 0x08000000`, or use the STM32 USB DFU bootloader
(BOOT0 high at reset, USB-C).

## Clock tree (all from the 16.384 MHz crystal, see mdx_config.h)

| Clock | Source | Value |
|---|---|---|
| SYSCLK | PLL1: HSE/1 x 56 /2 | 458.752 MHz |
| HCLK (AHB) | /2 | 229.376 MHz |
| APB1/APB2 | /2 | 114.688 MHz (timers x2 = 229.376 MHz) |
| TIM1 tick | 229.376 MHz | 448 ticks per ADC sample |
| MCO2 (PC9) | HSE | 16.384 MHz -> both ADS127L01 CLK |
| SAI1_B | PLL2: HSE x 60 /30 | 32.768 MHz bit clock, 512 kHz frame (64 bits) |
| TIM6 | 114.688 MHz /3584 | 32 kHz audio DAC trigger |
| USB | HSI48 + CRS | 48 MHz |

## Pin map (rev A1 schematic)

| Function | Pin | Peripheral |
|---|---|---|
| TX bridge A high / low | PE9 / PE8 | TIM1_CH1 / CH1N |
| TX bridge B high / low | PE11 / PE10 | TIM1_CH2 / CH2N |
| TX fault (over-current) | PE15 | TIM1_BKIN (active high) |
| Driver enable, HV enable, VTX enable, HV bleed, analog enable | PG2, PG3, PG4, PG5, PG6 | GPIO |
| ADC clock out | PC9 | MCO2 |
| ADC SCLK / FSYNC / data | PF8 / PF9 / PF6 | SAI1_SCK_B / FS_B / SD_B (TDM master) |
| ADC START / RESET | PD11 / PD12 | GPIO |
| ADC FILTER0/1, OSR0/1 | PF0, PF1, PF2, PF3 | GPIO |
| VTX set-point | PA4 | DAC1_OUT1 |
| Audio | PA5 | DAC1_OUT2, TIM6 + DMA |
| Rail monitors VTX / VBRIDGE / VBAT / VBUS | PA0 / PA1 / PA2 / PA3 | ADC1 IN16/17/14/15 |
| I2C1 (fuel gauge 0x55, IMU 0x6A) | PB8 / PB9 | I2C1 SCL / SDA |
| IMU interrupt | PC6 | EXTI |
| GNSS UART | PC10 / PC11 | UART4 TX / RX |
| GNSS reset / PPS | PD5 / PD0 | GPIO / EXTI |
| Bluetooth UART | PA9 / PA10 | USART1 |
| Bluetooth I2S | PE2 / PE4 / PE5 / PE6 | SAI1_A MCLK / FS / SCK / SD |
| Bluetooth enable / reset | PE3 / PE7 | GPIO |
| LCD SPI | PE12 / PE14 | SPI4 SCK / MOSI |
| LCD CS / EXTCOMIN / DISP | PB10 / PB11 / PD14 | GPIO |
| Front light PWM | PD15 | TIM4_CH4 |
| Keys 0..7, power | PG9..PG15, PB4, PC13 | GPIO pull-up |
| Speaker shutdown, headphone enable, headphone detect | PD1 / PD3 / PD6 | GPIO |
| Status / charge LEDs | PB0 / PB1 | GPIO |
| Charger CHG / PGOOD, gauge interrupt | PF4 / PF5 / PF10 | GPIO |
| USB | PA11 / PA12 | USB_OTG_FS |
| microSD (1-bit) | PC8 / PC12 / PD2, detect PD7 | SDMMC1 D0 / CK / CMD |
| Coil ID 1-Wire | PB5 | GPIO (bit-banged) |
| Debug | PA13 / PA14 / PB3 | SWD + SWO |

## Firmware structure on the target

```
main.c          init, main loop (frames -> detector, UI at 30 Hz, housekeeping 10 Hz, logging)
hal_stm32.c     every hal_* function from core/hal/hal.h; interrupt handlers; DMA rings
```

ADC data path: SAI1_B receives 64-bit frames (slot 0 = ADC A, slot 1 = ADC B, 24 valid bits
each, MSB first) into a circular DMA buffer of 4 x 104 frames. The DMA half/complete
interrupts advance a read pointer; `hal_adc_get_frame` assembles one transmit period of M
samples, aligned to TIM1's update event so that sample 0 of every period is the first sample
after the TX table restarts. That alignment is what makes the coherent DFT exact; it is done
by resetting the SAI read pointer on the TIM1 repetition-counter interrupt at start-up and then
counting samples (both clocks derive from the crystal, so they never drift).

TX waveform: TIM1 runs in centre-less up-counting PWM with preload; a circular DMA burst on
the update event rewrites ARR, RCR, CCR1 and CCR2 from an 80-entry table each slot, so the
3-level pattern needs no CPU. Dead time 100 ns (DTG = 23), break input latches the outputs
off on TX_FAULT and `hal_tx_clear_fault` re-arms MOE.

PI mode (bring-up scope): the same DMA table is replaced by a reversal/hold pattern; the
constant-current top-up during the hold is not yet implemented (the hold current decays with
L/R = 0.57 ms, acceptable for 300 us holds). Records are cut from the SAI ring at each
TIM1 update.
