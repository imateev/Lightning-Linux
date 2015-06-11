//read input key ok

#include <linux/init.h>
#include <linux/err.h>
#include <linux/errno.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/kthread.h>
#include <linux/input.h>

/* proc filesystem */
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/list.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/mm.h>


#define KEY_DEV_NAME       "button"
#define KEY_CLASS_NAME     "button"

#define INPUT_DEV_NAME     "/dev/input/event0"
#define KEY_PRESS_DOWN     1
#define KEY_RELEASE_UP     0

#define KEY_NUMS_TOTAL     3

struct timeval key_buff[KEY_NUMS_TOTAL][2];

int another_code = 0;
 
enum KEY_EVENT
{
    KEY_PAUSED = 0,
    KEY_VOL_INC = 1,
    KEY_VOL_DEC = 2,
    KEY_PAUSE_3SEC = 3,
    KEY_VOL_INC_3SEC = 4,
    KEY_VOL_DEC_3SEC = 5,
    KEY_COMBINE0 = 6, //组合按键1
    KEY_COMBINE1 = 7, //组合按键2
    KEY_BUTT
};

char *key_str[] = 
{
    "PAUSED",
    "VOL_INC",
    "VOL_DEC",
    "PAUSED_3S",
    "VOL_INC_3S",
    "VOL_DEC_3S",
    "COMBINE0",
    "COMBINE1",
    "BUTT"
};

enum KEY_EVENT keyevent = KEY_BUTT;


/* key char dev major num */
unsigned int major = 0;
dev_t key_devt = 0;

struct device *keydev = NULL;
static struct class *key_class = NULL;

static struct task_struct *keytask = NULL;
static struct pid *keypid = NULL;
struct completion keypid_completion;
char key_event_cnt = 0;

wait_queue_head_t kevent_queue;

long get_time_interval_ms(struct timeval *start, struct timeval *end)
{
    long ms_interval = 0;
    ms_interval = (end->tv_sec - start->tv_sec) * 1000 ;
    ms_interval += end->tv_usec / 1000; 
    ms_interval -= start->tv_usec / 1000;
    return ms_interval;
}


int check_commbined_key(int keya, int keyb, struct input_event *ev_key)
{
    int key, firstkey, lastkey;
    int press, release;
    long timeval;
    
    key = ev_key->code % 10;
    if((keya != key) && (keyb != key))
    {
        return 1;
    }

    lastkey = key;
    release = ev_key->value;
    press = 1 - release;
    firstkey = keya == lastkey ? keyb:keya;

    if(0 == key_buff[firstkey][release].tv_sec)
    {
        return 1;
    }

    // release timeval more than 500ms, take as invalide key
    timeval = get_time_interval_ms(&key_buff[firstkey][release], &key_buff[lastkey][release]);
    if(500 < timeval)
    {
        return 1;
    }
    
    // press timeval more than 1000ms, take as invalide key
    timeval = get_time_interval_ms(&key_buff[firstkey][press], &key_buff[lastkey][press]);
    if(1000 < timeval)
    {
        return 1;
    }
    
    return 0;
}

