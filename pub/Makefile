PWD := $(shell pwd)
topdir ?= $(PWD)/..
-include $(topdir)/config.mk

export PATH:=$(pub_dir)/../bin:$(PATH)

all:
	$(Q)cd images && ln -sf uboot-$(MACH).bin uboot.bin
	$(Q)cd images && ln -sf rootfs.$(rootfs_type) rootfs.bin
	$(Q)mkimage -f update_firmware.its images/firmware.bin

clean:
	$(Q)rm -f images/*

.PHONY: all clean
