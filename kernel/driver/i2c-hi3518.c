/* -- C -- ^v^
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
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/fs.h>

#include <linux/miscdevice.h>

#include <linux/i2c.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <asm/uaccess.h>
#include <linux/mutex.h>

#define APB_CLK		110000000

#define I2C_IRQNO	20
#define I2C_IRQEN	(1 << 7)
#define I2C_IRQMOD	0x101

/* i2c register offset */
#define I2C_REG_BASE	0x200D0000
#define I2C_CTRL_REG	0x000
#define I2C_COM_REB	0x004
#define I2C_ICR_REG	0x008
#define I2C_SR_REG	0x00C
#define I2C_SCLH_REG	0x010
#define I2C_SCLL_REG	0x014
#define I2C_TXR_REG	0x018
#define I2C_RXR_REG	0x01C

/* I2C_COM_REB */
#define I2C_CMD_NA	(1 << 4)
#define I2C_CMD_START	(1 << 3)
#define I2C_CMD_RD	(1 << 2)
#define I2C_CMD_WR	(1 << 1)
#define I2C_CMD_STOP	(1 << 0)

/* I2C_SR_REG */
#define I2C_START_INTR	(1 << 6)
#define I2C_NACK_INTR	(1 << 2)
#define I2C_OVER_INTR	(1 << 0)
#define I2C_ICR_CLRALL	(0x7F)

struct hisi2c_adap {
	struct mutex mutex;
	wait_queue_head_t	wait;

	struct i2c_msg		*msg;
	unsigned int		msg_num;
	unsigned int		msg_addr;
	unsigned int		msg_ptr;

	unsigned int		irq;
	void __iomem		*regs;
	struct device		*dev;
	struct i2c_adapter	adap;
};

static struct hisi2c_adap hisi2c_adap;

static void hisi2c_set_clk(struct hisi2c_adap *i2c, u32 rate)
{
	u32 scl = 0;
	u32 apb_clk = APB_CLK;

	scl = (apb_clk / (rate * 2)) / 2 - 1;
	writel(scl, i2c->regs + I2C_SCLH_REG);
	writel(scl, i2c->regs + I2C_SCLL_REG);

	/* enable i2c and its irq */
	writel(I2C_IRQMOD, i2c->regs + I2C_CTRL_REG);
	writel(I2C_ICR_CLRALL, i2c->regs + I2C_ICR_REG);
}

static inline void hisi2c_enable_irq(struct hisi2c_adap *i2c)
{
	u32 val = readl(i2c->regs + I2C_CTRL_REG);
	writel(val | I2C_IRQEN, i2c->regs + I2C_CTRL_REG);
}

static inline void hisi2c_disable_irq(struct hisi2c_adap *i2c)
{
	u32 val = readl(i2c->regs + I2C_CTRL_REG);
	writel(val & ~I2C_IRQEN, i2c->regs + I2C_CTRL_REG);
}

/* Wether this is the last byte in the current message */
static inline int is_msglast(struct hisi2c_adap *i2c)
{
	return i2c->msg_ptr == i2c->msg->len - 1;
}

/* Wether reached the end of the current message */
static inline int is_msgend(struct hisi2c_adap *i2c)
{
	return i2c->msg_ptr >= i2c->msg->len;
}

/* It does not support 10-bit address for now */
static void hisi2c_msg_start(struct hisi2c_adap *i2c)
{
	i2c->msg_ptr = 0;
	i2c->msg_addr = i2c->msg->addr << 1;

	if (i2c->msg->flags & I2C_M_RD)
		i2c->msg_addr |= 1;

	writel(i2c->msg_addr & 0xFF, i2c->regs + I2C_TXR_REG);
	writel(I2C_CMD_WR | I2C_CMD_START, i2c->regs + I2C_COM_REB);
}

static void hisi2c_msg_stop(struct hisi2c_adap *i2c)
{
	hisi2c_disable_irq(i2c);

	/* msg_num is used by hisi2c_xfer to calculate return value */
	i2c->msg_ptr = 0;
	i2c->msg_addr = 0;

	writel(I2C_CMD_STOP, i2c->regs + I2C_COM_REB);
	wake_up(&i2c->wait);
}

static void hisi2c_msg_read(struct hisi2c_adap *i2c, u32 state)
{
	u32 val;
	u32 cmd = I2C_CMD_RD;

	if (is_msglast(i2c) && i2c->msg_num == 1)
		cmd |= I2C_CMD_NA;

	if (~state & I2C_START_INTR) {
		val = readl(i2c->regs + I2C_RXR_REG);
		i2c->msg->buf[i2c->msg_ptr++] = val & 0xff;
	}

	writel(cmd, i2c->regs + I2C_COM_REB);
}

