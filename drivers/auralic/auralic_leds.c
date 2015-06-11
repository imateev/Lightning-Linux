
#include <linux/init.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/mutex.h>
#include <linux/math64.h>
#include <linux/slab.h>
#include <linux/sched.h>
#include <linux/timer.h>

/* proc filesystem */
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/list.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/mm.h>

//#define  AURALIC_LED_DEBUG

#define LED_PROC_NAME       "ledinfo"
#define LED_CLASS_NAME      "led"
#define WHITE_LED_GPIO      (1*32 + 4)  /* GPIO2_04 MX6QDL_PAD_NANDF_D4 */
#define AMBER_LED_GPIO      (1*32 + 5)  /* GPIO2_05 MX6QDL_PAD_NANDF_D5 */
//#define TEST_LED_GPIO       (4*32 + 20) /* GPIO5_20 MX6QDL_PAD_CSI0_DATA_EN*/

#define LED_BLK_ON_TIME      10*(HZ/10)  /* 10 x 100ms */
#define LED_BLK_OFF_TIME     10*(HZ/10)  /* 10 x 100ms */

#define HELP_STR       "echo wledon    > /proc/ledinfo ---- white led on\n \
echo wledoff   > /proc/ledinfo ---- white led off\n \
echo wledblink > /proc/ledinfo ---- white led blink\n \
echo aledon    > /proc/ledinfo ---- white led on\n \
echo aledoff   > /proc/ledinfo ---- white led off\n \
echo aledblink > /proc/ledinfo ---- white led blink\n"

unsigned blink_led = 0;   // the led gpio now in blinking
struct timer_list led_blink_timer;

enum led_event_code
{
    LED_ON = 1,
    LED_OFF,
    LED_BLK
};

ssize_t ledproc_read(struct file *filp, char __user *usrbuf, size_t size, loff_t *offset)
{
    return 0;
}

static void led_blink_timeout(unsigned long ptr)
{   
    if(ptr)
    {
        led_blink_timer.data = (ulong) 0;
	    led_blink_timer.expires = jiffies +  LED_BLK_OFF_TIME;
    }
    else
    {
        led_blink_timer.data = (ulong) 1;
	    led_blink_timer.expires = jiffies +  LED_BLK_ON_TIME;
    }

    if(blink_led)
    {
    #ifdef AURALIC_LED_DEBUG
        printk("led [%s] blink [%s]  time:%lu\n", 
        blink_led == WHITE_LED_GPIO ? "white":"amber",
        led_blink_timer.data == 1 ? "on":"off",
        led_blink_timer.expires - jiffies);
    #endif
        gpio_set_value(blink_led, led_blink_timer.data);
        add_timer(&led_blink_timer);
    }
}


static void led_event_process(unsigned gpio, char led_event)
{

#ifdef AURALIC_LED_DEBUG
    printk("receive led event: %s   %s\n", 
            WHITE_LED_GPIO == gpio ? "white":"amber",
            LED_ON == led_event ? "ON" :
            LED_OFF == led_event ? "OFF":"BLINK");
#endif

    if(LED_BLK == led_event)
    {
        if(blink_led != gpio)// not the same led now in blinking
        {
            if(blink_led)
            {
                del_timer_sync(&led_blink_timer);
            }
            gpio_set_value(AMBER_LED_GPIO, 0);
            gpio_set_value(WHITE_LED_GPIO, 0);
            
            blink_led = gpio;
    	    led_blink_timer.expires = jiffies +  LED_BLK_ON_TIME;
    	    add_timer(&led_blink_timer);
	    }
    }
    else
    {
        if(blink_led)
        {
            blink_led = 0;
            del_timer_sync(&led_blink_timer);
        }
        
        gpio_set_value(AMBER_LED_GPIO, 0);
        gpio_set_value(WHITE_LED_GPIO, 0); 
        if(LED_ON == led_event)
            gpio_set_value(gpio, 1);
        else
            gpio_set_value(gpio, 0);
    }
}
 

static ssize_t ledproc_write(struct file *filp, const char __user *usr_buf,
                             size_t count, loff_t *f_pos)
{
    char len;
    char buff[100] = {0};

    len = count < 100 ? count : 99;
    if(0 != copy_from_user(buff, usr_buf, len))
    {
        goto out;
    }
    
    buff[99] = '\0';

    if(sizeof("wledon") == count && 0 == strncmp(buff, "wledon", 6))
    {
        led_event_process(WHITE_LED_GPIO, LED_ON);
    }
    else if(sizeof("wledoff") == count && 0 == strncmp(buff, "wledoff", 7))
    {
        led_event_process(WHITE_LED_GPIO, LED_OFF);
    }
    else if(sizeof("wledblink") == count && 0 == strncmp(buff, "wledblink", 9))
    {
        led_event_process(WHITE_LED_GPIO, LED_BLK);
    }
    else if(sizeof("aledon") == count && 0 == strncmp(buff, "aledon", 6))
    {
        led_event_process(AMBER_LED_GPIO, LED_ON);
    }
    else if(sizeof("aledoff") == count && 0 == strncmp(buff, "aledoff", 7))
    {
        led_event_process(AMBER_LED_GPIO, LED_OFF);
    }
    else if(sizeof("aledblink") == count && 0 == strncmp(buff, "aledblink", 9))
    {
        led_event_process(AMBER_LED_GPIO, LED_BLK);
    }
    else
    {
        printk("%s", HELP_STR);
    }
    
out:    
    return count;
}


static const struct  file_operations ledproc_op = {
    .read = ledproc_read,
    .write = ledproc_write,
};

static int __init led_init(void)
{    
    printk("enter auralic led module!\n");
    
    if(0 > gpio_request(WHITE_LED_GPIO, "white_led_gpio\n")) // gpio2_4
    {
        printk("Err: request GPIO for white led failed!\n");
        return -1;
    }

    if(0 > gpio_request(AMBER_LED_GPIO, "amber_led_gpio\n")) // gpio2_5
    {
        printk("Err: request GPIO for amber led failed!\n");
        return -1;
    }
    
    /* create proc file /proc/PROC_ISP_NAME */
    if (NULL == proc_create(LED_PROC_NAME, 0755, NULL, &ledproc_op))    
    {
        printk("Err: create /proc/%s failed!", LED_PROC_NAME);
        gpio_free(WHITE_LED_GPIO);
        gpio_free(AMBER_LED_GPIO);
        return -1;
    }

    init_timer(&led_blink_timer);
    led_blink_timer.function = &led_blink_timeout;
	led_blink_timer.data = (ulong) 0;
	
    gpio_direction_output(WHITE_LED_GPIO, 0);
    gpio_direction_output(AMBER_LED_GPIO, 0);
    gpio_set_value(WHITE_LED_GPIO, 0); // white led off
    gpio_set_value(AMBER_LED_GPIO, 1); // amber led on
	return 0;
}

static void __exit led_exit(void)
{
	printk("exit auralic led module!\n");
	remove_proc_entry(LED_PROC_NAME, NULL);
    gpio_free(WHITE_LED_GPIO);
    gpio_free(AMBER_LED_GPIO);
}

module_init(led_init);
module_exit(led_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("hu yongfa 2015-04-23");
MODULE_DESCRIPTION("led driver for auralic");
