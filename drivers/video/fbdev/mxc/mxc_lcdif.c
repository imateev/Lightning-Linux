/*
 * Copyright (C) 2011-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#include <linux/init.h>
#include <linux/ipu.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mxcfb.h>
#include <linux/of_device.h>
#include <linux/pinctrl/consumer.h>
#include <linux/platform_device.h>

#include "mxc_dispdrv.h"

struct mxc_lcd_platform_data {
	u32 default_ifmt;
	u32 ipu_id;
	u32 disp_id;
};

struct mxc_lcdif_data {
	struct platform_device *pdev;
	struct mxc_dispdrv_handle *disp_lcdif;
};

#define DISPDRV_LCD	"lcd"

static struct fb_videomode lcdif_modedb[] = {
	{
	/* 800x480 @ 57 Hz , pixel clk @ 27MHz */
	"CLAA-WVGA", 57, 800, 480, 37037, 40, 60, 10, 10, 20, 10,
	FB_SYNC_CLK_LAT_FALL,
	FB_VMODE_NONINTERLACED,
	0,},
	{
	/* 800x480 @ 60 Hz , pixel clk @ 32MHz */
	"AURALiC", 60, 480, 800, 29850, 89, 164, 23, 10, 10, 10,
	FB_SYNC_OE_LOW_ACT,
	FB_VMODE_NONINTERLACED,
	0,},
};
static int lcdif_modedb_sz = ARRAY_SIZE(lcdif_modedb);

static int lcdif_init(struct mxc_dispdrv_handle *disp,
	struct mxc_dispdrv_setting *setting)
{
	int ret, i;
	struct mxc_lcdif_data *lcdif = mxc_dispdrv_getdata(disp);
	struct device *dev = &lcdif->pdev->dev;
	struct mxc_lcd_platform_data *plat_data = dev->platform_data;
	struct fb_videomode *modedb = lcdif_modedb;
	int modedb_sz = lcdif_modedb_sz;

	/* use platform defined ipu/di */
	ret = ipu_di_to_crtc(dev, plat_data->ipu_id,
			     plat_data->disp_id, &setting->crtc);
	if (ret < 0)
		return ret;

	ret = fb_find_mode(&setting->fbi->var, setting->fbi, setting->dft_mode_str,
				modedb, modedb_sz, NULL, setting->default_bpp);
	if (!ret) {
		fb_videomode_to_var(&setting->fbi->var, &modedb[0]);
		setting->if_fmt = plat_data->default_ifmt;
	}

	INIT_LIST_HEAD(&setting->fbi->modelist);
	for (i = 0; i < modedb_sz; i++) {
		struct fb_videomode m;
		fb_var_to_videomode(&m, &setting->fbi->var);
		if (fb_mode_is_equal(&m, &modedb[i])) {
			fb_add_videomode(&modedb[i],
					&setting->fbi->modelist);
			break;
		}
	}

	return ret;
}

void lcdif_deinit(struct mxc_dispdrv_handle *disp)
{
	/*TODO*/
}

static struct mxc_dispdrv_driver lcdif_drv = {
	.name 	= DISPDRV_LCD,
	.init 	= lcdif_init,
	.deinit	= lcdif_deinit,
};

static int lcd_get_of_property(struct platform_device *pdev,
				struct mxc_lcd_platform_data *plat_data)
{
	struct device_node *np = pdev->dev.of_node;
	int err;
	u32 ipu_id, disp_id;
	const char *default_ifmt;

	err = of_property_read_string(np, "default_ifmt", &default_ifmt);
	if (err) {
		dev_dbg(&pdev->dev, "get of property default_ifmt fail\n");
		return err;
	}
	err = of_property_read_u32(np, "ipu_id", &ipu_id);
	if (err) {
		dev_dbg(&pdev->dev, "get of property ipu_id fail\n");
		return err;
	}
	err = of_property_read_u32(np, "disp_id", &disp_id);
	if (err) {
		dev_dbg(&pdev->dev, "get of property disp_id fail\n");
		return err;
	}

