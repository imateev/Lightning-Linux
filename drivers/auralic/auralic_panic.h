
#ifndef  __AURALIC_PANIC_H__
#define	 __AURALIC_PANIC_H__

#ifdef CONFIG_AURALIC_MINI
#define     AURA_PANIC_LOG_SIZE_MAX  (128*1024)
#define     PHY_MEM_LOG_BUFF_SIZE   (1024*1024)
#define     PHY_MEM_LOG_BUFF_ADDR   0x2ff00000
#endif

#ifdef CONFIG_AURALIC_ARIES
#define     AURA_PANIC_LOG_SIZE_MAX  (128*1024)
#define     PHY_MEM_LOG_BUFF_SIZE   (1024*1024)
#define     PHY_MEM_LOG_BUFF_ADDR   0x4ff00000
#endif


#define     PANIC_MAGIC     "AuRaLiC"
#define     PANIC_HEADER    "===============================================\n" \
                            "   panic log collect module by AURALIC Ltd.\n" \
                            " developed by yongfa.hu@auralic.com(2015-08-20)\n" \
                            "===============================================\n"
                            
struct auralic_panic_head
{
    char magic[8];
    unsigned logsize;
    char header[0];
};

int setup_panic_head(struct auralic_panic_head *panic)
{
    strncpy(panic->magic, PANIC_MAGIC, strlen(PANIC_MAGIC));
    strncpy(panic->header, PANIC_HEADER, strlen(PANIC_HEADER));
    
    return sizeof(struct auralic_panic_head) + strlen(PANIC_HEADER);
}

#endif
