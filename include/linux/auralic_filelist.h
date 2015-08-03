#ifndef  AURALIC_FILELIST_H
#define  AURALIC_FILELIST_H

#define  AURALIC_NAME_LEN   250
#define  MATCH_PATH_STR     "/media/hd"

enum filelist_event_enum
{
    FILELIST_RENEW_LIST = 0,
    FILELIST_WRITE_BUFF = 1
};

struct filelist_event_t
{
    struct list_head list;
    char code;
    char len;
    char buff[200];
};

extern bool vfs_can_access;
extern spinlock_t filelist_lock;
extern struct list_head filelist_event;   
extern struct task_struct *filelist_task;  
extern char * auralic_get_filename_from_path(char *path);



#endif
