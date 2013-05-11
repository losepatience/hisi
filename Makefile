PWD := $(shell pwd)
topdir ?= $(PWD)
scripts_dir := $(topdir)/scripts/kconfig
-include $(topdir)/config.mk

all: kernel rootfs boot

rootfs kernel boot:
	$(Q)$(MAKE) $(S) -C $@ $(J)

menuconfig: scripts/kconfig/mconf
	$(Q)$< Kconfig

gconfig: scripts/kconfig/gconf
	$(Q)$< Kconfig

%conf:
	$(Q)$(MAKE) -C $(scripts_dir) $(shell basename $@)

clean:
	$(Q)$(MAKE) $(S) -C $(topdir)/scripts/kconfig clean
	$(Q)$(MAKE) $(S) -C $(topdir)/boot clean
	$(Q)$(MAKE) $(S) -C $(topdir)/kernel clean
	$(Q)$(MAKE) $(S) -C $(topdir)/rootfs clean

distclean: clean
	$(Q)$(MAKE) $(S) -C $(topdir)/kernel distclean
	$(Q)$(MAKE) $(S) -C $(topdir)/rootfs distclean
	$(Q)rm -f .config

.PHONY: all boot kernel rootfs clean distclean
