/* -- C -- ^v^
 * Derived from AT24 Driver
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

/*
 * Is there someone who would help me to add the
 * 24LC16 support?
 *
 * Now, it supports 24LC16. This feature
 * is added by furious_tauren @ 2012-04-24
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/module.h>
#include <linux/slab.h>
#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/sysfs.h>
#include <linux/jiffies.h>
#include <linux/log2.h>
#include <linux/i2c.h>
#include <linux/io.h>

#define AT24F_ADDR16 (0x1)
#define AT24F_ADDR11 (0x2)

struct hb24lcx_platform_data {
	u32 byte_len;		/* size (sum of all addr) */
	u16 page_size;		/* for writes */
	u8 magic;
};

struct hb24lcx_data {
	struct hb24lcx_platform_data *chip;
	struct mutex lock;
	struct bin_attribute bin;
	u8 *writebuf;
	unsigned write_max;
	struct i2c_client *client;
};

static unsigned io_limit = 128;
module_param(io_limit, uint, 0);
MODULE_PARM_DESC(io_limit, "Maximum bytes per I/O (default 128)");

static unsigned i2c_adapter = 0;
module_param(i2c_adapter, uint, 0);
MODULE_PARM_DESC(i2c_adapter, "Which bus this device has been attached to");

static unsigned write_timeout = 25;
module_param(write_timeout, uint, 0);
MODULE_PARM_DESC(write_timeout, "Time (in ms) to try writes (default 25)");

static const struct i2c_device_id hb24lcx_ids[] = {
	{"24lcx", 0},
	{ /* END OF LIST */ }
};

MODULE_DEVICE_TABLE(i2c, hb24lcx_ids);

static ssize_t __read(struct hb24lcx_data *pdata, char *buf,
		      unsigned offset, size_t count)
{
	struct i2c_msg msg[2];
	unsigned long timeout, read_time;
	struct i2c_client *client = pdata->client;
	u8 msgbuf[2];

	if (count > io_limit)
		count = io_limit;

	memset(msg, 0, sizeof(msg));

	msgbuf[0] = offset >> 8;
	msgbuf[1] = offset;

	msg[0].addr = client->addr;
	msg[0].buf = msgbuf + 1;
	msg[0].len = 1;
	msg[1].addr = client->addr;
	msg[1].flags = I2C_M_RD;
	msg[1].buf = buf;
	msg[1].len = count;

	if (pdata->chip->magic & AT24F_ADDR16) {
		msg[0].len = 2;
		msg[0].buf -= 1;
	} else if (pdata->chip->magic & AT24F_ADDR11) {
		msg[0].addr |= offset >> 8;
		msg[1].addr = msg[0].addr;
	}

	timeout = jiffies + msecs_to_jiffies(write_timeout);
	do {
		read_time = jiffies;
		if (2 == i2c_transfer(client->adapter, msg, 2))
			return count;
		msleep(1);
	} while (time_before(read_time, timeout));

	return -ETIMEDOUT;
}

static ssize_t at24_read(struct hb24lcx_data *pdata,
			 char *buf, loff_t off, size_t count)
{
	ssize_t retval = 0;

	if (unlikely(!count))
		return count;

	mutex_lock(&pdata->lock);
	while (count) {
		ssize_t status;

		status = __read(pdata, buf, off, count);
		if (status <= 0) {
			if (retval == 0)
				retval = status;
			break;
		}
		buf += status;
		off += status;
		count -= status;
		retval += status;
	}
	mutex_unlock(&pdata->lock);

	return retval;
}

static ssize_t at24_bin_read(struct file *filp, struct kobject *kobj,
			     struct bin_attribute *attr,
			     char *buf, loff_t off, size_t count)
{
	struct hb24lcx_data *pdata;

	pdata = dev_get_drvdata(container_of(kobj, struct device, kobj));
	return at24_read(pdata, buf, off, count);
}

static ssize_t __write(struct hb24lcx_data *pdata,
		       const char *buf, unsigned offset, size_t count)
{
	struct i2c_msg msg;
	unsigned long timeout, write_time;
	unsigned next_page;
	struct i2c_client *client = pdata->client;

	if (count > pdata->write_max)
		count = pdata->write_max;

	/* Never roll over backwards, to the start of this page */
	next_page = roundup(offset + 1, pdata->chip->page_size);
	if (offset + count > next_page)
		count = next_page - offset;

	pdata->writebuf[0] = offset >> 8;
	pdata->writebuf[1] = offset;

	msg.addr = client->addr;
	msg.flags = 0;
	msg.buf = pdata->writebuf + 1;
	msg.len = 1 + count;

	memcpy(&msg.buf[1], buf, count);

	if (pdata->chip->magic & AT24F_ADDR16) {
		msg.len += 1;
		msg.buf -= 1;
	} else if (pdata->chip->magic & AT24F_ADDR11) {
		msg.addr |= offset >> 8;
	}

	timeout = jiffies + msecs_to_jiffies(write_timeout);
	do {
		write_time = jiffies;

		if (1 == i2c_transfer(client->adapter, &msg, 1)) {

			/*
			 * msleep could wastes a lots of time. usleep_range
			 * is nicer, but it is not always supported by default.
			 */
			/* usleep_range(5000, 5000); */
			msleep(5);
			return count;
		}

		msleep(1);
	} while (time_before(write_time, timeout));

	return -ETIMEDOUT;
}