int key_process_fn(void *data)
{
    static struct file *filp = NULL;
    mm_segment_t old_fs;
    struct input_event ev_key;
    long timeval;
    int idx = 0;
    
    printk(KERN_DEBUG"start key_process_fn success\n");
    allow_signal(SIGKILL);

    filp = filp_open(INPUT_DEV_NAME, O_RDWR, 0644);
    if(IS_ERR(filp))
    {
        keytask = NULL;
        complete_and_exit(&keypid_completion, 1);
        return 0;
    }
    
    old_fs = get_fs();
    set_fs(get_ds());
    
    do
    {
        set_current_state(TASK_INTERRUPTIBLE);

        filp->f_op->read(filp, (char *)&ev_key, sizeof(struct input_event), &filp->f_pos);
        if(EV_KEY != ev_key.type)
        {
            continue;
        }
        
        //printk(KERN_DEBUG"%ld.%ld  key:%s  value:%d\n", ev_key.time.tv_sec, 
               //ev_key.time.tv_usec, key_str[ev_key.code%10], ev_key.value);
               
        idx = ev_key.code % 10; // key code start from 10 
        key_buff[idx][ev_key.value].tv_sec = ev_key.time.tv_sec;
        key_buff[idx][ev_key.value].tv_usec = ev_key.time.tv_usec;
        
        if(KEY_PRESS_DOWN == ev_key.value)
        {
            key_event_cnt++;// key press
            continue;
        }
        
        // the last key release
        key_event_cnt--;
        if(0 != key_event_cnt)
            continue;

        // here all key have been release
        keyevent = KEY_BUTT;
        // check combind keys first
        if(0 == check_commbined_key(KEY_VOL_INC, KEY_VOL_DEC, &ev_key))
        {
            keyevent = KEY_COMBINE0;// send key event to read     
            printk(KERN_DEBUG"detected key event: %s\n", key_str[keyevent]);
            wake_up_interruptible(&kevent_queue);
            
            key_buff[KEY_PAUSED][ev_key.value].tv_sec = 0;
            key_buff[KEY_VOL_INC][ev_key.value].tv_sec = 0;
            key_buff[KEY_VOL_DEC][ev_key.value].tv_sec = 0;
            continue;
        }
        
        if(0 == check_commbined_key(KEY_VOL_INC, KEY_PAUSED, &ev_key))
        {
            keyevent = KEY_COMBINE1;// send key event to read     
            printk(KERN_DEBUG"detected key event: %s\n", key_str[keyevent]);
            wake_up_interruptible(&kevent_queue);
            
            key_buff[KEY_PAUSED][ev_key.value].tv_sec = 0;
            key_buff[KEY_VOL_INC][ev_key.value].tv_sec = 0;
            key_buff[KEY_VOL_DEC][ev_key.value].tv_sec = 0;
            continue;
        }

        

        timeval = get_time_interval_ms(&key_buff[idx][1 - ev_key.value], &key_buff[idx][ev_key.value]);
        if(10000 < timeval)// more than 10 seconds
        {
            printk(KERN_DEBUG"more than 10s, invalide key\n");
            key_buff[KEY_PAUSED][ev_key.value].tv_sec = 0;
            key_buff[KEY_VOL_INC][ev_key.value].tv_sec = 0;
            key_buff[KEY_VOL_DEC][ev_key.value].tv_sec = 0;
            continue;
        }
        
        if(3000 < timeval)// more than 3 seconds, means long press event
        {
            // set key event
            //printk("more than 3s, long press key\n");
            if(KEY_PAUSED == idx)
                keyevent = KEY_PAUSE_3SEC;
            else if(KEY_VOL_INC == idx)
                keyevent = KEY_VOL_INC_3SEC;
            else
                keyevent = KEY_VOL_DEC_3SEC;
        }

        if(KEY_BUTT == keyevent)
        {
            // set key event
            //printk("normal key\n");
            if(KEY_PAUSED == idx)
                keyevent = KEY_PAUSED;
            else if(KEY_VOL_INC == idx)
                keyevent = KEY_VOL_INC;
            else
                keyevent = KEY_VOL_DEC;
        }
        // send key event to read     
        printk(KERN_DEBUG"detected key event: %s\n", key_str[keyevent]);
        wake_up_interruptible(&kevent_queue);
        
        key_buff[KEY_PAUSED][ev_key.value].tv_sec = 0;
        key_buff[KEY_VOL_INC][ev_key.value].tv_sec = 0;
        key_buff[KEY_VOL_DEC][ev_key.value].tv_sec = 0;
    } while(!signal_pending(current));

    set_fs(old_fs);
    complete_and_exit(&keypid_completion, 1);
    
    return 0;
}


