PWD := $(shell pwd)
topdir ?= $(PWD)/..
-include $(topdir)/config.mk
kconfig ?= $(linux_dir)/arch/arm/Kconfig

all: patch_linux
ifneq ($(shell [ -d $(pub_dir) ] && echo y),y)
	$(Q)mkdir $(pub_dir)
endif
	$(Q)echo -en "\n\tBuilding Kernel...\n\n"
	$(Q)cp include/configs/$(MACH)_defconfig $(linux_dir)/.config -f
	$(Q)$(MAKE) $(S) -C $(linux_dir) uImage modules
	$(Q)$(MAKE) $(S) INSTALL_MOD_PATH=$(rootfs_dir) -C $(linux_dir) modules_install
	$(Q)find $(rootfs_dir)/lib/modules/ -name build -o -name source | xargs -l1 rm -rf
	$(Q)$(MAKE) $(S) -C driver 
	$(Q)cp -f $(linux_dir)/arch/arm/boot/uImage $(pub_dir)

clean:
	$(Q)$(MAKE) $(S) -C $(linux_dir) clean 
	$(Q)$(MAKE) $(S) -C driver clean

distclean: clean
	$(Q)$(MAKE) $(S) -C $(linux_dir) distclean
	
patch_linux:
	$(Q)cp include/mach $(linux_dir)/arch/arm/mach-hi3518/include -af
        
ifeq ($(shell echo `grep -A 7 "hi3518 family" $(kconfig) | grep "select ARCH_WANT_OPTIONAL_GPIOLIB"`),)
	$(Q)sed "/hi3518 family\"/a \\\tselect ARCH_WANT_OPTIONAL_GPIOLIB" -i $(kconfig)
endif

.PHONY: all patch_linux clean distclean

