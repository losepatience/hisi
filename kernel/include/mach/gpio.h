/* -- H -- ~ @ ~
 *
 * Copyright (c) 2013, Beijing Hanbang Technology, Inc.
 * John Lee <furious_tauren@163.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

/* Put this file in your mach/include/mach directory */
#ifndef __ARCH_HIS_GPIO_H
#define __ARCH_HIS_GPIO_H

#include <linux/io.h>

#define GPIO_DIR_OFF	0x400

#define GPIO_CHIPSIZE	0x8
#define GPIO_INSTANCES	0xC
#define ARCH_NR_GPIOS	(GPIO_CHIPSIZE * GPIO_INSTANCES)

u32 chipbase[GPIO_INSTANCES] = {
	0x20160000, 0x20170000, 0x20180000, 0x20190000,
       	0x201A0000, 0x201B0000, 0x201C0000, 0x201D0000,
       	0x20200000, 0x20210000
};

#include <linux/errno.h>
#include <asm-generic/gpio.h>

static inline int gpio_get_value(unsigned gpio)
{
	return __gpio_get_value(gpio);
}

static inline void gpio_set_value(unsigned gpio, int value)
{
	__gpio_set_value(gpio, value);
}

static inline int gpio_cansleep(unsigned gpio)
{
	return __gpio_cansleep(gpio);
}

static inline int gpio_to_irq(unsigned gpio)
{
	return __gpio_to_irq(gpio);
}

#endif