static ssize_t keydev_write(struct file *filp, const char __user *usr_buf,
                            size_t count, loff_t *f_pos)
{ 
    return count;
}

static ssize_t keydev_read(struct file *filp, char __user *buf, 
                           size_t count, loff_t *f_pos)
{
    char buff[2];
    
    if (filp->f_flags & O_NONBLOCK)
        return -EAGAIN;
        
    buff[0] = 0x55;
    buff[1] = 0x55;
    #if 0
    wait_event_interruptible(kevent_queue, KEY_BUTT != keyevent);
    if(KEY_BUTT > keyevent)
    {
        printk(KERN_DEBUG"read a key:%s\n", key_str[keyevent]);
        buff[0] = 'A'; // auralic
        buff[1] = keyevent;
        keyevent = KEY_BUTT;
        
        return copy_to_user(buf, buff, 2);
    }
    #else
    while(1)
    {
        if(0 == wait_event_interruptible_timeout(kevent_queue, 
                                    KEY_BUTT != keyevent, HZ/20))
        {   // timeout
            if(1 != key_event_cnt)
            {
                continue;
            }
        }
        
        // get a key 
        break;
    }
    
    if(KEY_BUTT > keyevent)
    {
        printk(KERN_DEBUG"read a key:%s\n", key_str[keyevent]);
        buff[0] = 'A'; // auralic
        buff[1] = keyevent;
        keyevent = KEY_BUTT;
        
        return copy_to_user(buf, buff, 2);
    }
    #endif
    
    return -EAGAIN;
}


static int keydev_open(struct inode *inode, struct file *filep)
{
    init_waitqueue_head(&kevent_queue);
    init_completion(&keypid_completion);
    keytask = kthread_run(key_process_fn, NULL, "keytask");
    if (IS_ERR(keytask))
    {
        keytask = NULL;
        pr_err("create keytask thread failed!\n");
        return -ENOMEM;
    }
    
    return 0;
}

static int keydev_release(struct inode *inode, struct file *filp)
{
    if(NULL != keytask)
    {
        keypid = get_pid(task_pid(keytask));
        kill_pid(keypid, SIGKILL, 1);
        put_pid(keypid);
        wait_for_completion(&keypid_completion);
        keypid = NULL;
        keytask = NULL;
    }
    return 0;
}

static const struct file_operations keydev_fops = {
	.owner =	THIS_MODULE,
	.write =	keydev_write,
	.read =		keydev_read,
	.open =		keydev_open,
	.release =	keydev_release,
};


static int __init button_init(void)
{    
    printk(KERN_DEBUG"enter auralic button module!\n");

    major = register_chrdev(0, KEY_DEV_NAME, &keydev_fops);
    if (0 > major)
    {
        return -1;
    }
    
    key_devt = MKDEV(major, 0);

    /* create class /sys/device/KEY_CLASS_NAME */
    key_class = class_create(THIS_MODULE, KEY_CLASS_NAME);
    if (NULL == key_class || IS_ERR(key_class))
    {
        unregister_chrdev(major, KEY_CLASS_NAME);
        return -1;
    }
    
    keydev = device_create(key_class, NULL, key_devt, NULL, KEY_DEV_NAME);    
    if (NULL == keydev || IS_ERR(keydev))
    {
        unregister_chrdev(major, KEY_CLASS_NAME);
	    class_destroy(key_class);
        return -1;
    }

	return 0;
}

static void __exit button_exit(void)
{
	printk(KERN_DEBUG"exit auralic button module!\n");
    
    device_destroy(key_class, key_devt);
	class_destroy(key_class);
    unregister_chrdev(major, KEY_CLASS_NAME);
}

module_init(button_init);
module_exit(button_exit);


MODULE_LICENSE("GPL");
MODULE_AUTHOR("hu yongfa 2015-04-28");
MODULE_DESCRIPTION("buttons driver for auralic");
