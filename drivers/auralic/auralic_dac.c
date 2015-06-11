/*
 * Driver for EETI eGalax Multiple Touch Controller
 *
 * Copyright (C) 2011-2013 Freescale Semiconductor, Inc.
 *
 * based on max11801_ts.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* EETI eGalax serial touch screen controller is a I2C based multiple
 * touch screen controller, it supports 5 point multiple touch. */

/* TODO:
  - auto idle mode support
*/

#include <linux/module.h>
#include <linux/init.h>
#include <linux/i2c.h>
#include <linux/interrupt.h>
#include <linux/input.h>
#include <linux/irq.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/input/mt.h>
#include <linux/of_gpio.h>
#include <linux/kthread.h>

/* proc filesystem */
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/list.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/mm.h>


#define     DAC_PROC_NAME   "dac"
#define     RETRY_COUNT     3
#define     DAC_RESET_GPIO  (3*32 + 10)//KEY_COL2_GPIO4_10
#define     VOL_R_REG       15 //VOLUME 1
#define     VOL_L_REG       16 //VOLUME 2

struct i2c_client *dac_dev = NULL;

char vol_l_cur = 0xff, vol_r_cur =0xff;//

static struct task_struct *dactask = NULL;


ssize_t dacproc_read(struct file *filp, char __user *usrbuf, size_t size, loff_t *offset)
{
    return 0;
}

/*
0（静音）	-127.5	    --
1至10	    -78至-60	2dB
11至40	    -59至-30	1dB
41至100	    -29.5至0	0.5dB
*/

bool user_value_to_hex(char value, char *hex)
{
    bool ret = true;
    char tail = 0;

    tail = 0;
    if(0 == value)
    {
        *hex = 255;
    }
    else if(10 >= value) // 1 -- 10
    {
        *hex = 160 - value * 4;// 2 * (80 - value * 2)
    }
    else if(40 >= value) // 11 -- 40
    {
        *hex = 140 - value * 2;// 2 * (70 - value * 1)
    }
    else if(100 >= value) // 41 -- 100
    {
        *hex = 100 - value; // 2 * (50 - value*0.5)
    }
    else
    {
        ret = false;
    }
        
    return ret;
}

void dac_write_and_verify(char reg, char value, char retry)
{
    char cnt = 0;
    char buff[2];
    char tmp;
    int ret;
    
    while(cnt++ < retry)
    {
        buff[0] = reg;
        buff[1] = value;
        ret = i2c_master_send(dac_dev, buff, 2);// select register            

        // read back
        reg = reg;
        ret = i2c_master_send(dac_dev, &reg, 1);// select register
        ret = i2c_master_recv(dac_dev, &tmp, 1);// read value from register
        if(tmp == value)
        {
            printk(KERN_DEBUG"dac set reg[%02d] with 0x%02x success, try %d!\n", reg, value, cnt);
            break;
        }
            
        printk(KERN_DEBUG"dac set reg[%02d] with 0x%02x failed, try %d!\n", reg, value, cnt);
    }
}

static ssize_t dacproc_write(struct file *filp, const char __user *usr_buf,
                             size_t count, loff_t *f_pos)
{
    char len;
    char buff[100] = {0};
    char cmd[20];
    int value;
    int ret;

    len = count < 100 ? count : 99;
    if(0 != copy_from_user(buff, usr_buf, len))
    {
        goto out;
    }
    
    buff[99] = '\0';
    
    ret = sscanf(buff, "%s %d", cmd, &value);
    if(2 > ret)
    {
        printk("dac invalide command\n");
        goto out;
    }
    
    printk(KERN_DEBUG"dac  cmd:%s  value:%d\n", cmd, value);
    
    if(0 == strncmp(cmd, "vol_l", 5))
    {
        
        if(true == user_value_to_hex(value, &vol_l_cur))
        {
                dac_write_and_verify(VOL_L_REG, vol_l_cur, RETRY_COUNT);
        }
        else
        {
                printk(KERN_DEBUG"invalid volume left value:%d\n", value);
        }
    }
    else if(0 == strncmp(cmd, "vol_r", 5))
    {
        if(true == user_value_to_hex(value, &vol_r_cur))
        {
                dac_write_and_verify(VOL_R_REG, vol_r_cur, RETRY_COUNT);
        }
        else
        {
                printk(KERN_DEBUG"invalid volume right value:%d\n", value);
        }
    }
    
out:    

    return count;
}


