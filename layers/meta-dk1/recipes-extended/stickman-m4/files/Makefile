# Firmware for the Cortex-M4 of the STM32MP157D-DK1, loaded from Linux by
# remoteproc. Needs the STM32CubeMP1 package for the HAL and CMSIS headers.

CUBE   ?= $(HOME)/STM32CubeMP1
TARGET ?= i2c_screen_cm4
# Which game is compiled in: stickman or snake
GAME   ?= stickman

HAL := $(CUBE)/Drivers/STM32MP1xx_HAL_Driver
CMSIS := $(CUBE)/Drivers/CMSIS

CROSS := arm-none-eabi-
CC := $(CROSS)gcc
OBJCOPY := $(CROSS)objcopy
SIZE := $(CROSS)size
READELF := $(CROSS)readelf

BUILD := build

CFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
          -DCORE_CM4 -DSTM32MP157Dxx -DUSE_HAL_DRIVER -DNO_ATOMIC_64_SUPPORT \
          -Iinc -I$(HAL)/Inc -I$(CMSIS)/Device/ST/STM32MP1xx/Include -I$(CMSIS)/Include \
          -O2 -g3 -ffunction-sections -fdata-sections -ffreestanding \
          -Wall -Wextra -Wno-unused-parameter

LDFLAGS := -mcpu=cortex-m4 -mthumb -mfpu=fpv4-sp-d16 -mfloat-abi=hard \
           -T ld/stm32mp157_m4_ram.ld -nostartfiles \
           --specs=nano.specs --specs=nosys.specs \
           -Wl,--gc-sections -Wl,-Map=$(BUILD)/$(TARGET).map

SRC := src/main.c src/trace.c src/rsc_table.c src/i2c5.c src/ssd1306.c \
       src/$(GAME).c src/lcd_i2c.c src/rpmsg.c src/vring.c src/ipcc.c src/stm32mp1xx_hal_msp.c src/stm32mp1xx_it.c \
       src/system_stm32mp1xx.c

HAL_SRC := stm32mp1xx_hal.c stm32mp1xx_hal_cortex.c stm32mp1xx_hal_rcc.c \
           stm32mp1xx_hal_rcc_ex.c stm32mp1xx_hal_gpio.c stm32mp1xx_hal_i2c.c \
           stm32mp1xx_hal_i2c_ex.c stm32mp1xx_hal_dma.c stm32mp1xx_hal_dma_ex.c \
           stm32mp1xx_hal_exti.c stm32mp1xx_hal_pwr.c stm32mp1xx_hal_pwr_ex.c \
           stm32mp1xx_hal_hsem.c
SRC += $(addprefix $(HAL)/Src/,$(HAL_SRC))

ASM := src/startup_stm32mp15xx.s

OBJ := $(addprefix $(BUILD)/,$(notdir $(SRC:.c=.o)) $(notdir $(ASM:.s=.o)))
vpath %.c $(sort $(dir $(SRC)))
vpath %.s $(dir $(ASM))

all: $(BUILD)/$(TARGET).elf

$(BUILD):
	mkdir -p $@

$(BUILD)/%.o: %.c | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/%.o: %.s | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/$(TARGET).elf: $(OBJ)
	$(CC) $(OBJ) $(LDFLAGS) -o $@
	@echo
	@$(SIZE) $@
	@$(READELF) -S $@ | grep -q resource_table || \
		{ echo "ERROR: no .resource_table section, remoteproc would reject this"; exit 1; }

# Regenerate the font header from the host console font.
font:
	python3 scripts/mkfont.py && mv font8x8.h inc/font8x8.h

flash: $(BUILD)/$(TARGET).elf
	python3 scripts/deploy.py $<

trace:
	python3 scripts/deploy.py --trace-only

clean:
	rm -rf $(BUILD)

.PHONY: all clean flash trace font
