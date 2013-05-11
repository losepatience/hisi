
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
#include <linux/types.h>
#include <linux/timer.h>
#include <linux/miscdevice.h>
#include <linux/fs.h>
#include <linux/init.h>
#include <asm/uaccess.h>
#include <asm/io.h>
#include <linux/kthread.h>
#include <linux/string.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/proc_fs.h>


/* ==========================================================================*/
#define ADC_IRQ            19
#define SUPPORT_CHANNEL    0x2

#define SAR_ADC_BASE       0x200b0000
#define PERI_CRG32_BASE    0x20030080

#define ADC_STATUS         0x00
#define ADC_CTRL           0x04
#define ADC_POWER          0x08
#define ADC_INT_STATUS     0x0c
#define ADC_INT_MASK       0x10
#define ADC_INT_CLR        0x14
#define ADC_INT_RAW        0x18
#define ADC_RESULT         0x1c

#define ADC_START          0x01
#define ADC_CHANNEL        (0x01 << 16)
#define ADC_IRQ_ENABLE     0x00
#define ADC_IRQ_DISABLE    0x01
#define ADC_CLK_ENABLE     0x02
#define ADC_POWER_ENABLE   0x00


#define TIME_OUT           HZ * 5    // 队列等待时间
/* ==========================================================================*/
struct his_adc_driver{
    struct proc_dir_entry  *adc_file;
    void __iomem           *sar_adc_reg_base;
    int                    flag;
    wait_queue_head_t      irq_wait;
};

static char *adc_proc_name = "sar_adc";
static struct his_adc_driver his_adc;

/* ==========================================================================*/
static int read_channel(int channel)
{
    unsigned int AdcCtrlreg = 0;

    //--------------------------------
    // enable the interrupt
    //--------------------------------
    writel(ADC_IRQ_ENABLE, his_adc.sar_adc_reg_base + ADC_INT_MASK);

    //--------------------------------
    // choose the channel
    //--------------------------------
    AdcCtrlreg = readl(his_adc.sar_adc_reg_base + ADC_CTRL);

    if (1 == channel){
        AdcCtrlreg = ADC_CHANNEL|AdcCtrlreg;

    }else if (0 == channel){
        AdcCtrlreg = ~(ADC_CHANNEL) & AdcCtrlreg;
    }
    writel(AdcCtrlreg, his_adc.sar_adc_reg_base + ADC_CTRL);

    //--------------------------------
    // start convert
    //--------------------------------
    AdcCtrlreg = readl(his_adc.sar_adc_reg_base + ADC_CTRL);
    AdcCtrlreg = ADC_START|AdcCtrlreg ;
    writel(AdcCtrlreg, his_adc.sar_adc_reg_base + ADC_CTRL);

    wait_event_timeout(his_adc.irq_wait, his_adc.flag, TIME_OUT);
    if (0 == his_adc.flag)
        return -EFAULT;
    his_adc.flag = 0;

    //--------------------------------
    // enable the interrupt
    //--------------------------------
    writel(ADC_IRQ_DISABLE, his_adc.sar_adc_reg_base + ADC_INT_MASK);

    //--------------------------------
    // get the result
    //--------------------------------
    return readl(his_adc.sar_adc_reg_base + ADC_RESULT);
}

static int hisi_adc_proc_read(char *page, char **start,
	off_t off, int count, int *eof, void *data)
{
    int len = 0;
    int i;
    for (i = 0; i < SUPPORT_CHANNEL; i++)
    {
        page[i] = read_channel(i);
        len += sizeof(page);
        udelay(1);
    }
    *eof = 1;
    return len;
}

static irqreturn_t sar_adc_interrupt(int irq, void *id)
{
    //--------------------------------
    // clear the interrupt
    //--------------------------------
    writel(0x01, his_adc.sar_adc_reg_base + ADC_INT_CLR);
    his_adc.flag = 1;
    wake_up(&his_adc.irq_wait);

    return 0;
}
int hisi_init_adc(void)
{
	int retval = 0;

    his_adc.adc_file = create_proc_read_entry(adc_proc_name, 0, NULL,
            hisi_adc_proc_read, NULL);
	if (his_adc.adc_file == NULL) {
		pr_warning("%s: %s fail!\n", __func__, adc_proc_name);
		return -ENOMEM;
	}

    retval = request_irq(ADC_IRQ, sar_adc_interrupt, 0, "SAR_ADC", NULL);
    if(0 != retval){
        pr_warning("hi3518 ADC: failed to register IRQ(%d)\n", retval);
        goto ADC_INIT_FAIL1;
    }

    his_adc.sar_adc_reg_base = ioremap_nocache((unsigned long)SAR_ADC_BASE, 0x20);
    if (NULL == his_adc.sar_adc_reg_base){
        pr_warning("function %s line %u failed\n",
                __FUNCTION__, __LINE__);
        retval = -EFAULT;
        goto ADC_INIT_FAIL2;
    }

    //--------------------------------
    // power ADC function
    //--------------------------------
    writel(ADC_POWER_ENABLE, his_adc.sar_adc_reg_base + ADC_POWER);

    //----------------------------------
    // enable CLK and cancell soft reset
    //----------------------------------
    writel(ADC_CLK_ENABLE, IO_ADDRESS(PERI_CRG32_BASE));


    his_adc.flag = 0;
    init_waitqueue_head(&his_adc.irq_wait);
    return 0;

ADC_INIT_FAIL2:
    free_irq(ADC_IRQ, NULL);
ADC_INIT_FAIL1:
    remove_proc_entry(adc_proc_name, NULL);
	return retval;
}
void hisi_exit_adc(void)
{
    free_irq(ADC_IRQ, NULL);
    iounmap(his_adc.sar_adc_reg_base);
    remove_proc_entry(adc_proc_name, NULL);
}

module_init(hisi_init_adc);
module_exit(hisi_exit_adc);
MODULE_LICENSE("Dual BSD/GPL");