static void hisi2c_msg_write(struct hisi2c_adap *i2c, u32 state)
{
	int val;

	if (state & I2C_NACK_INTR) {
		pr_err("slave%x: no ack, state(%x)", i2c->msg_addr, state);
		return hisi2c_msg_stop(i2c);
	}

	val = i2c->msg->buf[i2c->msg_ptr++];
	writel(val, i2c->regs + I2C_TXR_REG);
	writel(I2C_CMD_WR, i2c->regs + I2C_COM_REB);
}

static irqreturn_t hisi2c_irq(int irqno, void *dev_id)
{
	struct hisi2c_adap *i2c = dev_id;
	u32 state = readl(i2c->regs + I2C_SR_REG);

	if (is_msgend(i2c)) {

		i2c->msg_num--;
		if (i2c->msg_num) {
			i2c->msg++;
			hisi2c_msg_start(i2c);

		} else { /* last msg */
			hisi2c_msg_stop(i2c);
		}

	} else if ((i2c->msg->flags & I2C_M_RD)) {
		hisi2c_msg_read(i2c, state);
	} else {
		hisi2c_msg_write(i2c, state);
	}

	writel(I2C_ICR_CLRALL, i2c->regs + I2C_ICR_REG);
	return IRQ_HANDLED;
}

static int hisi2c_doxfer(struct hisi2c_adap *i2c, struct i2c_msg *msgs, int num)
{
	unsigned long timeout;
	u32 state;
	int spins = 20;
	int rval;

	mutex_lock(&i2c->mutex);
	i2c->msg = msgs;
	i2c->msg_num = num;
	hisi2c_enable_irq(i2c);
	hisi2c_msg_start(i2c);

	timeout = wait_event_timeout(i2c->wait, i2c->msg_num == 0, 5 * HZ);
	rval = num - i2c->msg_num;

	if (timeout == 0)
		pr_err("timeout\n");
	else if (rval != num)
		pr_err("incomplete xfer (%d)\n", rval);

	/* first, try busy waiting briefly */
	do {
		state = readl(i2c->regs + I2C_SR_REG);
	} while (!(state & I2C_OVER_INTR) && --spins);

	/* if that timed out sleep */
	if (!spins) {
		msleep(1);
		state = readl(i2c->regs + I2C_SR_REG);
	}

	if (!(state & I2C_OVER_INTR))
		pr_debug("hisi2c: timeout waiting for bus idle\n");

	writel(I2C_ICR_CLRALL, i2c->regs + I2C_ICR_REG);
	mutex_unlock(&i2c->mutex);
	return rval;
}

/* return the number of msgs that has been tranfered */
static int hisi2c_xfer(struct i2c_adapter *adap, struct i2c_msg *msgs, int num)
{
	struct hisi2c_adap *i2c = (struct hisi2c_adap *)adap->algo_data;
	int rval;

	rval = hisi2c_doxfer(i2c, msgs, num);
	return rval > 0 ? rval : -EREMOTEIO;
}

static u32 hisi2c_func(struct i2c_adapter *adap)
{
	return I2C_FUNC_I2C | I2C_FUNC_SMBUS_EMUL;
}

static const struct i2c_algorithm hisi2c_algorithm = {
	.master_xfer		= hisi2c_xfer,
	.functionality		= hisi2c_func,
};


/* ----------------------------------------------------------------- */
/* this is for drivers using api of old i2c driver from hisi */
/* code may be confusing, but it really is what the old code is */
/* ----------------------------------------------------------------- */
int HI_I2C_Write(u8 addr, uint reg, uint reglen, uint data, uint len)
{
	struct i2c_adapter *adap = &hisi2c_adap.adap;
	u8 buf[reglen + len];
	int i;

	struct i2c_msg msg = {
		.addr = addr >> 1, /* in old driver, addr has 8bits */
		.flags = 0,
		.buf = buf,
		.len = reglen + len
	};

	for (i = 0; i < reglen; i++)
		buf[i] = reg >> ((reglen - i - 1) * 8);

	for (i = 0; i < len; i++)
		buf[reglen + i] = data >> ((len - i - 1) * 8);

	hisi2c_xfer(adap, &msg, 1);
	return 0;
}

