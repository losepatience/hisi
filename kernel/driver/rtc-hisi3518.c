/*
 * Copyright (c) 2013, Beijing Hanbang Technology, Inc.
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
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/timer.h>
#include <linux/types.h>
#include <linux/io.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/platform_device.h>

#include <linux/rtc.h>

/* RTC Control over SPI */
#define RTC_SPI_BASE_ADDR	IO_ADDRESS(0x20060000)
#define SPI_CLK_DIV			(RTC_SPI_BASE_ADDR + 0x000)
#define SPI_RW				(RTC_SPI_BASE_ADDR + 0x004)

typedef union {
    struct {
        unsigned int spi_wdata      : 8; /* [7:0] */
        unsigned int spi_rdata      : 8; /* [15:8] */
        unsigned int spi_addr       : 7; /* [22:16] */
        unsigned int spi_rw         : 1; /* [23] */
        unsigned int spi_start      : 1; /* [24] */
        unsigned int reserved       : 6; /* [30:25] */
        unsigned int spi_busy       : 1; /* [31] */
    } bits;
    /* Define an unsigned member */
    unsigned int spi_rw_u32;
} U_SPI_RW;

#define SPI_WRITE		(0)
#define SPI_READ		(1)


/* RTC REG */
#define RTC_10MS_COUN	0x00
#define RTC_S_COUNT  	0x01
#define RTC_M_COUNT  	0x02
#define RTC_H_COUNT  	0x03
#define RTC_D_COUNT_L	0x04
#define RTC_D_COUNT_H	0x05


#define RTC_LR_10MS		0x0C
#define RTC_LR_S		0x0D
#define RTC_LR_M		0x0E
#define RTC_LR_H		0x0F
#define RTC_LR_D_L		0x10
#define RTC_LR_D_H		0x11

#define RTC_LORD		0x12


struct hirtc_driver {
    struct rtc_device *rtc;
    spinlock_t lock;
};

struct hirtc_driver hirtc;

static int spi_rtc_write(char reg, char val)
{
	U_SPI_RW w_data, r_data;

	r_data.spi_rw_u32 = 0;
	w_data.spi_rw_u32 = 0;
	w_data.bits.spi_wdata = val;
	w_data.bits.spi_addr = reg;
	w_data.bits.spi_rw = SPI_WRITE;
	w_data.bits.spi_start = 0x1;
 	spin_lock(&hirtc.lock);
	writel(w_data.spi_rw_u32, SPI_RW);
	do {
		r_data.spi_rw_u32 = readl(SPI_RW);
	} while (r_data.bits.spi_busy);
	spin_unlock(&hirtc.lock);

	return 0;
}

static int spi_rtc_read(char reg, char *val)
{
	U_SPI_RW w_data, r_data;

	r_data.spi_rw_u32 = 0;
	w_data.spi_rw_u32 = 0;
	w_data.bits.spi_addr = (char) reg;
	w_data.bits.spi_rw = (char) SPI_READ;
	w_data.bits.spi_start = 0x1;

	spin_lock(&hirtc.lock);
	writel(w_data.spi_rw_u32, SPI_RW);
	do {
		r_data.spi_rw_u32 = readl(SPI_RW);
	} while (r_data.bits.spi_busy);
	spin_unlock(&hirtc.lock);

	*val = r_data.bits.spi_rdata;

	return 0;
}

static int hirtc_read_time(struct device *dev, struct rtc_time *tm)
{
	unsigned char dayl, dayh;
	unsigned char second, minute, hour;
	unsigned long seconds = 0;
	unsigned int day;

	spi_rtc_read(RTC_S_COUNT, &second);
	spi_rtc_read(RTC_M_COUNT, &minute);
	spi_rtc_read(RTC_H_COUNT, &hour);
	spi_rtc_read(RTC_D_COUNT_L, &dayl);
	spi_rtc_read(RTC_D_COUNT_H, &dayh);
	day = (dayl | (dayh << 8));
	seconds = second + minute*60 + hour*60*60 + day*24*60*60;

	rtc_time_to_tm(seconds, tm);

    return rtc_valid_tm(tm);

}
static int hirtc_set_time(struct device *dev, struct rtc_time *tm)
{
	unsigned char ret;
	unsigned int days;
	unsigned long seconds = 0;

	if (rtc_valid_tm(tm) < 0)
    {
        return -EINVAL;
    }

	rtc_tm_to_time(tm, &seconds);
	days = seconds/(60*60*24);

	do {
		spi_rtc_read(RTC_LORD, &ret);
		msleep(1);
	} while (ret);
	spi_rtc_write(RTC_LR_10MS, 0);
	spi_rtc_write(RTC_LR_S, tm->tm_sec);
	spi_rtc_write(RTC_LR_M, tm->tm_min);
	spi_rtc_write(RTC_LR_H, tm->tm_hour);
	spi_rtc_write(RTC_LR_D_L, (days & 0xFF));
	spi_rtc_write(RTC_LR_D_H, (days >> 8));

	spi_rtc_write(RTC_LORD, 1);
    msleep(1);
    return 0;
}

static const struct rtc_class_ops hirtc_ops = {
	.read_time = hirtc_read_time,
	.set_time = hirtc_set_time,
};

static struct platform_device *hirtc_device;

void rtc_init_clk(void)
{
	spin_lock(&hirtc.lock);
	writel(0x4, SPI_CLK_DIV);
	spin_unlock(&hirtc.lock);
}
static int __init hirtc_init(void)
{
    int ret = 0;

    hirtc_device = platform_device_alloc("hisi_rtc", -1);

    ret = platform_device_add(hirtc_device);
    if (ret){
        goto INIT_ERR;
    }
    hirtc.rtc = rtc_device_register("hisi_rtc", &hirtc_device->dev,
            &hirtc_ops, THIS_MODULE);
    if (!hirtc.rtc){
        ret = -EIO;
        goto INIT_ERR;
    }
    spin_lock_init(&hirtc.lock);
    rtc_init_clk();
INIT_ERR:
    platform_device_put(hirtc_device);
    return ret;
}

static void __exit hirtc_exit(void)
{
    rtc_device_unregister(hirtc.rtc);
    platform_device_unregister(hirtc_device);
}

module_init(hirtc_init);
module_exit(hirtc_exit);

MODULE_LICENSE("GPL");
