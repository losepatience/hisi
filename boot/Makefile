PWD := $(shell pwd)
topdir ?= $(PWD)/..
-include $(topdir)/config.mk
export CROSS_COMPILE

all: patch_uboot
	$(Q)echo -en "\n\tBuilding U-boot...\n\n"
ifneq ($(shell [ -d $(pub_dir) ] && echo y),y)
	$(Q)mkdir $(pub_dir)
endif
	$(Q)$(MAKE) $(S) -C $(boot_dir) $(MACH)_config
	$(Q)$(MAKE) $(S) -C $(boot_dir)
	$(Q)dd if=$(boot_dir)/u-boot.bin of=./fb1 bs=1 count=64
	$(Q)dd if=bin/reg_info_$(MACH).bin of=./fb2 bs=4096 conv=sync
	$(Q)dd if=$(boot_dir)/u-boot.bin of=./fb3 bs=1 skip=4160
	$(Q)cat fb1 fb2 fb3 > $(pub_dir)/uboot-$(MACH).bin
	$(Q)rm -f fb1 fb2 fb3

clean:
	$(Q)$(MAKE) $(S) -C $(boot_dir) clean 

distclean: clean
	$(Q)$(MAKE) $(S) -C $(boot_dir) distclean
	
patch_uboot:
ifneq ($(shell [ -d $(boot_dir) ] && echo y),y)
	$(Q)tar -zxf $(boot_dir).tgz -C $(shell dirname $(boot_dir))
endif
	$(Q)cp -f src/update.c $(boot_dir)/common
	$(Q)cp -f src/ds28e10.c $(boot_dir)/board/hi3518
	$(Q)cp -f src/board.c $(boot_dir)/board/hi3518
	$(Q)cp -f ./include/$(MACH).h $(boot_dir)/include/configs

ifeq ($(shell echo `grep "board.o" $(boot_dir)/board/hi3518/Makefile | grep "ds28e10.o"`),)
	$(Q)sed 's/board.o/& ds28e10.o/' -i $(boot_dir)/board/hi3518/Makefile
endif

.PHONY: all clean distclean patch_uboot