	plat_data->ipu_id = ipu_id;
	plat_data->disp_id = disp_id;
	if (!strncmp(default_ifmt, "RGB24", 5))
		plat_data->default_ifmt = IPU_PIX_FMT_RGB24;
	else if (!strncmp(default_ifmt, "BGR24", 5))
		plat_data->default_ifmt = IPU_PIX_FMT_BGR24;
	else if (!strncmp(default_ifmt, "GBR24", 5))
		plat_data->default_ifmt = IPU_PIX_FMT_GBR24;
	else if (!strncmp(default_ifmt, "RGB565", 6))
		plat_data->default_ifmt = IPU_PIX_FMT_RGB565;
	else if (!strncmp(default_ifmt, "RGB666", 6))
		plat_data->default_ifmt = IPU_PIX_FMT_RGB666;
	else if (!strncmp(default_ifmt, "YUV444", 6))
		plat_data->default_ifmt = IPU_PIX_FMT_YUV444;
	else if (!strncmp(default_ifmt, "LVDS666", 7))
		plat_data->default_ifmt = IPU_PIX_FMT_LVDS666;
	else if (!strncmp(default_ifmt, "YUYV16", 6))
		plat_data->default_ifmt = IPU_PIX_FMT_YUYV;
	else if (!strncmp(default_ifmt, "UYVY16", 6))
		plat_data->default_ifmt = IPU_PIX_FMT_UYVY;
	else if (!strncmp(default_ifmt, "YVYU16", 6))
		plat_data->default_ifmt = IPU_PIX_FMT_YVYU;
	else if (!strncmp(default_ifmt, "VYUY16", 6))
				plat_data->default_ifmt = IPU_PIX_FMT_VYUY;
	else {
		dev_err(&pdev->dev, "err default_ifmt!\n");
		return -ENOENT;
	}

	return err;
}


#define CONFIG_AURALIC_LCD		1
#if defined(CONFIG_AURALIC_LCD)
/* proc filesystem */
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <linux/uio.h>
#include <asm/uaccess.h>
#include <linux/list.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/mm.h>
#include <linux/timer.h>
#include <linux/kthread.h>
#include <linux/cdev.h>  
#include <linux/device.h> 

#define	IONUMER(x, y)	((x-1)*32 + y)

#define LCD_PROC_NAME       "lcd"
#define	lcd_reset_pin		IONUMER(4, 13)//
#define	spi_clk_pin			IONUMER(2, 23)
#define	spi_cs_pin			IONUMER(2, 27)
#define	spi_mosi_pin		IONUMER(2, 24)
#define	spi_miso_pin		IONUMER(2, 25)

#define lcd_reset(x)	\
do \
{ \
	gpio_set_value(lcd_reset_pin, x); \
}while(0)

#define spi_cs(x)	\
do \
{ \
	gpio_set_value(spi_cs_pin, x); \
}while(0)

#define spi_clk(x)	\
do \
{ \
	gpio_set_value(spi_clk_pin, x); \
	udelay(10); \
}while(0)

#define spi_mosi(x)	\
do \
{ \
	gpio_set_value(spi_mosi_pin, x); \
	udelay(10); \
}while(0)

#define spi_miso(x)	\
do \
{ \
	gpio_set_value(spi_miso_pin, x); \
}while(0)
	
void spi_cmd(u8 cmd)
{
	u8 i;
	
	//spi_cs(0);
	spi_clk(0);
	spi_mosi(0); //cmd
	spi_clk(1);
	
	for(i=0;i<8;i++)
	{
		spi_clk(0);   
		if(cmd&0x80)
		{
			spi_mosi(1);
		}
		else
		{
			spi_mosi(0);
		}
		spi_clk(1); 
		cmd = cmd<<1;
	}
	//spi_cs(1);
}

void spi_data(u8 data)
{
	u8 i;
	
	//spi_cs(0);
	spi_clk(0);
	spi_mosi(1); //data
	spi_clk(1);
	
	for(i=0;i<8;i++)
	{
		spi_clk(0);   
		if(data&0x80)
		{
			spi_mosi(1);
		}
		else
		{
			spi_mosi(0);
		}
		spi_clk(1); 
		data = data<<1;
	}
	//spi_cs(1);
}

