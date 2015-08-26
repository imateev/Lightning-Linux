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
#include <linux/notifier.h>
#include <linux/stacktrace.h>
#include <asm/io.h>


#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/spinlock.h>
#include <linux/mm.h>
#include <linux/kmsg_dump.h>
#include "auralic_panic.h"

#define  PANIC_LOG_FILE     "/home/root/kernel_panic.log"

void __iomem *logbuff = NULL;

static int aura_panic(struct notifier_block *n, unsigned long val, void *buff)
{
    int len=0;
    struct auralic_panic_head * panic = logbuff;
    
    len += setup_panic_head(panic);
    printk("=========== setup panic head success ===========\n");
    len += aura_dump_syslog_to_buff((char *)logbuff+len, AURA_PANIC_LOG_SIZE_MAX-len);
    panic->logsize = len;
    printk("=========== collect panic log sucess ===========\n");
    printk("=========== get log %d bytes in all ===========\n", len);
	return 0;
}

static struct notifier_block aura_notifier = {aura_panic, NULL, 1 };

static int __init aura_panic_init(void)
{
    struct auralic_panic_head * panic = NULL;
    
	atomic_notifier_chain_register(&panic_notifier_list, &aura_notifier);
    printk(KERN_DEBUG"loading auralic panic module!\n");
    logbuff = ioremap(PHY_MEM_LOG_BUFF_ADDR, PHY_MEM_LOG_BUFF_SIZE);
    if(NULL == logbuff)
    {
        printk("auralic panic ioremap error!\n");
        return -ENOMEM;
    }
    else
    {
        panic = logbuff;
        if(0 < panic->logsize && 0 == strncmp(panic->magic, PANIC_MAGIC, strlen(PANIC_MAGIC)))
        {
            mm_segment_t fs; 
            unsigned long flags;
            struct file *fp = NULL;

            printk(KERN_DEBUG"auralic found panic log %d bytes!\n", panic->logsize);
            fp =filp_open(PANIC_LOG_FILE, O_RDWR | O_CREAT, 0644);
            if(!IS_ERR(fp))
            {
                fs = get_fs();
                set_fs(KERNEL_DS);
                fp->f_op->write(fp, panic->header, panic->logsize, &fp->f_pos);
                filp_close(fp, NULL);
                set_fs(fs);
                printk(KERN_DEBUG"auralic save %s success!\n", PANIC_LOG_FILE);
            }
        }
        
        memset((char *)logbuff, 0, PHY_MEM_LOG_BUFF_SIZE);
    }
    printk(KERN_DEBUG"auralic panic ioremap success!\n");
	return 0;
}

static void __exit aura_panic_exit(void)
{
    iounmap(logbuff);
    printk(KERN_DEBUG"unloading auralic panic module!\n");
}

module_init(aura_panic_init);
module_exit(aura_panic_exit);
        
MODULE_LICENSE("GPL");
MODULE_AUTHOR("yongfa.hu@auralic.com");
MODULE_DESCRIPTION("capture kernel panic info");
