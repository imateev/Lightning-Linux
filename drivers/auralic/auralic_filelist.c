
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
#include <linux/mm.h>

#include <linux/auralic_filelist.h>


#define         FILELIST_PROC_NAME       "filelist"
#define         FILELIST_NAME_TMP        "/media/mmcblk0p1/app/mediaServer/list.bin"
#define         AURA_WRITE_INFO_NUM      50 
#define         AURA_WRITE_INFO_NUM_TMP  50 
#define         AURA_WRITE_INFO_NUM_WRITE  50 

char MATCH_PATH_STR[20]={"/media/hd"};
bool has_newlist = false;
bool is_stoped = false;
bool need_stop = false; // need stop by shell cmd: echo stop > /proc/filelist
bool vfs_can_access = false;
atomic_t info_count;

raw_spinlock_t filelist_lock;
struct list_head filelist_event;
struct task_struct *filelist_task = NULL;

raw_spinlock_t info_lock;
raw_spinlock_t info_lock_tmp;
raw_spinlock_t info_lock_write;

struct aura_write_info aura_info[AURA_WRITE_INFO_NUM];
struct aura_write_info aura_info_tmp[AURA_WRITE_INFO_NUM_TMP];
struct aura_write_info aura_info_write[AURA_WRITE_INFO_NUM_WRITE];

struct kmem_cache *filelist_cache = NULL;


char *helpstring = "echo start     > /proc/filelist --> start writing file change list\n"
                   "echo stop      > /proc/filelist --> stop writing file change list\n"
                   "echo /media/xy > /proc/filelist --> set /media/xy as new match path\n"
                   ;



bool init_aura_info(void)
{
    int i;
    struct page *page = NULL;

    atomic_set(&info_count, 0);
    raw_spin_lock_init(&info_lock);
    raw_spin_lock_init(&info_lock_tmp);
    raw_spin_lock_init(&info_lock_write);
    
    for(i=0; i<AURA_WRITE_INFO_NUM; i++)
    {
        aura_info[i].path = NULL;
        aura_info[i].stat = INFO_IDLE;
        init_timer(&aura_info[i].timer);
        page = alloc_page(GFP_KERNEL);
        if(NULL == page)
        {
            printk("filelist alloc pages failed!\n");
            return false;
        }

        aura_info[i].path = NULL;
        aura_info[i].buff = page_address(page);
        if(NULL == aura_info[i].buff)
        {
            printk("filelist address page failed!\n");
            return false;
        }
    }

    for(i=0; i<AURA_WRITE_INFO_NUM_TMP; i++)
    {
        page = alloc_page(GFP_KERNEL);
        if(NULL == page)
        {
            printk("filelist alloc pages tmp failed!\n");
            return false;
        }

        aura_info_tmp[i].stat = INFO_IDLE;
        aura_info_tmp[i].path = NULL;
        aura_info_tmp[i].buff = page_address(page);
        if(NULL == aura_info_tmp[i].buff)
        {
            printk("filelist address page tmp failed!\n");
            return false;
        }
    }
    
    for(i=0; i<AURA_WRITE_INFO_NUM_WRITE; i++)
    {
        page = alloc_page(GFP_KERNEL);
        if(NULL == page)
        {
            printk("filelist alloc pages write failed!\n");
            return false;
        }

        aura_info_write[i].stat = INFO_IDLE;
        aura_info_write[i].path = NULL;
        aura_info_write[i].buff = page_address(page);
        if(NULL == aura_info_write[i].buff)
        {
            printk("filelist address page write failed!\n");
            return false;
        }
    }

    return true;
}

struct aura_write_info *aura_get_one_info(void)
{
    int i;    
    
    raw_spin_lock(&info_lock);
    for(i=0; i<AURA_WRITE_INFO_NUM; i++)
    {
        if(INFO_IDLE == aura_info[i].stat)
        {
            aura_info[i].stat = INFO_USED_OTHER;
            raw_spin_unlock(&info_lock);
            aura_info[i].isdir = false;
            aura_info[i].iswrite = false;
            atomic_inc(&info_count);
            return &aura_info[i];
        }
    }
    raw_spin_unlock(&info_lock);
    
    return NULL;
}

struct aura_write_info *aura_get_one_info_tmp(void)
{
    int i;    
    
    raw_spin_lock(&info_lock_tmp);
    for(i=0; i<AURA_WRITE_INFO_NUM_TMP; i++)
    {
        if(INFO_IDLE == aura_info_tmp[i].stat)
        {
            aura_info_tmp[i].stat = INFO_USED_OTHER;
            raw_spin_unlock(&info_lock_tmp);
            return &aura_info_tmp[i];
        }
    }
    raw_spin_unlock(&info_lock_tmp);
    
    return NULL;
}

struct aura_write_info *aura_get_one_info_write(void)
{
    int i;    
    
    raw_spin_lock(&info_lock_write);
    for(i=0; i<AURA_WRITE_INFO_NUM_WRITE; i++)
    {
        if(INFO_IDLE == aura_info_write[i].stat)
        {
            aura_info_write[i].stat = INFO_USED_OTHER;
            raw_spin_unlock(&info_lock_write);
            return &aura_info_write[i];
        }
    }
    raw_spin_unlock(&info_lock_write);
    
    return NULL;
}


void aura_put_one_info(struct aura_write_info * info)
{
    if(NULL == info)
        return;
        
    raw_spin_lock(&info_lock);
    info->path = NULL;
    memset(info->buff, 0 , PATH_MAX);
    memset(info->file, 0 , NAME_MAX);
    info->stat = INFO_IDLE;
    raw_spin_unlock(&info_lock);
    
    atomic_dec(&info_count);
}

