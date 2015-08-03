
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


#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/kthread.h>

#include <linux/auralic_filelist.h>


#define         FILELIST_PROC_NAME       "filelist"
#define         FILELIST_NAME_TMP         "/media/mmcblk0p1/.list.bin"


bool has_newlist = false;
bool is_stoped = false;
bool need_stop = false; // need stop by shell cmd: echo stop > /proc/filelist
bool vfs_can_access = false;

spinlock_t filelist_lock;
struct list_head filelist_event;
struct task_struct *filelist_task = NULL;

char * auralic_get_filename_from_path(char *path)
{
    char i;
    char len = strlen(path);

    for(i=len-1; i>0; i--)
    {
        if('/' == path[i])
            return path+i+1;
    }

    // i==0
    if('/' == path[i])
        return path+i+1;
    else
        return path;
}


int filelist_process_fn(void *data)
{ 
    mm_segment_t fs; 
    struct file *fp = NULL;
    struct filelist_event_t *event = NULL;

	msleep(30000);// wait for mmcblk mount	
    fp =filp_open(FILELIST_NAME_TMP, O_RDWR | O_CREAT, 0644);
    if (IS_ERR(fp))
    {
        printk("create filelist file failed!\n");
        return 1;
    }
    
	printk(KERN_DEBUG"%s starting\n", __func__);
    fs = get_fs();
    set_fs(KERNEL_DS);
    //vfs_can_access = true;
    
	while (!kthread_should_stop()) 
	{	
		set_current_state(TASK_INTERRUPTIBLE);
		if(true == need_stop)
		{
	        is_stoped = true;
		    schedule();
	        continue;
		}
		
		while(!list_empty(&filelist_event))
        {
            __set_current_state(TASK_RUNNING);
            spin_lock(&filelist_lock);
            event = list_entry(filelist_event.next, struct filelist_event_t, list);
            list_del(&event->list);
            spin_unlock(&filelist_lock);

            if(FILELIST_WRITE_BUFF == event->code)// write record buff to .list.bin
            {        
                has_newlist = true;
                fp->f_op->llseek(fp, 0, SEEK_END);
                fp->f_op->write(fp, event->buff, event->len, &fp->f_pos);
                fp->f_op->write(fp, "\n", strlen("\n"), &fp->f_pos);
            }
        
            kfree(event);
            event = NULL;
        }
        
	    schedule();
		
	} /* end while (!kthread_should_stop()) */
	
	__set_current_state(TASK_RUNNING);

    filp_close(fp, NULL);
    set_fs(fs);
        
	printk(KERN_DEBUG"%s exiting!\n", __func__);
	
	return 0;
}

                
ssize_t filelist_proc_read(struct file *filp, char __user *usrbuf, size_t size, loff_t *offset)
{
    char len;
    char buff[200] = {0};
    
    if(0 != *offset)
            return 0;
    
    if(true == has_newlist)
    {
        sprintf(buff, "%s", "yes\n");
        len = strlen("yes\n");
    }
    else
    {
        sprintf(buff, "%s", "no\n");
        len = strlen("no\n");
    }
    
    *offset = len;
    
    if(0 != copy_to_user(usrbuf, buff, len))
    {
        return 0;
    }
    
    return len;
}


static ssize_t filelist_proc_write(struct file *filp, const char __user *usr_buf,
                              size_t count, loff_t *f_pos)
{
    char len;
    char buff[200] = {0};

    len = count < 199 ? count : 199;
    memset(buff, '\0', 199);
    if(0 != copy_from_user(buff, usr_buf, len))
    {
        goto out;
    }
    
    if(0 == strncmp(buff, "start", 5))
    {
        need_stop = false;
        printk(KERN_DEBUG"filelist started!\n");
        wake_up_process(filelist_task);
    }
    else if(0 == strncmp(buff, "stop", 4))
    {
        is_stoped = false;
        need_stop = true;
        wake_up_process(filelist_task);
        while(false == is_stoped)
        {
                msleep(50);
        }
        has_newlist = false;
        printk(KERN_DEBUG"filelist stoped!\n");
    }
	
out:		                
    return count;
}


static const struct  file_operations filelist_proc_op = {
    .read = filelist_proc_read,
    .write = filelist_proc_write,
};
     
static int __init filelist_init(void)
{    
    printk(KERN_DEBUG"enter auralic filelist module!\n");
    
    INIT_LIST_HEAD(&filelist_event);
    spin_lock_init(&filelist_lock);
    
    proc_create(FILELIST_PROC_NAME, 0755, NULL, &filelist_proc_op);
    
    filelist_task = kthread_run(filelist_process_fn, NULL, "filelist");
    if (IS_ERR(filelist_task)) 
    {
        pr_err("create filelist task failed!\n");
        return -ENOMEM;
    }
                
    return 0;
}

static void __exit filelist_exit(void)
{
    printk(KERN_DEBUG"exit auralic filelist module!\n");
    
    if (filelist_task)
        kthread_stop(filelist_task);
            
	remove_proc_entry(FILELIST_PROC_NAME, NULL);
}

module_init(filelist_init);
module_exit(filelist_exit);
        
MODULE_LICENSE("GPL");
MODULE_AUTHOR("yongfa.hu@auralic.com");
MODULE_DESCRIPTION("record file change time");

