# architecture-dependent variables

#---------------- Source code ----------------
ASMSRC = lpc17xx/startup.S lpc17xx/crc.S

SRC += lpc17xx/printf.c lpc17xx/fastloader-ll.c

# Various RTC implementations
ifeq ($(CONFIG_RTC_VARIANT),3)
  SRC += rtc.c lpc17xx/rtc_lpc17xx.c lpc17xx/iec-bus.c
endif

# I2C is always needed for the config EEPROM
NEED_I2C := y
SRC += lpc17xx/i2c_lpc17xx.c lpc17xx/arch-eeprom.c

#---------------- Toolchain ----------------
CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
OBJDUMP = arm-none-eabi-objdump
SIZE = arm-none-eabi-size
NM = arm-none-eabi-nm


#---------------- Architecture variables ----------------
ARCH_CFLAGS  = -mthumb -mcpu=cortex-m3 -nostartfiles
ARCH_ASFLAGS = -mthumb -mcpu=cortex-m3
ARCH_LDFLAGS = -Tscripts/lpc17xx/$(CONFIG_MCU).ld

#---------------- Config ----------------
# currently no stack tracking supported
