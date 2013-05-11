/* -- C -- ~ @ ~
 *
 * Copyright (c) 2013, Beijing Hanbang Technology, Inc.
 * John Lee <furious_tauren@163.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/fs.h>
#include <linux/miscdevice.h>
#include <linux/gpio.h>
#include <linux/uaccess.h>
#include <linux/errno.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/random.h>
#include <linux/crc16.h>

#define DS28E10			"ds28e10"
#define DS28E10_RETRY_CN	3

#define DS28E10_IOC_MAGIC	'm'
#define IOCTL_GET_ROMID		_IOR(DS28E10_IOC_MAGIC, 1, int)
#define IOCTL_GET_MAC		_IOR(DS28E10_IOC_MAGIC, 2, int)
#define HI35X_IOC_MAXNR		2

static int w1_pin = 6;
module_param(w1_pin, int, 0);
MODULE_PARM_DESC(w1_pin, "GPIO used as w1 output");

static spinlock_t w1_lock;

struct ds28e10_mac_data {
	unsigned char challenge[12];
	unsigned char pagedata[32];
	unsigned char mac[22];
};

static unsigned char w1_crc8_table[] = {
	0, 94, 188, 226, 97, 63, 221, 131, 194, 156, 126, 32,
	163, 253, 31, 65, 157, 195, 33, 127, 252, 162, 64, 30,
	95, 1, 227, 189, 62, 96, 130, 220, 35, 125, 159, 193,
	66, 28, 254, 160, 225, 191, 93, 3, 128, 222, 60, 98,
	190, 224, 2, 92, 223, 129, 99, 61, 124, 34, 192, 158,
	29, 67, 161, 255, 70, 24, 250, 164, 39, 121, 155, 197,
	132, 218, 56, 102, 229, 187, 89, 7, 219, 133, 103, 57,
	186, 228, 6, 88, 25, 71, 165, 251, 120, 38, 196, 154,
	101, 59, 217, 135, 4, 90, 184, 230, 167, 249, 27, 69,
	198, 152, 122, 36, 248, 166, 68, 26, 153, 199, 37, 123,
	58, 100, 134, 216, 91, 5, 231, 185, 140, 210, 48, 110,
	237, 179, 81, 15, 78, 16, 242, 172, 47, 113, 147, 205,
	17, 79, 173, 243, 112, 46, 204, 146, 211, 141, 111, 49,
	178, 236, 14, 80, 175, 241, 19, 77, 206, 144, 114, 44,
	109, 51, 209, 143, 12, 82, 176, 238, 50, 108, 142, 208,
	83, 13, 239, 177, 240, 174, 76, 18, 145, 207, 45, 115,
	202, 148, 118, 40, 171, 245, 23, 73, 8, 86, 180, 234,
	105, 55, 213, 139, 87, 9, 235, 181, 54, 104, 138, 212,
	149, 203, 41, 119, 244, 170, 72, 22, 233, 183, 85, 11,
	136, 214, 52, 106, 43, 117, 151, 201, 74, 20, 246, 168,
	116, 42, 200, 150, 21, 75, 169, 247, 182, 232, 10, 84,
	215, 137, 107, 53
};

static u8 w1_crc8(unsigned char *data, int len)
{
	u8 crc = 0;

	while (len--)
		crc = w1_crc8_table[crc ^ *data++];

	return crc;
}

static inline void w1_gpio_pulldown(u32 pin)
{
	gpio_direction_output(pin, 0);
}

static inline void w1_gpio_release(u32 pin)
{
	gpio_direction_input(pin);
}

static inline int w1_gpio_sample(u32 pin)
{
	return gpio_get_value(pin);
}

static unsigned char w1_gpio_read_bit(u8 pin)
{
	int val;

	/*
	 *  \       /``````````````````\
	 * tF\_tRL_/....................\__
	 *
	 * the master generates a start signal by pull the bus down for tRL.
	 * then release the bus, and sample the bus within tMSR
	 */
	w1_gpio_pulldown(pin);
	udelay(6);

	w1_gpio_release(pin);
	udelay(9);

	val = w1_gpio_sample(pin);
	udelay(55);

	return val;
}

