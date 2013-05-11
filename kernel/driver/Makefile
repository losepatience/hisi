PWD := $(shell pwd)
topdir ?= $(PWD)/../..
-include $(topdir)/config.mk

ifeq ($(CONFIG_RTC_DRV_HISI3518),y)
CONFIG_RTC_DRV_HISI3518 := m
export CONFIG_RTC_DRV_HISI3518
endif

ifeq ($(CONFIG_GPIO_HISI),y)
CONFIG_GPIO_HISI := m
export CONFIG_GPIO_HISI
endif

ifeq ($(CONFIG_W1_DS28E10),y)
CONFIG_W1_DS28E10 := m
export CONFIG_W1_DS28E10
endif

ifeq ($(CONFIG_I2C_HISI3518),y)
CONFIG_I2C_HISI3518 := m
export CONFIG_I2C_HISI3518
endif

ifeq ($(CONFIG_EEPROM_24LCX),y)
CONFIG_EEPROM_24LCX := m
export CONFIG_EEPROM_24LCX
endif

ifneq ($(KERNELRELEASE),)
obj-$(CONFIG_RTC_DRV_HISI3518)	+= rtc-hisi3518.o
obj-$(CONFIG_GPIO_HISI)		+= his_gpio.o
obj-$(CONFIG_W1_DS28E10)	+= ds28e10.o
obj-$(CONFIG_I2C_HISI3518)	+= i2c-hi3518.o
obj-$(CONFIG_EEPROM_24LCX)	+= 24lcx.o
else
all:
	$(Q)$(MAKE) $(S) -C $(linux_dir) M=$(PWD) modules
	$(Q)$(MAKE) $(S) INSTALL_MOD_PATH=$(rootfs_dir) -C $(linux_dir) M=$(PWD) modules_install

clean:
	$(Q)$(MAKE) $(S) -C $(linux_dir) M=$(PWD) clean
endif

.PHONY: all clean