static const struct  file_operations dacproc_op = {
    .read  = dacproc_read,
    .write = dacproc_write,
};


bool dac_check_reseted(void)
{
    char reg = 11;
    char value = 0;

    if(NULL == dac_dev)
    {
        return false;
    }
    
    i2c_master_send(dac_dev, &reg, 1);// select register
    i2c_master_recv(dac_dev, &value, 1);// read value from register
    if(0x0d == value)
        return false;
    else
        return true;
}

void init_dac_es9018k2m(void)
{    
    gpio_direction_output(DAC_RESET_GPIO, 0);
    gpio_set_value(DAC_RESET_GPIO, 0);
    mdelay(100);
    gpio_set_value(DAC_RESET_GPIO, 1);
    mdelay(20);
    
    //dac_write_and_verify(1, 0x83, RETRY_COUNT);
    
    dac_write_and_verify(6, 0x4f, RETRY_COUNT);
    dac_write_and_verify(7, 0xc0, RETRY_COUNT);
    dac_write_and_verify(11, 0x0d, RETRY_COUNT);
    
    dac_write_and_verify(VOL_L_REG, vol_l_cur, RETRY_COUNT);
    dac_write_and_verify(VOL_R_REG, vol_r_cur, RETRY_COUNT);
}


int dac_reset_detect_fn(void *data)
{
    printk(KERN_DEBUG"start dac_reset_detect_fn\n");
    
    while(!kthread_should_stop())
    {
        set_current_state(TASK_INTERRUPTIBLE);
        schedule_timeout(HZ);
        if(true == dac_check_reseted())
        {
            printk("dac detect reset, re-init it now!");
            init_dac_es9018k2m();
        }
        //printk(KERN_DEBUG"dac_reset_detect_fn wakeup\n");
    }

    printk(KERN_DEBUG"leave dac_reset_detect_fn\n");
    
    return 0;
}


static int auralic_dac_probe(struct i2c_client *client,
				       const struct i2c_device_id *id)
{
    
    if(0 > gpio_request(DAC_RESET_GPIO, "DAC reset gpio\n")) // gpio2_4
    {
        printk("request DAC reset gpio failed\n");
        return -1;
    }    
    
    /* create proc file /proc/PROC_ISP_NAME */
    if (NULL == proc_create(DAC_PROC_NAME, 0755, NULL, &dacproc_op))    
    {
        return -1;
    }

    printk(KERN_DEBUG"dac addr:0x%x\n", client->addr);
    
    dac_dev = client;
    init_dac_es9018k2m();
    
    dactask = kthread_run(dac_reset_detect_fn, NULL, "dactask");
    if (IS_ERR(dactask))
    {
        dactask = NULL;
        pr_err("create dactask thread failed!\n");
        return -ENOMEM;
    }
    
    return 0;
}

static int auralic_dac_remove(struct i2c_client *client)
{
    if(NULL != dactask)
    {
        kthread_stop(dactask);
        dactask = NULL;
    }
    remove_proc_entry(DAC_PROC_NAME, NULL);
    gpio_free(DAC_RESET_GPIO);
    printk(KERN_DEBUG"leave dac module!\n");
	return 0;
}

static const struct i2c_device_id auralic_dac_id[] = {
	{ "auralic", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, auralic_dac_id);

#ifdef CONFIG_PM_SLEEP
static int auralic_suspend(struct device *dev)
{
	return 0;
}

static int auralic_resume(struct device *dev)
{
	return 0;
}
#endif

static SIMPLE_DEV_PM_OPS(auralic_pm_ops, auralic_suspend, auralic_resume);

static struct of_device_id auralic_dac_dt_ids[] = {
	{ .compatible = "fsl,auralic" },
	{ /* sentinel */ }
};

static struct i2c_driver auralic_dac_driver = {
	.driver = {
		.name	= "auralic_iic",
		.owner	= THIS_MODULE,
		.pm	    = &auralic_pm_ops,
		.of_match_table	= of_match_ptr(auralic_dac_dt_ids),
	},
	.id_table	= auralic_dac_id,
	.probe		= auralic_dac_probe,
	.remove		= auralic_dac_remove,
};
 
module_i2c_driver(auralic_dac_driver);

MODULE_AUTHOR("AURALiC LIMTED.");
MODULE_DESCRIPTION("coder driver for ES9018K2M");
MODULE_LICENSE("GPL");

