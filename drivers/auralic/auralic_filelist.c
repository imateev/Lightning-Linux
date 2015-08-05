
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
#include <linux/spinlock.h>

#include <linux/auralic_filelist.h>


#define         FILELIST_PROC_NAME       "filelist"
#define         FILELIST_NAME_TMP        "/media/mmcblk0p1/app/mediaServer/list.bin"
#define         AURA_WRITE_INFO_NUM      100 

char MATCH_PATH_STR[20]={"/media/hd"};
bool has_newlist = false;
bool is_stoped = false;
bool need_stop = false; // need stop by shell cmd: echo stop > /proc/filelist
bool vfs_can_access = false;
atomic_t info_count;

spinlock_t filelist_lock;
struct list_head filelist_event;
struct task_struct *filelist_task = NULL;

spinlock_t info_lock;
struct aura_write_info aura_info[AURA_WRITE_INFO_NUM];

struct kmem_cache *filelist_cache = NULL;


char *helpstring = "echo start     > /proc/filelist --> start writing file change list\n"
                   "echo stop      > /proc/filelist --> stop writing file change list\n"
                   "echo /media/xy > /proc/filelist --> set /media/xy as new match path\n"
                   ;



void init_aura_info(void)
{
    int i;

    atomic_set(&info_count, 0);
    spin_lock_init(&info_lock);
    for(i=0; i<AURA_WRITE_INFO_NUM; i++)
    {
        aura_info[i].is_used = false;
        init_timer(&aura_info[i].timer);
    }
}

struct aura_write_info *aura_get_one_info(char * path, char len)
{
    int i;    
    char lentmp = len < AURALIC_NAME_LEN ? len : AURALIC_NAME_LEN;

    spin_lock(&info_lock);
    for(i=0; i<AURA_WRITE_INFO_NUM; i++)
    {
        if(false == aura_info[i].is_used)
        {
            aura_info[i].is_used = true;
            aura_info[i].len = lentmp;
            memcpy(aura_info[i].buff, path, lentmp);
            spin_unlock(&info_lock);
            atomic_inc(&info_count);
            return &aura_info[i];
        }
    }
    spin_unlock(&info_lock);
    
    return NULL;
}

void aura_put_one_info(struct aura_write_info * info)
{
     info->is_used = false;
}


bool aura_fresh_one_info_by_filepath(char *path)
{
    int i;
    
    spin_lock(&info_lock);
    for(i=0; i<AURA_WRITE_INFO_NUM; i++)
    {
        if(true == aura_info[i].is_used)
        {
            if(0 == strncmp(path, aura_info[i].buff, AURALIC_NAME_LEN))
            {
                mod_timer(&aura_info[i].timer, jiffies + HZ);
                spin_unlock(&info_lock);
                return true;
            }
            //printk("not match: path=[%s] buff=[%s]\n", path, aura_info[i].buff);
        }
    }
    spin_unlock(&info_lock);
    
    return false;
}

void aura_info_timeout_handle(unsigned long data)
{
    struct aura_write_info * info;
    struct filelist_event_t * event;

    info = (struct aura_write_info *)data;
    event = (struct filelist_event_t *)kmem_cache_alloc(filelist_cache, GFP_ATOMIC);
    if(NULL != event)
    {
        event->code = FILELIST_WRITE_BUFF;                                        
        event->len = info->len;
        memcpy(event->buff, info->buff, info->len);
        spin_lock(&filelist_lock);
        list_add_tail(&event->list, &filelist_event);
        spin_unlock(&filelist_lock);
        wake_up_process(filelist_task);
    }
    memset(info->buff, 0, AURALIC_NAME_LEN);
    atomic_dec(&info_count);
    info->is_used = false;
}



void aura_start_one_info(struct aura_write_info * info, char *path, char len)
{
    info->timer.expires = jiffies + HZ;
	info->timer.data = (unsigned long)info;
	info->timer.function = aura_info_timeout_handle;
	add_timer(&info->timer);
}


char * auralic_get_filename_from_path(char *path)
{
    int i;
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
    vfs_can_access = true;
    
	while (!kthread_should_stop()) 
	{	
		set_current_state(TASK_INTERRUPTIBLE);
		if(true == need_stop)
		{
	        is_stoped = true;
		    schedule();
	        continue;
		}
		else
		{
		    is_stoped = false;
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
        
            kmem_cache_free(filelist_cache, (void *)event);
            event = NULL;
            
            if(true == need_stop)
                break;
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
    int len = 0, tmp;
    char buff[200] = {0};
    
    if(0 != *offset)
        return 0;
    tmp = atomic_read(&info_count);
    if(tmp)
    {
        len += sprintf(buff+len, "status: %s(%d)\n", "busy", tmp);
    }
    else
    {
        len += sprintf(buff+len, "status: %s\n", "idle");
    }
    
    if(true == has_newlist)
    {
        len += sprintf(buff+len, "newlist: %s\n", "yes");
    }
    else
    {
        len += sprintf(buff+len, "newlist: %s\n", "no");
    }
    
    if(true == is_stoped)
    {
        len += sprintf(buff+len, "process: %s\n", "stoped");
    }
    else
    {
        len += sprintf(buff+len, "process: %s\n", "running");
    }

    len += sprintf(buff+len, "path = [%s]\n", MATCH_PATH_STR);
    
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
    else if(0 == strncmp(buff, "/media/", strlen("/media/")))
    {
        char len = strlen(buff);
        if(len < 20)
        {
            memset(MATCH_PATH_STR, 0, 20);
            memcpy(MATCH_PATH_STR, buff, len-1);
            printk("set new match path [%s] success!\n", MATCH_PATH_STR);
        }
        else
        {
            printk("Err: path string can't more than 20 letters!\n");
        }
    }
    else
    {
        printk(helpstring);
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

    filelist_cache = kmem_cache_create("filelist_cache",
					      sizeof(struct filelist_event_t),
					      0, 0, NULL);
    if(NULL == filelist_cache)
    {
        pr_err("create filelist cache failed!\n");
        return -ENOMEM;
    }
    
    filelist_task = kthread_run(filelist_process_fn, NULL, "filelist");
    if (IS_ERR(filelist_task)) 
    {
        pr_err("create filelist task failed!\n");
        return -ENOMEM;
    }
    
    init_aura_info();
    
    return 0;
}

static void __exit filelist_exit(void)
{
    printk(KERN_DEBUG"exit auralic filelist module!\n");
    
    if (filelist_task)
        kthread_stop(filelist_task);
            
	remove_proc_entry(FILELIST_PROC_NAME, NULL);

	if(filelist_cache)
	    kmem_cache_destroy(filelist_cache);
}

module_init(filelist_init);
module_exit(filelist_exit);
        
MODULE_LICENSE("GPL");
MODULE_AUTHOR("yongfa.hu@auralic.com");
MODULE_DESCRIPTION("record file change time");

