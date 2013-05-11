-include $(topdir)/.config

ARCH ?= arm

MACH := hi3518c
ifeq ($(CONFIG_MACH_HI3518A),y)
MACH := hi3518a
endif

ifeq ($(CROSS_COMPILE),)
CROSS_COMPILE := $(shell grep ^CONFIG_CROSS_COMPILER_PREFIX $(topdir)/.config 2>/dev/null)
CROSS_COMPILE := $(subst CONFIG_CROSS_COMPILER_PREFIX=,,$(CROSS_COMPILE))
CROSS_COMPILE := $(subst ",,$(CROSS_COMPILE))
#")
endif
ifeq ($(CROSS_COMPILE),)
CROSS_COMPILE := arm-hisiv100nptl-linux-
endif

Q ?=  @
S ?= -s
J ?= -j5
export Q S J ARCH MACH

boot_dir ?= $(topdir)/sdk3518/osdrv/uboot/u-boot-2010.06
rootfs_dir ?= $(topdir)/rootfs/fakeroot
linux_dir ?= $(topdir)/sdk3518/osdrv/kernel/linux-3.0.y
pub_dir ?= $(topdir)/pub/images

export boot_dir rootfs_dir linux_dir pub_dir

rootfs_type := cramfs
ifeq ($(CONFIG_ROOTFS_JFFS2),y)
rootfs_type := jffs2
endif

jffs2_erase_size := 64KiB
ifeq ($(CONFIG_ROOTFS_JFFS2),y)
jffs2_erase_size := $(shell grep ^CONFIG_JFFS2_ $(topdir)/.config 2>/dev/null)
jffs2_erase_size := $(subst CONFIG_JFFS2_,,$(jffs2_erase_size))
jffs2_erase_size := $(subst =y,,$(jffs2_erase_size))
endif

export rootfs_type jffs2_erase_size