static ssize_t at24_write(struct hb24lcx_data *pdata,
			  const char *buf, loff_t off, size_t count)
{
	ssize_t retval = 0;

	if (unlikely(!count))
		return count;

	mutex_lock(&pdata->lock);
	while (count) {
		ssize_t status;

		status = __write(pdata, buf, off, count);
		if (status <= 0) {
			if (retval == 0)
				retval = status;
			break;
		}
		buf += status;
		off += status;
		count -= status;
		retval += status;
	}
	mutex_unlock(&pdata->lock);

	return retval;
}

static ssize_t at24_bin_write(struct file *filp, struct kobject *kobj,
			      struct bin_attribute *attr,
			      char *buf, loff_t off, size_t count)
{
	struct hb24lcx_data *pdata;

	pdata = dev_get_drvdata(container_of(kobj, struct device, kobj));
	return at24_write(pdata, buf, off, count);
}

static int __devinit hb24lcx_probe(struct i2c_client *client,
				   const struct i2c_device_id *id)
{
	struct hb24lcx_platform_data *chip;
	struct hb24lcx_data *pdata;
	int err;

	chip = client->dev.platform_data;
	if (chip == NULL) {
		dev_info(&client->dev, "unable to find platform data\n");
		return -ENODEV;
	}

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C))
		return -ENODEV;

	pdata = kzalloc(sizeof(struct hb24lcx_data) +
			sizeof(struct i2c_client *), GFP_KERNEL);
	if (!pdata)
		return -ENOMEM;

	mutex_init(&pdata->lock);
	pdata->chip = chip;

	sysfs_bin_attr_init(&pdata->bin);
	pdata->bin.attr.name = "eeprom";
	pdata->bin.attr.mode = S_IRUGO | S_IWUSR;
	pdata->bin.read = at24_bin_read;
	pdata->bin.write = at24_bin_write;
	pdata->bin.size = chip->byte_len;
	pdata->write_max = chip->page_size;

	if (pdata->write_max > io_limit)
		pdata->write_max = io_limit;

	/* buffer (data + address at the beginning) */
	pdata->writebuf = kmalloc(pdata->write_max + 2, GFP_KERNEL);
	if (!pdata->writebuf) {
		kfree(pdata);
		return -ENOMEM;
	}

	pdata->client = client;

	err = sysfs_create_bin_file(&client->dev.kobj, &pdata->bin);
	if (err) {
		kfree(pdata->writebuf);
		kfree(pdata);
		return err;
	}

	i2c_set_clientdata(client, pdata);

	dev_info(&client->dev,
		 "%zu byte %s EEPROM, %s, %u bytes/write\n",
		 pdata->bin.size, client->name, "writable", pdata->write_max);
	return 0;
}

static int __devexit hb24lcx_remove(struct i2c_client *client)
{
	struct hb24lcx_data *pdata;

	pdata = i2c_get_clientdata(client);
	sysfs_remove_bin_file(&client->dev.kobj, &pdata->bin);

	kfree(pdata->writebuf);
	kfree(pdata);
	return 0;
}

static struct i2c_driver hb24lcx_driver = {
	.driver = {
		.name = "24lcx",
		.owner = THIS_MODULE,
	},
	.probe = hb24lcx_probe,
	.remove = __devexit_p(hb24lcx_remove),
	.id_table = hb24lcx_ids,
};

static struct hb24lcx_platform_data  hanbang_24lc32_data = {
	.byte_len = BIT(12),
	.page_size = 0x20,
	.magic = 0x1
};

/* FIXME: legency style */
static struct i2c_board_info __initdata hb24lcx_info = {
	I2C_BOARD_INFO("24lcx", 0x50),
	.platform_data = &hanbang_24lc32_data,
};

static int __init hb24lcx_init(void)
{
	struct i2c_adapter *adp;

	/*
	 * XXX: we should always take care of the following code
	 * while this driver tranplant to another board.
	 */
	adp = i2c_get_adapter(i2c_adapter); /* adapter 0 by default */
	if(adp == NULL) {
		pr_err("hb24lcx: can't get i2c adapter 0\n");
		return -ENODEV;
	} else {
		i2c_new_device(adp, &hb24lcx_info);
	}

	if (!io_limit) {
		pr_err("hb24lcx: io_limit must not be 0!\n");
		return -EINVAL;
	}

	io_limit = rounddown_pow_of_two(io_limit);
	return i2c_add_driver(&hb24lcx_driver);
}

static void __exit hb24lcx_exit(void)
{
	i2c_del_driver(&hb24lcx_driver);
}

module_init(hb24lcx_init);
module_exit(hb24lcx_exit);

MODULE_DESCRIPTION("Driver for 24LCX EEPROM");
MODULE_AUTHOR("John Lee <furious_tauren@163.com>");
MODULE_LICENSE("GPL");