void lcd_spi_init(void) 
{  
	//// Reset LCD Driver////
	lcd_reset(1);
	mdelay(110); // Delay 1ms
	lcd_reset(0);
	mdelay(150); // Delay 10ms // This Delay time is necessary
	lcd_reset(1);
	mdelay(100); // Delay 50 ms
	spi_cs(0);
	mdelay(1);

	//************* Start Initial Sequence **********//
	spi_cmd(0xFF); // EXTC Command Set enable register
	spi_data(0xFF);
	spi_data(0x98);
	spi_data(0x06);

	spi_cmd(0xBA); // SPI Interface Setting
	spi_data(0x60);

	spi_cmd(0xBC); // GIP 1
	spi_data(0x01);
	spi_data(0x0E);
	spi_data(0x61);
	spi_data(0xFB);
	spi_data(0x10);
	spi_data(0x10);
	spi_data(0x0B);
	spi_data(0x0F);
	spi_data(0x2E);
	spi_data(0x73);
	spi_data(0xFF);
	spi_data(0xFF);
	spi_data(0x0E);
	spi_data(0x0E);
	spi_data(0x00);

	spi_data(0x03);
	spi_data(0x66);
	spi_data(0x63);
	spi_data(0x01);
	spi_data(0x00);
	spi_data(0x00);

	spi_cmd(0xBD); // GIP 2
	spi_data(0x01);
	spi_data(0x23);
	spi_data(0x45);
	spi_data(0x67);
	spi_data(0x01);
	spi_data(0x23);
	spi_data(0x45);
	spi_data(0x67);
	spi_cmd(0xBE); // GIP 3
	spi_data(0x00);
	spi_data(0x21);
	spi_data(0xAB);
	spi_data(0x60);
	spi_data(0x22);
	spi_data(0x22);
	spi_data(0x22);
	spi_data(0x22);
	spi_data(0x22);
	spi_cmd(0xC7); // Vcom
	spi_data(0x47);

	spi_cmd(0xED); // EN_volt_reg
	spi_data(0x7F);
	spi_data(0x0F);
	spi_data(0x00);

	spi_cmd(0xB6); // Display Function Control
	spi_data(0x20);    //02

	spi_cmd(0xC0); // Power Control 1
	spi_data(0x37);
	spi_data(0x0B);
	spi_data(0x0A);
	spi_cmd(0xFC); // LVGL
	spi_data(0x0A);
	spi_cmd(0xDF); // Engineering Setting
	spi_data(0x00);
	spi_data(0x00);
	spi_data(0x00);
	spi_data(0x00);
	spi_data(0x00);
	spi_data(0x20);
	spi_cmd(0xF3); // DVDD Voltage Setting
	spi_data(0x74);
	spi_cmd(0xB4); // Display Inversion Control
	spi_data(0x00);
	spi_data(0x00);
	spi_data(0x00);
	spi_cmd(0xF7); // 480x800
	spi_data(0x82);
	spi_cmd(0xB1); // Frame Rate
	spi_data(0x00);
	spi_data(0x12);
	spi_data(0x13);
	spi_cmd(0xF2); // CR/EQ/PC
	spi_data(0x80);
	spi_data(0x5B);
	spi_data(0x40);
	spi_data(0x28);


	spi_cmd(0xC1); // Power Control 2
	spi_data(0x07);
	spi_data(0x9F);
	spi_data(0x71);
	spi_data(0x20);
	spi_cmd(0xE0); //Gamma
	spi_data(0x00); //P1
	spi_data(0x11); //P2
	spi_data(0x18); //P3
	spi_data(0x0C); //P4
	spi_data(0x0F); //P5
	spi_data(0x0D); //P6
	spi_data(0x09); //P7
	spi_data(0x08); //P8
	spi_data(0x02); //P9
	spi_data(0x06); //P10
	spi_data(0x0F); //P11
	spi_data(0x0E); //P12
	spi_data(0x10); //P13
	spi_data(0x18); //P14
	spi_data(0x14); //P15
	spi_data(0x00); //P16
	spi_cmd(0xE1); //Gamma
	spi_data(0x00); //P1
	spi_data(0x05); //P2
	spi_data(0x0D); //P3
	spi_data(0x0B); //P4
	spi_data(0x0D); //P5
	spi_data(0x0B); //P6
	spi_data(0x05); //P7
	spi_data(0x03); //P8
	spi_data(0x09); //P9
	spi_data(0x0D); //P10
	spi_data(0x0C); //P11
	spi_data(0x10); //P12
	spi_data(0x0B); //P13
	spi_data(0x13); //P14


	spi_data(0x09); //P15
	spi_data(0x00); //P16
	spi_cmd(0x35); //Tearing Effect ON
	spi_data(0x00);


	//spi_cmd(0x11); //Exit Sleep
	//mdelay(120);
	//spi_cmd(0x29); // Display On
	//mdelay(120);
	
	spi_cs(1);
}  

void lcd_off (void)
{
	spi_cs(0);
	spi_cmd(0x28); // Display off
	mdelay(10);
	spi_cmd(0x10); // Enter Standby mode
	mdelay(120);
	spi_cs(1);
}

void lcd_on(void)
{
	spi_cs(0);
	spi_cmd(0x11); // Standby out
	mdelay(120); 
	spi_cmd(0x29); // Display on
	spi_cs(1);
}
EXPORT_SYMBOL(lcd_on);

ssize_t lcdproc_read(struct file *filp, char __user *usrbuf, size_t size, loff_t *offset)
{
	return 0;
}

static ssize_t lcdproc_write(struct file *filp, 
				const char __user *usr_buf, size_t count, loff_t *f_pos)
{
	char len;
	char cmd[100];
    char buff[100] = {0};
	int value, param_cnt;

    len = count < 100 ? count : 99;
    if(0 != copy_from_user(buff, usr_buf, len))
    {
        return count;
    }
    
    buff[99] = '\0';

	param_cnt = sscanf(buff, "%s %d", cmd, &value);
	
	if(0 == strncmp(cmd, "on", 2))
    {
		lcd_on();
    }
    else if(0 == strncmp(cmd, "off", 3))
    {
		lcd_off();
    }
	else
	{
		printk("wrong lcd command parameter\n");
	}
	
    return count;
}

