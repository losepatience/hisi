PWD := $(shell pwd)
topdir ?= $(PWD)/..
-include $(topdir)/config.mk
bb := busybox-1.21.0

all: busybox
	$(Q)tar -jxf tars/prebuild.tar.bz2 -C $(rootfs_dir)/..
	$(Q)tar -jxf tars/udev.tar.bz2 -C $(rootfs_dir)/..
	$(Q)tar -jxf tars/tools.tar.bz2 -C $(rootfs_dir)/..

ifneq ($(shell [ -d $(pub_dir) ] && echo y),y)
	$(Q)mkdir $(pub_dir)
endif
	$(Q)mkdir logfs

	$(Q)rm -f _fakeroot.build
	$(Q)touch _fakeroot.build
	$(Q)echo "chown -R 0:0 $(rootfs_dir)" >> _fakeroot.build
	$(Q)echo "chown -R 0:0 logfs" >> _fakeroot.build

ifeq ($(rootfs_type),jffs2)
	$(Q)echo "mkfs.jffs2 -d $(rootfs_dir) -l -e $(jffs2_erase_size) -o $(pub_dir)/rootfs.jffs2" >> _fakeroot.build
else
	$(Q)echo "mkcramfs $(rootfs_dir) $(pub_dir)/rootfs.cramfs" >> _fakeroot.build
endif
	$(Q)echo "mkfs.jffs2 -d logfs -l -e $(jffs2_erase_size) -o $(pub_dir)/logfs.jffs2" >> _fakeroot.build
	$(Q)chmod +x _fakeroot.build
	$(Q)fakeroot -- $(shell dirname $(rootfs_dir))/_fakeroot.build
	$(Q)rm -fr logfs _fakeroot.build

busybox:
	$(Q)echo -en "\n\tBuilding Rootfs...\n"
	$(Q)cp -f include/configs/defconfig $(bb)/.config
	$(Q)$(MAKE) -C $(bb)
	$(Q)$(MAKE) $(S) CONFIG_PREFIX=$(rootfs_dir) -C $(bb) install

clean:
	$(Q)$(MAKE) -C $(bb) clean
	-$(Q)rm -fr $(rootfs_dir) _fakeroot.build

distclean: clean
	$(Q)$(MAKE) -C $(bb) distclean

.PHONY: all clean distclean busybox jffs2 cramfs