static void w1_gpio_write_bit(u8 pin, int bit)
{
	/*
	 *  \        /``````````````````\
	 * tF\_tW1L_/                    \__
	 *  \                      /````\
	 * tF\________tW0L________/      \__
	 *
	 * XXX: the master pulls the bus down for tW1L or tW0L
	 */
	if (bit) {
		w1_gpio_pulldown(pin);
		udelay(6);
		w1_gpio_release(pin);
		udelay(64);
	} else {
		w1_gpio_pulldown(pin);
		udelay(60);
		w1_gpio_release(pin);
		udelay(10);
	}
}

static int w1_gpio_reset(u32 pin)
{
	int val;

	/*
	 *  \               /```\XXXXXXXX/`````\
	 * tF\____tRSTL____/tPDH \XXXXXX/ tREC  \__
	 *                         tMSR
	 */
	w1_gpio_pulldown(pin);
	udelay(480);

	/*
	 * after tRSTL, Vpup and slave control the bus.
	 * slave waits for tPDH and then transmits a Presence
	 * Pulse by pulling the line low for tPDL.
	 */
	w1_gpio_release(pin);
	udelay(70);

	val = w1_gpio_sample(pin);
	udelay(410);

	return val;
}

static u8 w1_gpio_read_8(u32 pin)
{
	int i;
	u8 res = 0;

	for (i = 0; i < 8; ++i)
		res |= (w1_gpio_read_bit(pin) << i);

	return res;
}

static ssize_t w1_gpio_read_block(u32 pin, u8 *buf, size_t len)
{
	int i;

	for (i = 0; i < len; ++i)
		buf[i] = w1_gpio_read_8(pin);

	return len;
}

static void w1_gpio_write_8(u32 pin, u8 val)
{
	int i;

	for (i = 0; i < 8; ++i)
		w1_gpio_write_bit(pin, (val >> i) & 0x1);
}

static ssize_t w1_gpio_write_block(u32 pin, const u8 *buf, size_t len)
{
	int i;

	for (i = 0; i < len; ++i)
		w1_gpio_write_8(pin, buf[i]);

	return len;
}

/* ---------------------------------------------------------------- */
/* things about ds28e10 */
/* ---------------------------------------------------------------- */

/*
 * Due to bug of IC, we may need reset do a hardware reset for ds28e10
 * before any operation. this cost lots of time and may be unnecessary.
 */
#ifdef DS28E10_HW_RESET
static int ds28e10_hw_reset(u32 pin)
{
	unsigned long flags;
	char buf[20] = {0x55, 0x00, 0x00, 0xff, 0xff, 0xff, 0xff};

	spin_lock_irqsave(&w1_lock, flags);

	/* soft reset ds28e10 */
	if (w1_gpio_reset(pin))
		return -ENODEV;

	w1_gpio_write_8(pin, 0xcc);	/* skip the romid */

	w1_gpio_write_block(pin, buf, 7);
	w1_gpio_read_block(pin, &buf[7], 2);

	if (crc16(0, buf, 9) != 0xb001)
		return -EAGAIN;

	mdelay(100);	/* 100ms */
	w1_gpio_write_8(pin, 0x00);

	spin_unlock_irqrestore(&w1_lock, flags);
	udelay(100);	/* delay for flushing cache */

	return 0;
}
#else
static int ds28e10_hw_reset(u32 pin)
{
	return 0;
}
#endif

static int ds28e10_get_romid(u32 pin, u8 id[8])
{
	int try = 0;
	int rval;
	unsigned long flags;

	spin_lock_irqsave(&w1_lock, flags);
	do {
		if (w1_gpio_reset(pin)) {
			rval = -ENODEV;
			continue;
		}

		w1_gpio_write_8(pin, 0x33);	/* send read-romid cmd */
		w1_gpio_read_block(pin, id, 8);	/* read the 8-bytes romid */
						/* into id buffer */
		rval = w1_crc8(id, 8) ? -EAGAIN : 0;

	} while (rval && (++try < DS28E10_RETRY_CN));
	spin_unlock_irqrestore(&w1_lock, flags);

	return rval;
}