static const struct  file_operations lcdproc_op = {
    .read = lcdproc_read,
    .write = lcdproc_write,
};

int  aura_lcd_init(void)
{    
    printk("auralic lcd inited!\n");
	
	/* create proc file /proc/PROC_ISP_NAME */
    if(NULL == proc_create(LCD_PROC_NAME, 0755, NULL, &lcdproc_op))   
    {
        printk("err: create /proc/%s failed!", LCD_PROC_NAME);
        return 0;
    }

	if(0 > gpio_request(lcd_reset_pin, "lcd_reset_pin\n"))
    {
        printk("request lcd_reset_pin failed!\n");
        return 0;
    }

	if(0 > gpio_request(spi_clk_pin, "spi_clk_pin\n"))
    {
        printk("request spi_clk_pin failed!\n");
        return 0;
    }

	if(0 > gpio_request(spi_cs_pin, "spi_cs_pin\n"))
    {
        printk("request spi_cs_pin failed!\n");
        return 0;
    }

	if(0 > gpio_request(spi_mosi_pin, "spi_mosi_pin\n"))
    {
        printk("request spi_mosi_pin failed!\n");
        return 0;
    }

	if(0 > gpio_request(spi_miso_pin, "spi_miso_pin\n"))
    {
        printk("request spi_miso_pin failed!\n");
        return 0;
    }
	
	gpio_direction_output(lcd_reset_pin, 1);
	gpio_direction_output(spi_clk_pin, 1);
	gpio_direction_output(spi_cs_pin, 1);
	gpio_direction_output(spi_mosi_pin, 1);
	gpio_direction_output(spi_miso_pin, 1);

	lcd_spi_init();

	return 0;
}
#endif

static int mxc_lcdif_probe(struct platform_device *pdev)
{
	int ret;
	struct pinctrl *pinctrl;
	struct mxc_lcdif_data *lcdif;
	struct mxc_lcd_platform_data *plat_data;

	dev_dbg(&pdev->dev, "%s enter\n", __func__);
	lcdif = devm_kzalloc(&pdev->dev, sizeof(struct mxc_lcdif_data),
				GFP_KERNEL);
	if (!lcdif)
		return -ENOMEM;
	plat_data = devm_kzalloc(&pdev->dev,
				sizeof(struct mxc_lcd_platform_data),
				GFP_KERNEL);
	if (!plat_data)
		return -ENOMEM;
	pdev->dev.platform_data = plat_data;

	ret = lcd_get_of_property(pdev, plat_data);
	if (ret < 0) {
		dev_err(&pdev->dev, "get lcd of property fail\n");
		return ret;
	}

	pinctrl = devm_pinctrl_get_select_default(&pdev->dev);
	if (IS_ERR(pinctrl)) {
		dev_err(&pdev->dev, "can't get/select pinctrl\n");
		return PTR_ERR(pinctrl);
	}

	lcdif->pdev = pdev;
	lcdif->disp_lcdif = mxc_dispdrv_register(&lcdif_drv);
	mxc_dispdrv_setdata(lcdif->disp_lcdif, lcdif);

	dev_set_drvdata(&pdev->dev, lcdif);
	dev_dbg(&pdev->dev, "%s exit\n", __func__);
	
	#if defined(CONFIG_AURALIC_LCD)
	aura_lcd_init();
	#endif

	return ret;
}

static int mxc_lcdif_remove(struct platform_device *pdev)
{
	struct mxc_lcdif_data *lcdif = dev_get_drvdata(&pdev->dev);

	mxc_dispdrv_puthandle(lcdif->disp_lcdif);
	mxc_dispdrv_unregister(lcdif->disp_lcdif);
	kfree(lcdif);
	return 0;
}

static const struct of_device_id imx_lcd_dt_ids[] = {
	{ .compatible = "fsl,lcd"},
	{ /* sentinel */ }
};
static struct platform_driver mxc_lcdif_driver = {
	.driver = {
		.name = "mxc_lcdif",
		.of_match_table	= imx_lcd_dt_ids,
	},
	.probe = mxc_lcdif_probe,
	.remove = mxc_lcdif_remove,
};

static int __init mxc_lcdif_init(void)
{
	return platform_driver_register(&mxc_lcdif_driver);
}

static void __exit mxc_lcdif_exit(void)
{
	platform_driver_unregister(&mxc_lcdif_driver);
}

module_init(mxc_lcdif_init);
module_exit(mxc_lcdif_exit);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("i.MX ipuv3 LCD extern port driver");
MODULE_LICENSE("GPL");