void aura_put_one_info_tmp(struct aura_write_info * info)
{
    if(NULL == info)
        return;
    raw_spin_lock(&info_lock_tmp);
    info->path = NULL;
    memset(info->buff, 0 , PATH_MAX);
    memset(info->file, 0 , NAME_MAX);
    info->stat = INFO_IDLE;
    raw_spin_unlock(&info_lock_tmp);
}

void aura_put_one_info_write(struct aura_write_info * info)
{
    if(NULL == info)
        return;
    raw_spin_lock(&info_lock_write);
    info->path = NULL;
    memset(info->buff, 0 , PATH_MAX);
    memset(info->file, 0 , NAME_MAX);
    info->stat = INFO_IDLE;
    raw_spin_unlock(&info_lock_write);
}


bool aura_fresh_one_info_by_filepath(struct aura_write_info * info)
{
    int i;
    int offset;

    if(NULL == info || NULL == info->path)
        return false;

    offset = (int)((unsigned long)info->path - (unsigned long)info->buff);
    
    raw_spin_lock(&info_lock);
    for(i=0; i<AURA_WRITE_INFO_NUM; i++)
    {
        if(INFO_USED_TIMER == aura_info[i].stat)
        {
            if(0 == strncmp(info->path, aura_info[i].path, PATH_MAX-offset))
            {
                raw_spin_unlock(&info_lock);
                mod_timer(&aura_info[i].timer, jiffies + HZ);
                return true;
            }
        }
    }
    raw_spin_unlock(&info_lock);
    
    return false;
}

void aura_info_timeout_handle(unsigned long data)
{
    struct aura_write_info * info;
    info = (struct aura_write_info *)data;
    
    raw_spin_lock(&filelist_lock);
    list_add_tail(&info->list, &filelist_event);
    raw_spin_unlock(&filelist_lock);
    wake_up_process(filelist_task);
}

void aura_start_one_info(struct aura_write_info * info)
{
    info->timer.expires = jiffies + HZ;
	info->timer.data = (unsigned long)info;
	info->timer.function = aura_info_timeout_handle;
	info->stat = INFO_USED_TIMER;
	
	if(info->timer.entry.next == NULL)
	{
	    add_timer(&info->timer);
	}
	else
	{
	    printk("filelist: add a exist timer, path=[%s] file=[%s]!\n", 
	           info->path==NULL ? "null":info->path, info->file);
	}
}

bool aura_get_filename_to_buff(const char *name, char *buff, int bufflen)
{
    int i, idx=0;
    int len = strlen(name);

    for(i=len-1; i>0; i--)
    {
        if('/' == name[i])
        {
            break;
        }
    }

    if('/' == name[i])
        idx = i+1;
    else
        idx = i;
        
    if(bufflen < len-idx)
        return false;

    memcpy(buff, name+idx, len-idx);
    return true;
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

size_t aura_strlen(const char *s)
{
	const char *sc;

    if(NULL == s)
    {
        printk("filelist: detect strlen null string pointer!\n");
        return 0;
    }
        
	for (sc = s; *sc != '\0'; ++sc)
		/* nothing */;
	return sc - s;
}

int filelist_process_fn(void *data)
{ 
    mm_segment_t fs; 
    unsigned long flags;
    struct file *fp = NULL;
    struct aura_write_info *info;

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
            raw_spin_lock_irqsave(&filelist_lock, flags);
            info = list_entry(filelist_event.next, struct aura_write_info, list);
            list_del(&info->list);
            raw_spin_unlock_irqrestore(&filelist_lock, flags);

            has_newlist = true;
            fp->f_op->llseek(fp, 0, SEEK_END);
            if(NULL != info->path && !IS_ERR(info->path))
            {
                fp->f_op->write(fp, info->path, aura_strlen(info->path), &fp->f_pos);
                if(false == info->iswrite)
                {
                    fp->f_op->write(fp, "/", 1, &fp->f_pos);
                    fp->f_op->write(fp, info->file, aura_strlen(info->file), &fp->f_pos);
                    if(true == info->isdir)
                        fp->f_op->write(fp, "/", 1, &fp->f_pos);
                }
            }
            fp->f_op->write(fp, "\n", strlen("\n"), &fp->f_pos);

            aura_put_one_info(info);
            info = NULL;
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
    
    raw_spin_lock_init(&filelist_lock);
    INIT_LIST_HEAD(&filelist_event);
    
    proc_create(FILELIST_PROC_NAME, 0755, NULL, &filelist_proc_op);

    if(false == init_aura_info())
    {
        pr_err("filelist init aura_info failed!\n");
        return -ENOMEM;
    }
    
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
    int i;
    
    printk(KERN_DEBUG"exit auralic filelist module!\n");
    
    if (filelist_task)
        kthread_stop(filelist_task);
            
	remove_proc_entry(FILELIST_PROC_NAME, NULL);

	
    for(i=0; i<AURA_WRITE_INFO_NUM; i++)
    {
        if(aura_info[i].buff)
            free_page((unsigned long)aura_info[i].path);
        aura_info[i].buff = NULL;
    }
    
    for(i=0; i<AURA_WRITE_INFO_NUM_TMP; i++)
    {
        if(aura_info_tmp[i].buff)
            free_page((unsigned long)aura_info_tmp[i].path);
        aura_info_tmp[i].buff = NULL;
    }
    
    for(i=0; i<AURA_WRITE_INFO_NUM_WRITE; i++)
    {
        if(aura_info_write[i].buff)
            free_page((unsigned long)aura_info_write[i].buff);
        aura_info_write[i].buff = NULL;
    }
}

module_init(filelist_init);
module_exit(filelist_exit);
        
MODULE_LICENSE("GPL");
MODULE_AUTHOR("yongfa.hu@auralic.com");
MODULE_DESCRIPTION("record file change time");