static int ds28e10_write_challenge(u32 pin, u8 *buf)
{
	int rval;
	u8 challenge[12];

	rval = w1_gpio_reset(pin);
	if (rval)
		return -ENODEV;

	w1_gpio_write_8(pin, 0xcc);	/* skip the romid */
	w1_gpio_write_8(pin, 0x0F);	/* write-challenge command */

	/* write 12-byte challenge and then read it back */
	w1_gpio_write_block(pin, buf, 12);
	w1_gpio_read_block(pin, challenge, 12);

	return memcmp(challenge, buf, 12) ? -EAGAIN : 0;
}

static int ds28e10_get_mac(u32 pin, struct ds28e10_mac_data *pdata)
{
	u8 buf[34] = {0};
	int rval;
	int try = 0;
	unsigned long flags;

	spin_lock_irqsave(&w1_lock, flags);
	do {
		rval = ds28e10_write_challenge(pin, pdata->challenge);
		if (rval)
			continue;

		rval = w1_gpio_reset(pin);
		if (rval)
			continue;

		w1_gpio_write_8(pin, 0xcc);	/* skip the romid */

		/* read authentication page 0000h */
		buf[0] = 0xa5;
		buf[1] = 0x00;
		buf[2] = 0x00;
		w1_gpio_write_block(pin, buf, 3);

		/* get the 28-bytes OTP, 0xFF and CRC16 */
		w1_gpio_read_block(pin, &buf[3], 28 + 3);
		if (crc16(0, buf, 34) != 0xb001) {
			rval = -EAGAIN;
			continue;
		}

		memcpy(pdata->pagedata, &buf[3], 28);

		/* waiting for Tcsha */
		udelay(2000);

		/* get the 20-bytes MAC and CRC16 */
		w1_gpio_read_block(pin, pdata->mac, 20 + 2);
		if (crc16(0, pdata->mac, 22) != 0xb001)
			rval = -EAGAIN;
	} while (rval && ++try < DS28E10_RETRY_CN);
	spin_unlock_irqrestore(&w1_lock, flags);

	return rval;
}

static long ds28e10_ioctl(struct file *fp, u32 cmd, ulong arg)
{
	struct ds28e10_mac_data mac;
	u8 romid[8];
	int rval = -ENOTTY;

	if (cmd == IOCTL_GET_ROMID) {
		rval = ds28e10_get_romid(w1_pin, romid);
		if (!rval && copy_to_user((u8 *)arg, romid, 8))
		       rval = -EFAULT;

	} else if (cmd == IOCTL_GET_MAC) {
		get_random_bytes((void *)(mac.challenge), 12);
		rval = ds28e10_get_mac(w1_pin, &mac);
		if (!rval && copy_to_user((void *)arg, &mac, sizeof(mac)))
		       rval = -EFAULT;
	}

	return rval;
}

static int ds28e10_open(struct inode *inode, struct file *filp)
{
	return 0;
}

static int ds28e10_release(struct inode *inode, struct file *filp)
{
	return 0;
}

static const struct file_operations ds28e10_fops = {
	.owner = THIS_MODULE,
	.open = ds28e10_open,
	.unlocked_ioctl = ds28e10_ioctl,
	.release = ds28e10_release
};

static struct miscdevice ds28e10_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = DS28E10,
	.fops = &ds28e10_fops
};

static int __init ds28e10_init(void)
{
	int rval;

	if (!gpio_is_valid(w1_pin)) {
		pr_err("%s: %d is invalid GPIO port\n", DS28E10, w1_pin);
		return -EFAULT;
	}

	rval = gpio_request(w1_pin, DS28E10);
	if (rval) {
		pr_err("%s: %d is used\n", DS28E10, w1_pin);
		return -EBUSY;
	}

	ds28e10_hw_reset(w1_pin);
	return misc_register(&ds28e10_dev);
}

static void __exit ds28e10_exit(void)
{
	gpio_free(w1_pin);
	misc_deregister(&ds28e10_dev);
}

module_init(ds28e10_init);
module_exit(ds28e10_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("John Lee <furious_tauren@163.com>");
MODULE_DESCRIPTION("ds28e10 driver");
