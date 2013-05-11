/* -- C -- 0 @ 0
 * ISL88013 Watchdog Driver
 *
 * Copyright (c) 2013, Beijing Hanbang Technology, Inc.
 * John Lee <furious_tauren@163.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/gpio.h>

#define his_readl(a) (*(volatile unsigned int *)IO_ADDRESS(a))
#define his_writel(v,a) (*(volatile unsigned int *)IO_ADDRESS(a) = (v))

struct his_gpio {
	spinlock_t lock;
	u32 base;
	struct gpio_chip gc;
};

static struct his_gpio hisgpio[GPIO_INSTANCES];

static int his_gpio_direction_input(struct gpio_chip *gc, u32 offset)
{
	u32 dir;
	struct his_gpio *gpio = container_of(gc, struct his_gpio, gc);

	spin_lock(&gpio->lock);
	dir = his_readl(gpio->base + GPIO_DIR_OFF);
	if ((dir & (1 << offset)))
		his_writel(dir & ~(1 << offset), gpio->base + GPIO_DIR_OFF);
	spin_unlock(&gpio->lock);
	return 0;
}

/*
 * PADDR[9:2] corresponds to GPIO_DATA[7:0]. When the corresponding
 * bit is high, it can be read or written. When the corresponding bit
 * is low, no operations are supported.
 */
static int his_gpio_get_value(struct gpio_chip *gc, u32 offset)
{
	int val;
	struct his_gpio *gpio = container_of(gc, struct his_gpio, gc);

	spin_lock(&gpio->lock);
	val = his_readl(gpio->base + (1 << (offset + 2)));
	val = val & (1 << offset) ? 1 : 0;
	spin_unlock(&gpio->lock);

	return val;
}

static void his_gpio_set_value(struct gpio_chip *gc, u32 offset, int val)
{
	struct his_gpio *gpio = container_of(gc, struct his_gpio, gc);

	if (val)
		val = 1 << offset;
	else
		val = ~(1 << offset);

	spin_lock(&gpio->lock);
	his_writel(val, gpio->base + (1 << (offset + 2)));
	spin_unlock(&gpio->lock);
}

static int his_gpio_direction_output(struct gpio_chip *gc, u32 offset, int val)
{
	u32 dir;
	struct his_gpio *gpio = container_of(gc, struct his_gpio, gc);

	spin_lock(&gpio->lock);
	dir = his_readl(gpio->base + GPIO_DIR_OFF);
	if (!(dir & (1 << offset)))
		his_writel(dir | (1 << offset), gpio->base + GPIO_DIR_OFF);
	spin_unlock(&gpio->lock);

	his_gpio_set_value(gc, offset, val);
	return 0;
}

static int __init his_gpio_init(void)
{
	int i;
	int rval;
	struct gpio_chip *gc;

	for (i = 0; i < GPIO_INSTANCES; i++) {

		spin_lock_init(&hisgpio[i].lock);
		hisgpio[i].base = chipbase[i];
		gc = &hisgpio[i].gc;

		gc->label = "hisgpio";
		gc->owner = THIS_MODULE;
		gc->ngpio = GPIO_CHIPSIZE;
		gc->base = i * GPIO_CHIPSIZE;

		gc->direction_input = his_gpio_direction_input;
		gc->direction_output = his_gpio_direction_output;
		gc->get = his_gpio_get_value;
		gc->set = his_gpio_set_value;

		rval = gpiochip_add(gc);
		if (rval) {
			while (--i > 0)
				if (gpiochip_remove(&hisgpio[i].gc))
					pr_err("remove chip(%d) failed\n", i);
			return rval;
		}
	}

	return 0;
}

subsys_initcall(his_gpio_init);

MODULE_DESCRIPTION("His GPIO Driver");
MODULE_AUTHOR("John Lee<furious_tauren@163.com>");
MODULE_LICENSE("GPL");