int HI_I2C_Read(u8 addr, uint reg, uint reglen, uint len)
{
	struct i2c_adapter *adap = &hisi2c_adap.adap;
	unsigned int val = 0;

	struct i2c_msg msgs[2] = {
		[0] = {
			.addr = addr >> 1,
			.flags = 0,
			.buf = (u8 *)&reg,
			.len = reglen
		},
		[1] = {
			.addr = addr >> 1,
			.flags = I2C_M_RD,
			.buf = (u8 *)&val,
			.len = len
		}
	};

	hisi2c_xfer(adap, msgs, 2);
	return val;
}

EXPORT_SYMBOL(HI_I2C_Write);
EXPORT_SYMBOL(HI_I2C_Read);

typedef struct I2C_DATA {
	unsigned char addr;
	unsigned int reg;
	unsigned int reglen;
	unsigned int data;
	unsigned int len;
} I2C_DATA_S;

#define CMD_I2C_WRITE 0x01
#define CMD_I2C_READ 0x03

static int hisi2c_open(struct inode * inode, struct file * file)
{
	return 0 ;
}

static int  hisi2c_close(struct inode * inode, struct file * file)
{
	return 0;
}

static long hisi2c_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	I2C_DATA_S msg;

	if (copy_from_user(&msg, (I2C_DATA_S __user *)arg, sizeof(msg))) {
        	pr_err("%s: failed to copy data from user.\n", __func__);
		return -EFAULT;
	}

	if (cmd == CMD_I2C_WRITE) {
		return HI_I2C_Write(msg.addr, msg.reg,
				msg.reglen, msg.data, msg.len);

	} else if (cmd == CMD_I2C_READ) {
		msg.data = HI_I2C_Read(msg.addr, msg.reg, msg.reglen, msg.len);
		if (copy_to_user((I2C_DATA_S __user *)arg, &msg, sizeof(msg))) {
			pr_err("%s: failed to copy data to user.\n", __func__);
			return -EFAULT;
		}

		return 0;
	}

	return -ENOTTY;
}

static struct file_operations hisi2c_fops = {
	.owner = THIS_MODULE,
	.unlocked_ioctl = hisi2c_ioctl,
	.open = hisi2c_open,
	.release = hisi2c_close,
};

static struct miscdevice hisi2c_dev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "hi_i2c",
	.fops = &hisi2c_fops,
};
/* ----------------------------------------------------------------- */

static int __init hisi2c_module_init(void)
{
	struct hisi2c_adap *i2c = &hisi2c_adap;
	int rval;

	strlcpy(i2c->adap.name, "hisi2c", sizeof(i2c->adap.name));
	i2c->adap.owner = THIS_MODULE;
	i2c->adap.class = I2C_CLASS_HWMON;
	i2c->adap.algo = &hisi2c_algorithm;
	i2c->adap.algo_data = i2c;
	i2c->adap.nr = 0;
	i2c->adap.retries = 2;

	mutex_init(&i2c->mutex);
	init_waitqueue_head(&i2c->wait);

	i2c->regs = ioremap_nocache(I2C_REG_BASE, 0x10000);
	if (i2c->regs == NULL) {
		pr_err("Cannot map IO\n");
		return -ENXIO;
	}

	hisi2c_set_clk(i2c, 100 * 1000); /* default to 100k HZ */

	i2c->irq = I2C_IRQNO;
	rval = request_irq(i2c->irq, hisi2c_irq, IRQF_DISABLED, "hisi2c", i2c);
	if (rval) {
		pr_err("Cannot claim IRQ %d\n", i2c->irq);
		goto err_iomap;
	}

	rval = i2c_add_numbered_adapter(&i2c->adap);
	if (rval < 0) {
		pr_err("Failed to add bus to i2c core\n");
		goto  err_irq;
	}

	rval = misc_register(&hisi2c_dev);
	if (rval) {
		pr_err("Failed to add hisi2c device\n");
		goto err_dev;
	}

	return 0;

err_dev:
	i2c_del_adapter(&i2c->adap);

err_irq:
	free_irq(i2c->irq, i2c);

err_iomap:
	iounmap(i2c->regs);
	return rval;
}

static void __exit hisi2c_module_exit(void)
{
	struct hisi2c_adap *i2c = &hisi2c_adap;

	misc_deregister(&hisi2c_dev);
	i2c_del_adapter(&i2c->adap);
	free_irq(i2c->irq, i2c);
	iounmap(i2c->regs);
}


subsys_initcall(hisi2c_module_init);
module_exit(hisi2c_module_exit);

MODULE_DESCRIPTION("HISI I2C Bus driver");
MODULE_AUTHOR("John Lee, <furious_tauren@163.com>");
MODULE_LICENSE("GPL");
