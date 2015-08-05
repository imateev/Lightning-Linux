#ifndef  AURALIC_FILELIST_H
#define  AURALIC_FILELIST_H

#define  AURALIC_NAME_LEN   250
//#define  MATCH_PATH_STR     "/media/hd"
#define  FILELIST_DEBUG     0
#define  FILELIST_MODIFY_DEBUG     0

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
    char buff[AURALIC_NAME_LEN];
};

struct aura_write_info
{
    bool is_used;
    char len;
    char buff[AURALIC_NAME_LEN];
    struct timer_list timer;
};

extern struct kmem_cache *filelist_cache;
extern char MATCH_PATH_STR[20];
extern bool vfs_can_access;
extern spinlock_t filelist_lock;
extern struct list_head filelist_event;   
extern struct task_struct *filelist_task;  
extern char * auralic_get_filename_from_path(char *path);

void aura_start_one_info(struct aura_write_info * info, char *path, char len);
struct aura_write_info *aura_get_one_info(char * path, char len);
void aura_put_one_info(struct aura_write_info * info);
bool aura_fresh_one_info_by_filepath(char *path);

#endif
