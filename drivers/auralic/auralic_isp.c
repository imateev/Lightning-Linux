
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
#include <linux/mod_devicetable.h>

#include <linux/of_platform.h>

#include <linux/spi/spi.h>

/* proc filesystem */
#include <linux/proc_fs.h>
#include <linux/fs.h>
#include <asm/uaccess.h>
#include <linux/list.h>
#include <linux/gpio.h>
#include <linux/delay.h>
#include <linux/mm.h>


#define ISP_PROC_NAME       "atmel"
#define ISP_CLASS_NAME      "atmel"
#define ISP_DEV_NAME        "atmel_isp"
#define ISP_PAGE_SIZE       256    /* atmega644 flash block size in bytes */
#define ISP_MAX_PAGES       512    /* block counts */
//#define BIN_FILE_PATH       "/home/root/file"
#define PROC_BUFF_SIZE      200    /* proc read/write buff */
#define RESET_AVR_GPIO      (1*32 + 26) /* GPIO2_26 EIM_RW */
#define ISP_CHAR_DEV        0 /*0: no char dev   1:create char dev */


char hexfile_path[100] = "/auralic/isp/file.hex";
char eepfile_path[100] = "/auralic/isp/file.eep";
char cfgfile_path[100] = "/auralic/isp/isp.cfg";
char fuselow = 0xe2;
char fusehigh = 0x99;
char fuseext = 0xff;

bool inISP_mode      = false; /* already be in ISP mode */
bool skip_fuselow    = false;
bool skip_fuseext    = false;
bool skip_fusehigh   = false;
bool skip_eeprom     = false;


#define CFG_MAX_SIZE        1024



/* isp char dev major num */
unsigned int isp_major_num = 0;
dev_t isp_devt = 0;
unsigned int total_size = 0;

#if ISP_CHAR_DEV
struct device *dev = NULL;
static struct class *atmel_class = NULL;
#endif

struct spi_device *spidev = NULL; 

typedef struct _avrcmd_s {
    char byte1; // cmd
    char byte2; 
    char byte3;
    char byte4; // data
}avrcmd_t;

typedef enum _mcu_type {
    ATMEGA644PA = 1,
    MCU_TYPE_BUTT
}MCU_TYPE_E;

enum mcu_flash_size {
    ATMEGA644PA_FLASH_SIZE = 65536, // 64KB
    MCU_FLASH_SIZE_BUTT = 0,
}MCU_FLASH_SIZE_E;

enum mcu_hex_size {
    ATMEGA644PA_HEX_SIZE = 176128, // 173KB
    MCU_HEX_SIZE_BUTT = 0,
}MCU_HEX_SIZE_E;


enum mcu_eeprom_size {
    ATMEGA644PA_EEPROM_SIZE = 2048, // 2KB
    MCU_EEPROM_SIZE_BUTT = 0,
}MCU_EEPROM_SIZE_E;


typedef enum isp_status {
    ISP_STATE_BUTT   = 0,
    ISP_STATE_FAIL   = 1,
    ISP_STATE_DOING  = 2,
    ISP_STATE_DONE   = 3,
}ISP_STATE_E;

char *isp_state_string[] = {
    "butt",
    "fail",
    "doing",
    "done",
    "null"
};

ISP_STATE_E cur_isp_state = ISP_STATE_DONE;
ISP_STATE_E verify_state = ISP_STATE_DONE;

static const struct spi_device_id atmel_ids[] = {
	{ "atmega644pa",  0},
	{ },
};
MODULE_DEVICE_TABLE(spi, atmel_ids);


char *help_string = "\n"
                    "echo dbgon     > /proc/atmel ---> to switch on debug print\n"
                    "echo dbgoff    > /proc/atmel ---> to switch off debug print\n"
                    "echo reset     > /proc/atmel ---> to reboot avr mcu\n"
                    "echo doisp     > /proc/atmel ---> to start isp download\n"
                    "echo verify    > /proc/atmel ---> to check avr flash with hex file\n"
                    "echo /xx/yy.zz > /proc/atmel ---> to set yy.zz as config file\n"
                    "cat  /proc/atmel ---------------> to display current config\n"
                    "\n";

typedef enum dbg_level {
    NORMAL = 0,
    DEBUG  = 1,
    DBG_BUT,
} DEBUG_LEVEL_E;

bool dbglevel = false;


#define dbgisp(level, format, args...)   \
do  \
{   \
    if(true == dbglevel) \
    { \
        printk(format, ##args); \
    } \
    else if(level == NORMAL) \
    { \
        printk(format, ##args); \
    } \
}   \
while(0)

/*
send enable program instruct to avr,
and check whether 0x53 can be echo back correctly
*/
bool avr_program_enable(void)
{
    bool ret = false;
    char data = 0xff;    
    avrcmd_t cmd;

    cmd.byte1 = 0xac;
    cmd.byte2 = 0x53; /* this byte will echo back by avr */
    cmd.byte3 = 0;
    cmd.byte4 = 0;
    
    if(0 != spi_write(spidev, &cmd, 2))
    {
        dbgisp(DEBUG, "Err: %s %d\n", __func__, __LINE__);
        return ret;
    }
    
    if(0 != spi_read(spidev, &data, 1))
    {
        dbgisp(DEBUG, "Err: %s %d\n", __func__, __LINE__);
        return ret;
    }
    
    spi_write(spidev, &cmd.byte4, 1);

    if(0x53 ==  data)
    {
        ret = true;
    }
    else
    {
        dbgisp(DEBUG, "Err: %s %d\n", __func__, __LINE__);    
    }
    return ret;
}


bool avr_load_extendaddr_type(char ext_addr)
{
    avrcmd_t cmd;
    bool ret = false;

    cmd.byte1 = 0x4d;
    cmd.byte2 = 0;
    cmd.byte3 = ext_addr;
    cmd.byte4 = 0;

    if(0 == spi_write(spidev, &cmd, sizeof(avrcmd_t)))
    {
        ret = true;
    }

    return ret;
}

/*
write the high byte of a world to page buff
*/
bool avr_load_memory_page_highbyte(char addr_lsb, char data)
{
    avrcmd_t cmd;
    bool ret = false;

    cmd.byte1 = 0x48;
    cmd.byte2 = 0;
    cmd.byte3 = addr_lsb;
    cmd.byte4 = data;

    if(0 == spi_write(spidev, &cmd, sizeof(avrcmd_t)))
    {
        ret = true;
    }

    return ret;
}

/*
write the low byte of a world to page buff
*/
bool avr_load_memory_page_lowbyte(char addr_lsb, char data)
{
    avrcmd_t cmd;
    bool ret = false;

    cmd.byte1 = 0x40;
    cmd.byte2 = 0;
    cmd.byte3 = addr_lsb;
    cmd.byte4 = data;

    if(0 == spi_write(spidev, &cmd, sizeof(avrcmd_t)))
    {
        ret = true;
    }

    return ret;
}

/*
write page buff to a page of flash, the first page is page 0
*/
bool avr_write_memory_page_to_flash(int page)
{
    avrcmd_t cmd;
    bool ret = false;
    char addr_msb, addr_lsb;
    
    addr_lsb = page & 0xfe; // the last bit of page as the top bit of lsb
    addr_lsb = page << 7;

    addr_msb = page >> 1;  // the top 7bit of page as the last 7bit of msb
    
    cmd.byte1 = 0x4c;
    cmd.byte2 = addr_msb;
    cmd.byte3 = addr_lsb;
    cmd.byte4 = 0;

    if(0 == spi_write(spidev, &cmd, sizeof(avrcmd_t)))
    {
        ret = true;
    }

    return ret;
}

/*
read the high byte of a world
*/
bool avr_read_memory_highbyte(unsigned int addr, char *data)
{
    avrcmd_t cmd;
    char addr_msb;
    char addr_lsb;
    bool ret = false;

    if(NULL == data)
    {
        return ret;
    }

    addr_lsb = addr & 0xff;
    addr_msb = (addr >> 8) & 0xff;
    
    cmd.byte1 = 0x28;
    cmd.byte2 = addr_msb;
    cmd.byte3 = addr_lsb;
    cmd.byte4 = 0;

    if(0 != spi_write(spidev, &cmd, 3))
    {
        return ret;
    }
    
    if(0 == spi_read(spidev, data, 1))
    {
        ret = true;
    }

    return ret;
}

/*
read the low byte of a world
*/
bool avr_read_memory_lowbyte(unsigned int addr, char *data)
{
    avrcmd_t cmd;
    char addr_msb;
    char addr_lsb;
    bool ret = false;

    if(NULL == data)
    {
        return ret;
    }

    addr_lsb = addr & 0xff;
    addr_msb = (addr >> 8) & 0xff;

    cmd.byte1 = 0x20;
    cmd.byte2 = addr_msb;
    cmd.byte3 = addr_lsb;
    cmd.byte4 = 0;

    if(0 != spi_write(spidev, &cmd, 3))
    {
        return ret;
    }
    
    if(0 == spi_read(spidev, data, 1))
    {
        ret = true;
    }

    return ret;
}


bool read_avr_low_fuse(char *data)
{
    bool ret = false;
    avrcmd_t cmd;

    memset(&cmd, 0, sizeof(avrcmd_t));
    cmd.byte1 = 0x50;

    if(0 != spi_write(spidev, &cmd, 3))
    {
        return ret;
    }
    
    if(0 == spi_read(spidev, data, 1))
    {
        ret = true;
    }

    return ret;
}

bool read_avr_high_fuse(char *data)
{
    bool ret = false;
    avrcmd_t cmd;

    memset(&cmd, 0, sizeof(avrcmd_t));
    cmd.byte1 = 0x58;
    cmd.byte2 = 0x08;

    if(0 != spi_write(spidev, &cmd, 3))
    {
        return ret;
    }
    
    if(0 == spi_read(spidev, data, 1))
    {
        ret = true;
    }

    return ret;
}

bool read_avr_ext_fuse(char *data)
{
    bool ret = false;
    avrcmd_t cmd;

    memset(&cmd, 0, sizeof(avrcmd_t));
    cmd.byte1 = 0x50;
    cmd.byte2 = 0x08;

    if(0 != spi_write(spidev, &cmd, 3))
    {
        return ret;
    }
    
    if(0 == spi_read(spidev, data, 1))
    {
        ret = true;
    }

    return ret;
}


bool write_avr_low_fuse(char data)
{
    bool ret = false;
    avrcmd_t cmd;

    cmd.byte1 = 0xac;
    cmd.byte2 = 0xa0;
    cmd.byte3 = 0x00;
    cmd.byte4 = data;

    if(0 == spi_write(spidev, &cmd, 4))
    {
        ret = true;
        mdelay(5);
    }

    return ret;
}

bool write_avr_high_fuse(char data)
{
    bool ret = false;
    avrcmd_t cmd;

    cmd.byte1 = 0xac;
    cmd.byte2 = 0xa8;
    cmd.byte3 = 0x00;
    cmd.byte4 = data;

    if(0 == spi_write(spidev, &cmd, 4))
    {
        ret = true;
        mdelay(5);
    }

    return ret;
}


bool write_avr_ext_fuse(char data)
{
    bool ret = false;
    avrcmd_t cmd;

    cmd.byte1 = 0xac;
    cmd.byte2 = 0xa4;
    cmd.byte3 = 0x00;
    cmd.byte4 = data;

    if(0 == spi_write(spidev, &cmd, 4))
    {
        ret = true;
        mdelay(5);
    }

    return ret;
}


bool read_one_byte_avr_eeprom(int addr, char *data)
{
    bool ret = false;
    avrcmd_t cmd;

    cmd.byte1 = 0xa0;
    cmd.byte2 = (addr >> 8) & 0xff;
    cmd.byte3 = addr & 0xff;

    if(0 != spi_write(spidev, &cmd, 3))
    {
        return ret;
    }
    
    if(0 == spi_read(spidev, data, 1))
    {
        ret = true;
    }

    return ret;

}


bool read_avr_eeprom_to_buff(char *buff, unsigned int len)
{   
    bool ret = false;
    unsigned int index = 0;

    if(NULL == buff)
    {
        dbgisp(DEBUG, "%s: buff is NUll!\n", __func__);
        return ret;
    }

    while(index < len)
    {
        if(false == read_one_byte_avr_eeprom(index, buff + index))
        {
            break;
        }
        
        index++;
    }

    if(index == len)
    {
        ret = true;
    }
    
    return ret;
}



bool write_one_byte_avr_eeprom(int addr, char data)
{
    bool ret = false;
    avrcmd_t cmd;

    cmd.byte1 = 0xc0;
    cmd.byte2 = (addr >> 8) & 0xff;
    cmd.byte3 = addr & 0xff;
    cmd.byte4 = data;

    if(0 == spi_write(spidev, &cmd, 4))
    {
        ret = true;
        mdelay(5);
    }

    return ret;
}


bool write_buff_to_avr_eeprom(char *buff, unsigned int len)
{
    bool ret = false;
    unsigned int index = 0;

    if(NULL == buff)
    {
        dbgisp(DEBUG, "%s: buff is NUll!\n", __func__);
        return ret;
    }

    while(index < len)
    {
        if(false == write_one_byte_avr_eeprom(index, buff[index]))
        {
            break;
        }
        
        index++;
    }

    if(index == len)
    {
        ret = true;
    }
    
    return ret;
}

unsigned long get_mcu_max_hexfile_size(MCU_TYPE_E type)
{
    unsigned long size = MCU_HEX_SIZE_BUTT;
    
    if(ATMEGA644PA == type)
    {
        size = ATMEGA644PA_HEX_SIZE;
    }

    return size;
}

unsigned long get_mcu_flash_size(MCU_TYPE_E type)
{
    unsigned long size = MCU_FLASH_SIZE_BUTT;
    
    if(ATMEGA644PA == type)
    {
        size = ATMEGA644PA_FLASH_SIZE;
    }

    return size;
}


unsigned long get_mcu_eeprom_size(MCU_TYPE_E type)
{
    unsigned long size = MCU_EEPROM_SIZE_BUTT;
    
    if(ATMEGA644PA == type)
    {
        size = ATMEGA644PA_EEPROM_SIZE;
    }

    return size;
}

bool write_buff_to_file(char *path, char *buff, unsigned long size)
{
    bool ret = false;
    struct file *fp;
    mm_segment_t fs;
    
    if(NULL == buff || NULL == path)
    {            
        dbgisp(DEBUG, "%s: buff or path is NULL!\n", __func__);
        return ret;
    }
    
    fp = filp_open(path, O_CREAT, 0644);
    if (IS_ERR(fp)) {
        dbgisp(DEBUG, "%s: open file %s failed!\n", __func__, path);
        return ret;
    }    
    
    fs = get_fs();
    set_fs(KERNEL_DS);
    
    fp->f_op->write(fp, buff, size, &fp->f_pos); 
            
    filp_close(fp, NULL);
    set_fs(fs);   
    ret = true;
    
    return ret; 
}



unsigned long read_file_to_buff(char *path, char *buff, unsigned long size)
{
    loff_t pos;
    struct file *fp;
    mm_segment_t fs;
    int size_tmp;
    unsigned long read_size = 0;
    
    if(NULL == buff || NULL == path)
    {            
        dbgisp(DEBUG, "%s: buff or path is NULL!\n", __func__);
        return read_size;
    }
    
    fp = filp_open(path, O_RDONLY, 0644);
    if (IS_ERR(fp)) {
        dbgisp(DEBUG, "%s: open file %s failed!\n", __func__, path);
        return read_size;
    }    
    
    fs = get_fs();
    set_fs(KERNEL_DS);
    
    pos = 0;    
    size_tmp = 0;
    while(1)
    {
        size_tmp = fp->f_op->read(fp, buff, size - read_size, &pos);  
        if(0 > size_tmp)
        {
            dbgisp(DEBUG, "%s: read file %s failed!\n", __func__, path);
            break;
        }
        
        if(0 == size_tmp)
        {
            dbgisp(DEBUG, "%s: reach file end!\n", __func__);
            break;
        }
        
        read_size += size_tmp;        
        if(read_size == size)// have got enough data
        {
            dbgisp(DEBUG, "%s: have get enough data!\n", __func__);
            break;
        }
    }
            
    filp_close(fp, NULL);
    set_fs(fs);   
    
    return read_size; 
}


bool write_buff_to_avr_flash(char *buff, unsigned int len)
{
    int i = 0;
    bool ret = false;
    int page_cnt = 0;
    //int ext_addr = 0;
    int page_index = 0;
    unsigned int buff_index = 0;

    if(NULL == buff)
    {
        dbgisp(DEBUG, "Err: buff is NULL!\n");
        return ret;
    }

    page_cnt = len / ISP_PAGE_SIZE;
    
    if(len % ISP_PAGE_SIZE)
    {
        dbgisp(DEBUG, "Err: buff length is not page aligned!\n");
        return ret;
    }

    if(ISP_MAX_PAGES < page_cnt)
    {
        dbgisp(DEBUG, "Err: buff size exceed the ISP_MAX_PAGES!\n");
        return ret;
    }
    
    //if(false == avr_load_extendaddr_type(ext_addr))
        //return ret;
        
    page_index = 0;
    buff_index = 0;
    
    while(page_index < page_cnt)
    {
        for(i=0; i < ISP_PAGE_SIZE; i++)
        {
            if(i%2)
            {   
                // high byte
                if(false == avr_load_memory_page_highbyte(i/2, buff[buff_index++]))
                    return ret;
            }
            else
            {
                // low byte
                if(false == avr_load_memory_page_lowbyte(i/2, buff[buff_index++]))
                    return ret;
            }
        }

        // write page buff to flash
        avr_write_memory_page_to_flash(page_index++);
        mdelay(10);
    }
    
    ret = true;
    return ret;
}


bool read_avr_flash_to_rbuff(char *buff, unsigned int len)
{
    
    unsigned int i;
    bool ret = false;
    //int ext_addr = 0;
    unsigned int buff_index = 0;

    if(NULL == buff)
    {
        dbgisp(DEBUG, "%s: buff is NULL!\n", __func__);
        return ret;
    }
    
    if(len % ISP_PAGE_SIZE)
    {
        dbgisp(DEBUG, "%s: buff length is not page aligned!\n", __func__);
        return ret;
    }

    ret = true;
    buff_index = 0;
    
    for(i=0; i < len/2; i++) // world
    {
        // low byte
        if(false == avr_read_memory_lowbyte(i, buff + buff_index++))
        {
            ret = false;
            break;
        }
        
        // high byte
        if(false == avr_read_memory_highbyte(i, buff + buff_index++))
        {
            ret = false;
            break;
        }        
    } 
    
    return ret; 
}


bool verify_read_write_buff(char *wbuff, char *rbuff, unsigned long len)
{
    bool ret = false;   
    unsigned long index = 0;
    
    if(NULL==wbuff || NULL==rbuff)
    {
        dbgisp(DEBUG, "%s: wbuff or rbuff is NULL!\n", __func__);
        return ret;
    }

    for(index=0; index < len; index++)
    {
        if(wbuff[index] != rbuff[index])
        {
            dbgisp(DEBUG, "verify failed at addr: 0x%lx!\n", index);
            break;
        }
    }

    if(index == len)
        ret = true;
        
    return ret; 
}

char hex_to_str(char hex)
{
    char str = 0x0f;   

    switch(hex)
    {
        case 0:
        case 1:
        case 2:
        case 3:
        case 4:
        case 5:
        case 6:
        case 7:
        case 8:
        case 9:
            str = '0' + hex;
        break;
        
        case 10:
            str = 'A';
        break;
        
        case 11:
            str = 'B';
        break;
        
        case 12:
            str = 'C';
        break;
        
        case 13:
            str = 'D';
        break;
        
        case 14:
            str = 'E';
        break;
        
        case 15:
            str = 'F';
        break;        
    }

    return str;
}

char str_to_hex(char *str)
{
    char hex = 0x0f;
    
    if(NULL == str)
        return hex;

    switch(*str)
    {
        case '0':
        case '1':
        case '2':
        case '3':
        case '4':
        case '5':
        case '6':
        case '7':
        case '8':
        case '9':
            hex = (*str) - '0';
            break;

        case 'a':
        case 'b':
        case 'c':
        case 'd':
        case 'e':
        case 'f':
            hex = (*str) - 'a' + 10;
            break;

        case 'A':
        case 'B':
        case 'C':
        case 'D':
        case 'E':
        case 'F':
            hex = (*str) - 'A' + 10;
            break;
    }

    return hex;
}

/* 2strings to one hex byte */
char str2hex(char *data)
{
    return ((str_to_hex(data) << 4) | str_to_hex(data+1));
}


unsigned int hexfile_to_binfile(char *hexbuff, char *binbuff, unsigned int binbuff_len, bool *isok)
{

#define HEXSTART    ":"
#define DATA        0
#define EOF         1
    
#define START       0  
#define DATALEN     1
#define ADDR_HIGH   3
#define ADDR_LOW    5
#define DATATYPE    7
#define DATA0       9

    int i;
    char *tmp = hexbuff;
    char bufftmp[100];
    unsigned int binlen = 0;
    unsigned int addr;
    unsigned int addr_tmp = 5;
    
    unsigned int datalen = 5;
    unsigned int datatype = 5;
    
    if(NULL == hexbuff || NULL == binbuff || NULL == isok)
    {
        return binlen;
    }

    *isok = false;
    addr = 0;
    while(1)
    {   
        tmp = strstr(tmp, HEXSTART);
        if(NULL == tmp)
        {
            dbgisp(DEBUG, "tmp is NULL\n");
            break;
        }
        
        memcpy(bufftmp, tmp, 9);
        i = sscanf(bufftmp, ":%2x%4x%2x", &datalen, &addr_tmp, &datatype);
        dbgisp(DEBUG, "len=0x%02x  addr=0x%04x  addr_tmp=0x%04x  type=0x%02x\n", datalen, addr, addr_tmp, datatype);
        
        if(EOF == datatype)
        {
            dbgisp(DEBUG, "found EOF\n");
            *isok = true;
            break;
        }   
        
        if(DATA != datatype)
        {
            dbgisp(DEBUG, "non DATA type\n");
            binlen = 0;
            break;
        }  
        
        if(addr != addr_tmp)
        {
            dbgisp(DEBUG, "not line addr\n");
            binlen = 0;
            break;
        }
        
        if(binlen + datalen > binbuff_len)
        {
            dbgisp(DEBUG, "datalen more than buff len\n");
            binlen = 0;
            break;
        }

        addr += datalen;
        for(i=0; i < datalen; i++)
        {
            binbuff[binlen++] = str2hex(&tmp[DATA0 + i*2]);
            //dbgisp(DEBUG, "hex:0x%02x   high:%c  low:%c\n", binbuff[binlen-1], tmp[DATA0 + i*2], tmp[DATA0 + i*2+1]);
        }

        tmp += datalen * 2;
        
    }
    dbgisp(DEBUG, "binlen = 0x%04x\n", binlen);
    return  binlen;
}



bool do_enter_avr_isp_mode(void)
{
    bool ret = false;
    char data = 0xff;

    /* start to enter ISP mode */
    if(false == inISP_mode)
    {
        spi_write(spidev, &data, 1);
        gpio_direction_output(RESET_AVR_GPIO, 1);
        gpio_set_value(RESET_AVR_GPIO, 1); 
        mdelay(5);
        gpio_set_value(RESET_AVR_GPIO, 0);
        inISP_mode = avr_program_enable();
    }

    ret = inISP_mode;

    return ret;
}


void do_leave_isp_mode(void)
{    
    gpio_set_value(RESET_AVR_GPIO, 1); 
}

bool do_avr_fuse_program(char fuselow, char fusehigh, char fuseext)
{    
    char low=0, high=0, ext=0;
    
    if( false == write_avr_low_fuse(fuselow)
        || false == write_avr_high_fuse(fusehigh)
        || false == write_avr_ext_fuse(fuseext)  )
    {
        return false;
    }
        
    if( false == read_avr_low_fuse(&low)
        || false == read_avr_high_fuse(&high)
        || false == read_avr_ext_fuse(&ext)  )
    {
        return false;
    }

    if( low != fuselow || high != fusehigh || ext != fuseext)
    {
        return false;
    }
    
    return true;
}

bool do_avr_fuselow_program(char fuselow)
{    
    char low=0;
    
    if( false == write_avr_low_fuse(fuselow))
    {
        return false;
    }
        
    if( false == read_avr_low_fuse(&low) )
    {
        return false;
    }

    if( low != fuselow)
    {
        return false;
    }
    
    return true;
}

bool do_avr_fusehigh_program(char fusehigh)
{    
    char high=0;
    
    if( false == write_avr_high_fuse(fusehigh))
    {
        return false;
    }
        
    if( false == read_avr_high_fuse(&high) )
    {
        return false;
    }

    if( high != fusehigh)
    {
        return false;
    }
    
    return true;
}

bool do_avr_fuseext_program(char fuseext)
{    
    char ext=0;
    
    if( false == write_avr_ext_fuse(fuseext))
    {
        return false;
    }
        
    if( false == read_avr_ext_fuse(&ext) )
    {
        return false;
    }

    if( ext != fuseext)
    {
        return false;
    }
    
    return true;
}


bool do_avr_eeprom_program(MCU_TYPE_E mcu_type, char *path)
{
    bool ret = false;
    bool isok = false;
    char *hexbuff = NULL;
    char *eepbuff = NULL;
    char *binbuff = NULL;
    unsigned long size = 0;
    unsigned long eeprom_size = 0;
    
    if(ATMEGA644PA != mcu_type || NULL == path)
    {
        dbgisp(DEBUG, "%s: %s\n", __func__, NULL == path ? 
               "path is NULL!" : "wrong mcu type for atmega644!");
        return ret;
    }

    eeprom_size = get_mcu_eeprom_size(mcu_type);

    /* alloc mem for buff */
    hexbuff = kzalloc(eeprom_size * 3, GFP_KERNEL);
    eepbuff = kzalloc(eeprom_size, GFP_KERNEL);
    binbuff = kzalloc(eeprom_size, GFP_KERNEL);
    if(NULL == eepbuff || NULL == hexbuff || NULL == binbuff)
    {
        dbgisp(DEBUG, "alloc memory for hexbuff or binbuff or eepbuff failed!\n");
        kfree(hexbuff);
        kfree(eepbuff);
        kfree(binbuff);
        return ret;
    }

    /* start to read hex file */
    size = read_file_to_buff(path, hexbuff, eeprom_size * 3);
    if(0 >= size)
    {
        dbgisp(DEBUG, "%s: read failed!\n", __func__);
        kfree(hexbuff);
        kfree(eepbuff);
        kfree(binbuff);
        return ret;
    }
    
    size = hexfile_to_binfile(hexbuff, binbuff, eeprom_size, &isok);
    if(0 == size && false == isok)
    {
        dbgisp(DEBUG, "%s: hex to bin failed!\n", __func__);
        kfree(hexbuff);
        kfree(eepbuff);
        kfree(binbuff);
        return ret;
    }

    /* write buff to eeprom */
    if(false == write_buff_to_avr_eeprom(binbuff, size))
    {
        dbgisp(DEBUG, "%s: write failed!\n", __func__);
        kfree(hexbuff);
        kfree(eepbuff);
        kfree(binbuff);
        return ret;
    }   

    /* read eeprom to buff */
    if(false == read_avr_eeprom_to_buff(eepbuff, size))
    {
        dbgisp(DEBUG, "%s: read failed!\n", __func__);
        kfree(hexbuff);
        kfree(eepbuff);
        kfree(binbuff);
        return ret;
    }

    /* verify rbuff and wbuff */
    dbgisp(DEBUG, "start to verify eeprom!\n");
    if(false == verify_read_write_buff(binbuff, eepbuff, size))
    {
        dbgisp(DEBUG, "%s: verify wbuff and rbuff failed!\n", __func__);        
        kfree(hexbuff);
        kfree(eepbuff);
        kfree(binbuff);
        return ret;
    }

    ret = true;
    
    kfree(hexbuff);
    kfree(eepbuff);
    kfree(binbuff);

    return ret;
}


bool do_avr_in_system_program(MCU_TYPE_E mcu_type, char *hexfile_path)
{
    bool ret = false;
    bool isok = false;
    char *hexbuff = NULL;
    char *flashbuff = NULL;
    char *binbuff = NULL; // bin buff
    unsigned long size = 0;
	struct page *page = NULL;
    unsigned long *addrpage = NULL;
    unsigned long flash_size = 0;
    unsigned long hexfile_size = 0;
    
    if(ATMEGA644PA != mcu_type || NULL == hexfile_path)
    {
        dbgisp(DEBUG, "%s: wrong mcu type or hexfile_path is NULL!\n", __func__);
        return ret;
    }
    
    hexfile_size = get_mcu_max_hexfile_size(mcu_type);
    flash_size = get_mcu_flash_size(mcu_type);

    /* alloc mem for buff */
    //hexbuff = kzalloc(ISP_MAX_PAGES * ISP_PAGE_SIZE, GFP_KERNEL);
    page = alloc_pages(GFP_KERNEL, get_order(hexfile_size));
    if(NULL == page)
    { 
        dbgisp(DEBUG, "%s: alloc pages failed!\n", __func__);
        return ret;
    }

    addrpage = page_address(page);
    hexbuff = (char *)addrpage;
    flashbuff = kzalloc(flash_size, GFP_KERNEL);
    binbuff = kzalloc(flash_size, GFP_KERNEL);
    if(NULL == flashbuff || NULL == page || NULL == binbuff)
    {
        dbgisp(DEBUG, "%s: alloc memory for flashbuff or binbuff failed!\n", __func__);
        free_pages((unsigned long)addrpage, get_order(hexfile_size));
        kfree(flashbuff);
        kfree(binbuff);
        return ret;
    }

    /* start to read hex file */
    size = read_file_to_buff(hexfile_path, hexbuff, hexfile_size);
    if(0 >= size)
    {
        dbgisp(DEBUG, "%s: read failed!\n", __func__);
        free_pages((unsigned long)addrpage, get_order(hexfile_size));
        kfree(flashbuff);
        kfree(binbuff);
        return ret;
    }
    
    size = hexfile_to_binfile(hexbuff, binbuff, flash_size, &isok);
    if(0 == size && false == isok)
    {
        dbgisp(DEBUG, "%s: hex to bin failed!\n", __func__);
        free_pages((unsigned long)addrpage, get_order(hexfile_size));
        kfree(flashbuff);
        kfree(binbuff);
        return ret;
    }
    
    /* make size aligned by ISP_PAGE_SIZE */
    if(size % ISP_PAGE_SIZE)
    {
        size += ISP_PAGE_SIZE - (size % ISP_PAGE_SIZE);
    }
    
    if(ISP_MAX_PAGES < size / ISP_PAGE_SIZE)
    {
        dbgisp(DEBUG, "%s: file size exceed the max size!\n", __func__);
        free_pages((unsigned long)addrpage, get_order(hexfile_size));
        kfree(flashbuff);
        kfree(binbuff);
        return ret;
    }

    /* write wbuff to mcu */
    dbgisp(DEBUG, "start writing flash!\n");   
    if(false == write_buff_to_avr_flash(binbuff, size))
    {
        dbgisp(DEBUG, "%s: write buff to mcu failed!\n", __func__);
        free_pages((unsigned long)addrpage, get_order(hexfile_size));
        kfree(flashbuff);
        kfree(binbuff);
        return ret;
    }

    /* read mcu flash to rbuff */
    dbgisp(DEBUG, "start reading flash!\n");
    if(false == read_avr_flash_to_rbuff(flashbuff, size))
    {
        dbgisp(DEBUG, "%s: read mcu flash to buff failed!\n", __func__);
        free_pages((unsigned long)addrpage, get_order(hexfile_size));
        kfree(flashbuff);
        kfree(binbuff);
        return ret;
    }

    /* start to verify the wbuff and rbuff */  
    dbgisp(DEBUG, "start to verify flash!\n");
    if(false == verify_read_write_buff(binbuff, flashbuff, size))
    {
        dbgisp(DEBUG, "%s: verify wbuff and rbuff failed!\n", __func__);  
        free_pages((unsigned long)addrpage, get_order(hexfile_size));
        kfree(flashbuff);
        kfree(binbuff);
        return ret;
    }

    ret = true;
    
    free_pages((unsigned long)addrpage, get_order(hexfile_size));
    kfree(flashbuff);
    kfree(binbuff);

    return ret;
}

bool do_avr_flash_verify(MCU_TYPE_E mcu_type, char *hexfile_path)
{
    bool ret = false;
    bool isok = false;
    char *hexbuff = NULL;
    char *flashbuff = NULL;
    char *binbuff = NULL; // bin buff
    unsigned long size = 0;
    unsigned long align_size = 0;
    struct page *page = NULL;
    unsigned long *addrpage = NULL;
    unsigned long flash_size = 0;
    unsigned long hexfile_size = 0;
    
    if(ATMEGA644PA != mcu_type || NULL == hexfile_path)
    {
        dbgisp(DEBUG, "%s: wrong mcu type or hexfile_path is NULL!\n", __func__);
        return ret;
    }
    
    hexfile_size = get_mcu_max_hexfile_size(mcu_type);
    flash_size = get_mcu_flash_size(mcu_type);

    /* alloc mem for buff */
    //hexbuff = kzalloc(ISP_MAX_PAGES * ISP_PAGE_SIZE, GFP_KERNEL);
    page = alloc_pages(GFP_KERNEL, get_order(hexfile_size));
    if(NULL == page)
    { 
        dbgisp(DEBUG, "%s: alloc pages failed!\n", __func__);
        return ret;
    }

    addrpage = page_address(page);
    hexbuff = (char *)addrpage;
    flashbuff = kzalloc(flash_size, GFP_KERNEL);
    binbuff = kzalloc(flash_size, GFP_KERNEL);
    if(NULL == flashbuff || NULL == page || NULL == binbuff)
    {
        dbgisp(DEBUG, "%s: alloc memory for flashbuff or binbuff failed!\n", __func__);
        free_pages((unsigned long)addrpage, get_order(hexfile_size));
        kfree(flashbuff);
        kfree(binbuff);
        return ret;
    }

    /* start to read hex file */
    size = read_file_to_buff(hexfile_path, hexbuff, hexfile_size);
    if(0 >= size)
    {
        dbgisp(DEBUG, "%s: read failed!\n", __func__);
        free_pages((unsigned long)addrpage, get_order(hexfile_size));
        kfree(flashbuff);
        kfree(binbuff);
        return ret;
    }
    
    size = hexfile_to_binfile(hexbuff, binbuff, flash_size, &isok);
    if(0 == size && false == isok)
    {
        dbgisp(DEBUG, "%s: hex to bin failed!\n", __func__);
        free_pages((unsigned long)addrpage, get_order(hexfile_size));
        kfree(flashbuff);
        kfree(binbuff);
        return ret;
    }
    
    /* make size aligned by ISP_PAGE_SIZE */
    align_size = size;
    if(align_size % ISP_PAGE_SIZE)
    {
        align_size += ISP_PAGE_SIZE - (align_size % ISP_PAGE_SIZE);
    }
    
    
    if(ISP_MAX_PAGES < align_size / ISP_PAGE_SIZE)
    {
        dbgisp(DEBUG, "%s: file size exceed the max size!\n", __func__);
        free_pages((unsigned long)addrpage, get_order(hexfile_size));
        kfree(flashbuff);
        kfree(binbuff);
        return ret;
    }

    /* read mcu flash to rbuff */
    dbgisp(DEBUG, "start reading flash!\n");
    if(false == read_avr_flash_to_rbuff(flashbuff, align_size))
    {
        dbgisp(DEBUG, "%s: read mcu flash to buff failed!\n", __func__);
        free_pages((unsigned long)addrpage, get_order(hexfile_size));
        kfree(flashbuff);
        kfree(binbuff);
        return ret;
    }

    /* start to verify the wbuff and rbuff */  
    dbgisp(DEBUG, "start to verify flash!\n");
    if(false == verify_read_write_buff(binbuff, flashbuff, size))
    {
        dbgisp(DEBUG, "%s: verify wbuff and rbuff failed!\n", __func__);  
        free_pages((unsigned long)addrpage, get_order(hexfile_size));
        kfree(flashbuff);
        kfree(binbuff);
        return ret;
    }

    ret = true;
    
    free_pages((unsigned long)addrpage, get_order(hexfile_size));
    kfree(flashbuff);
    kfree(binbuff);

    return ret;
}



/*
erase all the flash and eeprom space
*/
bool do_avr_chip_erase(bool skip_eeprom)
{
    bool ret = false;
    avrcmd_t cmd;
    char *bkbuff = NULL;
    int eepsize = 0;

    if(true == skip_eeprom)
    {
        eepsize = get_mcu_eeprom_size(ATMEGA644PA);
        bkbuff = kzalloc(eepsize, GFP_KERNEL);
        if(NULL == bkbuff)
        {
            return ret;
        }

        if(false == read_avr_eeprom_to_buff(bkbuff, eepsize))
        {
            kfree(bkbuff);
            return ret;
        }
    }

    cmd.byte1 = 0xac;// erase chip
    cmd.byte2 = 0x80;
    cmd.byte3 = 0;
    cmd.byte4 = 0;
    if(0 == spi_write(spidev, &cmd, sizeof(avrcmd_t)))
    {
        mdelay(15); 
        ret = true;
    }
    else
    {
        kfree(bkbuff);
        return ret; 
    }

    if(true == skip_eeprom)
    {
        if(false == write_buff_to_avr_eeprom(bkbuff, eepsize))
        {
            dbgisp(DEBUG, "%s: restor avr eeprom failed!\n", __func__);
        }
        else
        {
            ret = true;
        }
        
        kfree(bkbuff);
    }
    
    return ret;
}


bool search_hexfile_path(char *source_str, char *hexpath)
{
    bool ret = false;
    char *tmp = NULL;
    
    if(NULL == source_str || NULL == hexpath)
    {
        return ret;
    }

    tmp = strstr(source_str, "hexfile = ");
    if(NULL != tmp)
    {
        if(0 < sscanf(tmp, "hexfile = %s", hexpath))
            ret = true;
    }

    return ret;
}

bool search_eepfile_path(char *source_str, char *eeppath, bool *skip)
{
    bool ret = false;
    char *tmp = NULL;
    
    if(NULL == source_str || NULL == eeppath || NULL == skip)
    {
        return ret;
    }

    *skip = false;

    if(strstr(source_str, "#eepfile = "))
    {
        dbgisp(DEBUG, "%s: found #, skip it!\n", __func__);
        *skip = true;
        return ret;
    }
    
    tmp = strstr(source_str, "eepfile = ");
    if(NULL != tmp)
    {
        if(0 < sscanf(tmp, "eepfile = %s", eeppath))
            ret = true;
    }
    else
    {
        dbgisp(DEBUG, "%s: nothing, skip it!\n", __func__);
        *skip = true;
    }

    return ret;
}


bool search_fuse_low_value(char *source_str, char *fuse, bool *skip)
{
    bool ret = false;
    char *tmp = NULL;
    unsigned int value;
    
    if(NULL == source_str || NULL == fuse || NULL == skip)
    {
        return ret;
    }

    *skip = false;

    if(strstr(source_str, "#fuselow = "))
    {
        dbgisp(DEBUG, "%s: found #, skip it!\n", __func__);
        *skip = true;
        return ret;
    }
    
    tmp = strstr(source_str, "fuselow = ");
    if(NULL != tmp)
    {
        if(0 < sscanf(tmp, "fuselow = %x", &value))
        {
            ret = true;
            *fuse = value & 0xff;
        }
    }
    else
    {
        dbgisp(DEBUG, "%s: nothing, skip it!\n", __func__);
        *skip = true;
    }

    return ret;
}


bool search_fuse_high_value(char *source_str, char *fuse, bool *skip)
{
    bool ret = false;
    char *tmp = NULL;
    unsigned int value;
    
    if(NULL == source_str || NULL == fuse || NULL == skip)
    {
        return ret;
    }

    *skip = false;

    if(strstr(source_str, "#fusehigh = "))
    {
        dbgisp(DEBUG, "%s: found #, skip it!\n", __func__);
        *skip = true;
        return ret;
    }
    
    tmp = strstr(source_str, "fusehigh = ");
    if(NULL != tmp)
    {
        if(0 < sscanf(tmp, "fusehigh = %x", &value))
        {
            ret = true;
            *fuse = value & 0xff;
        }
    }
    else
    {
        dbgisp(DEBUG, "%s: nothing, skip it!\n", __func__);
        *skip = true;
    }

    return ret;
}


bool search_fuse_ext_value(char *source_str, char *fuse, bool *skip)
{
    bool ret = false;
    char *tmp = NULL;
    unsigned int value;
    
    if(NULL == source_str || NULL == fuse || NULL == skip)
    {
        return ret;
    }

    *skip = false;

    if(strstr(source_str, "#fuseext = "))
    {
        dbgisp(DEBUG, "%s: found #, skip it!\n", __func__);
        *skip = true;
        return ret;
    }
    
    tmp = strstr(source_str, "fuseext = ");
    if(NULL != tmp)
    {
        if(0 < sscanf(tmp, "fuseext = %x", &value))
        {
            ret = true;
            *fuse = value & 0xff;
        }
    }
    else
    {
        dbgisp(DEBUG, "%s: nothing, skip it!\n", __func__);
        *skip = true;
    }

    return ret;
}


bool do_analyse_cfg_file(char *path)
{
    bool ret = false;
    bool result = false;
    char *buff = NULL;
    unsigned int size;
    
    if(NULL == path)
    {
        dbgisp(DEBUG, "%s: path is NULL!\n", __func__);
        return ret;
    }

    /* alloc mem for buff */
    buff = kzalloc(CFG_MAX_SIZE, GFP_KERNEL);
    if(NULL == buff)
    {
        dbgisp(DEBUG, "%s: alloc memory for buff failed!\n", __func__);
        return ret;
    }

    /* start to read CFG file */
    size = read_file_to_buff(path, buff, CFG_MAX_SIZE);
    if(0 >= size)
    {
        dbgisp(DEBUG, "%s: read failed!\n", __func__);
        kfree(buff);
        return ret;
    }
   
    /* start to search path and fuse value */
    result = search_fuse_ext_value(buff, &fuseext, &skip_fuseext);
    if(false == result && false == skip_fuseext)
    {
        dbgisp(DEBUG, "%s: get fuse extend value failed!\n", __func__);
        goto out;
    }

    result = search_fuse_high_value(buff, &fusehigh, &skip_fusehigh);
    if(false == result && false == skip_fusehigh)
    {
        dbgisp(DEBUG, "%s: get fuse high value failed!\n", __func__);
        goto out;
    }

    result = search_fuse_low_value(buff, &fuselow, &skip_fuselow);
    if(false == result && false == skip_fuselow)
    {
        dbgisp(DEBUG, "%s: get fuse low value failed!\n", __func__);
        goto out;
    }
    
    if(false == search_hexfile_path(buff, hexfile_path))
    {
        dbgisp(DEBUG, "%s: get hex file path failed!\n", __func__);
        goto out;
    }

    result = search_eepfile_path(buff, eepfile_path, &skip_eeprom);
    if(false == result && false == skip_eeprom)
    {
        dbgisp(DEBUG, "%s: get eeprom file path failed!\n", __func__);
        goto out;
    }
    
    ret = true;

out:

    kfree(buff);
    return ret; 
}


ssize_t ispproc_read(struct file *filp, char __user *usrbuf, size_t size, loff_t *offset)
{
    char buff[500];
    char tmp[100];
    char tmplen = 0;
    int index = 0;
    
    if(0 != *offset)
        return 0;
        
    memset(buff, '\0', 500);
    
    memset(tmp, '\0', 100);    
    sprintf(tmp, "debug = %s\n", dbglevel == true ? "on" : "off");
    tmplen = strlen(tmp);
    memcpy(buff + index, tmp, tmplen);
    index += tmplen;
    
    memset(tmp, '\0', 100);    
    sprintf(tmp, "isp_state = %s\n", isp_state_string[cur_isp_state]);
    tmplen = strlen(tmp);
    memcpy(buff + index, tmp, tmplen);
    index += tmplen;
    
    memset(tmp, '\0', 100);    
    sprintf(tmp, "verify_state = %s\n", isp_state_string[verify_state]);
    tmplen = strlen(tmp);
    memcpy(buff + index, tmp, tmplen);
    index += tmplen;
    
    if(true == do_analyse_cfg_file(cfgfile_path))
    {    
        if(false == skip_fuselow)
        {
            memset(tmp, '\0', 100);    
            sprintf(tmp, "fuselow = 0x%x\n", fuselow);
            tmplen = strlen(tmp);
            memcpy(buff + index, tmp, tmplen);
            index += tmplen;
        }
        else
        {
            tmplen = strlen("fuselow .... skip\n");
            memcpy(buff + index, "fuselow .... skip\n", tmplen);
            index += tmplen;
        }
            
        if(false == skip_fusehigh)
        {
            memset(tmp, '\0', 100);    
            sprintf(tmp, "fusehigh = 0x%x\n", fusehigh);
            tmplen = strlen(tmp);
            memcpy(buff + index, tmp, tmplen);
            index += tmplen;
        }
        else
        {
            tmplen = strlen("fusehigh ... skip\n");
            memcpy(buff + index, "fusehigh ... skip\n", tmplen);
            index += tmplen;
        }
        
        if(false == skip_fuseext)
        {
            memset(tmp, '\0', 100);    
            sprintf(tmp, "fuseext = 0x%x\n", fuseext);
            tmplen = strlen(tmp);
            memcpy(buff + index, tmp, tmplen);
            index += tmplen;
        }
        else
        {
            tmplen = strlen("fuseext .... skip\n");
            memcpy(buff + index, "fuseext .... skip\n", tmplen);
            index += tmplen;
        }
        
        memset(tmp, '\0', 100);    
        sprintf(tmp, "hexfile = %s\n", hexfile_path);
        tmplen = strlen(tmp);
        memcpy(buff + index, tmp, tmplen);
        index += tmplen;
        
        if(false == skip_eeprom)
        {
            memset(tmp, '\0', 100);    
            sprintf(tmp, "eepfile = %s\n", eepfile_path);
            tmplen = strlen(tmp);
            memcpy(buff + index, tmp, tmplen);
            index += tmplen;
        }
        else
        {
            tmplen = strlen("eepfile ... skip\n");
            memcpy(buff + index, "eepfile ... skip\n", tmplen);
            index += tmplen;
        }
    }
    else
    {
        tmplen = strlen("analyse cfgfile failed\n");
        memcpy(buff + index, "analyse cfgfile failed\n", tmplen);
        index += tmplen;
    }

    
    memset(tmp, '\0', 100);    
    sprintf(tmp, "cfgfile = %s\n", cfgfile_path);
    tmplen = strlen(tmp);
    memcpy(buff + index, tmp, tmplen);
    index += tmplen;
    
    buff[index++] = '\n';


    index = index < size ? index : size;
    *offset += index;
    
    if(0 != copy_to_user(usrbuf, buff, index))
    {
        index = 0;
    }
    
    
	return index;
}


static ssize_t ispproc_write(struct file *filp, const char __user *usr_buf,
                             size_t count, loff_t *f_pos)
{
#if 1
    char len;
    char buff[100] = {0};

    len = count < 100 ? count : 99;
    if(0 != copy_from_user(buff, usr_buf, len))
    {
        goto out;
    }
    
    buff[99] = '\0';

    if(sizeof("doisp") == count && 0 == strncmp(buff, "doisp", 5))
    {
        dbgisp(NORMAL, "\n");
        cur_isp_state = ISP_STATE_DOING;
        dbgisp(NORMAL, "start to analyse cfg file.....");
        if(true == do_analyse_cfg_file(cfgfile_path))
        {
            //dbgisp(NORMAL, "fuselow  = 0x%x\n", fuselow);
            //dbgisp(NORMAL, "fusehigh = 0x%x\n", fusehigh);
            //dbgisp(NORMAL, "fuseext  = 0x%x\n", fuseext);
            //dbgisp(NORMAL, "hexfile = %s\n", hexfile_path);
            //dbgisp(NORMAL, "eepfile = %s\n", eepfile_path);
            dbgisp(NORMAL, "done\n");
        }        
        else
        {            
            dbgisp(NORMAL, "fail\n");
            cur_isp_state = ISP_STATE_FAIL;
            goto out;
        }       
        
        dbgisp(NORMAL, "start to enter isp mode ......");
        if(false == do_enter_avr_isp_mode())
        {
            dbgisp(NORMAL, "fail\n");
            cur_isp_state = ISP_STATE_FAIL;
            //dbgisp(DEBUG, "%s: enter avr isp mode failed!\n", __func__);
            goto out;
        }
        else
        {
            dbgisp(NORMAL, "done\n");
        }
        
        dbgisp(NORMAL, "start to erash chip ..........");
        if(false == do_avr_chip_erase(skip_eeprom))
        {
            dbgisp(NORMAL, "fail\n");
            cur_isp_state = ISP_STATE_FAIL;
            //dbgisp(DEBUG, "%s: do avr chip erase failed!\n", __func__);
            goto out;
        }
        else
        {
            dbgisp(NORMAL, "done\n");
        }
        
        dbgisp(NORMAL, "start to download hex file ...");
        if(false == do_avr_in_system_program(ATMEGA644PA, hexfile_path))
        {
            dbgisp(NORMAL, "fail\n");
            cur_isp_state = ISP_STATE_FAIL;
            //dbgisp(DEBUG, "%s: do avr isp download failed!\n", __func__);
            goto out;
        }
        else
        {
            dbgisp(NORMAL, "done\n");
        }
        
        dbgisp(NORMAL, "start to download eep file ...");
        if(true == skip_eeprom)
        {
            dbgisp(NORMAL, "skip\n");
        }
        else if(false == do_avr_eeprom_program(ATMEGA644PA, eepfile_path))
        {
            dbgisp(NORMAL, "fail\n");
            cur_isp_state = ISP_STATE_FAIL;
            //dbgisp(DEBUG, "%s: do avr eeprom program failed!\n", __func__);
            goto out;
        }
        else
        {
            dbgisp(NORMAL, "done\n");
        }
        
        #if 0
        dbgisp(NORMAL, "start to write fuse ..........");
        if(false == do_avr_fuse_program(fuselow, fusehigh, fuseext))
        {
            dbgisp(NORMAL, "fail\n");
            cur_isp_state = ISP_STATE_FAIL;
            //dbgisp(DEBUG, "%s: do avr fuse program failed!\n", __func__);
            goto out;
        }
        else
        {
            dbgisp(NORMAL, "done\n");
        }
        #else
        dbgisp(NORMAL, "start to write fuse low ......");
        if(true == skip_fuselow)
        {
            dbgisp(NORMAL, "skip\n");
        }
        else if(false == do_avr_fuselow_program(fuselow))
        {
            dbgisp(NORMAL, "fail\n");
            cur_isp_state = ISP_STATE_FAIL;
            //dbgisp(DEBUG, "%s: do avr fuse program failed!\n", __func__);
            goto out;
        }
        else
        {
            dbgisp(NORMAL, "done\n");
        }
        
        dbgisp(NORMAL, "start to write fuse high .....");
        if(true == skip_fusehigh)
        {
            dbgisp(NORMAL, "skip\n");
        }
        else if(false == do_avr_fusehigh_program(fusehigh))
        {
            dbgisp(NORMAL, "fail\n");
            cur_isp_state = ISP_STATE_FAIL;
            //dbgisp(DEBUG, "%s: do avr fuse program failed!\n", __func__);
            goto out;
        }
        else
        {
            dbgisp(NORMAL, "done\n");
        }
        
        dbgisp(NORMAL, "start to write fuse ext ......");
        if(true == skip_fuseext)
        {
            dbgisp(NORMAL, "skip\n");
        }
        else if(false == do_avr_fuseext_program(fuseext))
        {
            dbgisp(NORMAL, "fail\n");
            cur_isp_state = ISP_STATE_FAIL;
            //dbgisp(DEBUG, "%s: do avr fuse program failed!\n", __func__);
            goto out;
        }
        else
        {
            dbgisp(NORMAL, "done\n");
        }
        #endif
        
        //dbgisp(NORMAL, "isp download success!");
        dbgisp(NORMAL, "\n");
        cur_isp_state = ISP_STATE_DONE;
        
        //do_leave_isp_mode();
    }
    #if 0
    else if(sizeof("verify") == count && 0 == strncmp(buff, "verify", 6))
    {
        dbgisp(NORMAL, "\n");
        verify_state = ISP_STATE_DOING;
        dbgisp(NORMAL, "start to analyse cfg file.....");
        if(true == do_analyse_cfg_file(cfgfile_path))
        {
            //dbgisp(NORMAL, "fuselow  = 0x%x\n", fuselow);
            //dbgisp(NORMAL, "fusehigh = 0x%x\n", fusehigh);
            //dbgisp(NORMAL, "fuseext  = 0x%x\n", fuseext);
            //dbgisp(NORMAL, "hexfile = %s\n", hexfile_path);
            //dbgisp(NORMAL, "eepfile = %s\n", eepfile_path);
            dbgisp(NORMAL, "done\n");
        }        
        else
        {            
            verify_state = ISP_STATE_FAIL;
            dbgisp(NORMAL, "fail\n");
            goto out;
        }
        
        dbgisp(NORMAL, "start to enter isp mode ......");
        if(false == do_enter_avr_isp_mode())
        {
            verify_state = ISP_STATE_FAIL;
            dbgisp(NORMAL, "fail\n");
            //dbgisp(DEBUG, "%s: enter avr isp mode failed!\n", __func__);
            goto out;
        }
        else
        {
            dbgisp(NORMAL, "done\n");
        }        
        
        dbgisp(NORMAL, "start to verify flash ........");
        if(false == do_avr_flash_verify(ATMEGA644PA, hexfile_path))
        {
            verify_state = ISP_STATE_FAIL;
            dbgisp(NORMAL, "fail\n");
            //dbgisp(DEBUG, "%s: do avr isp download failed!\n", __func__);
            goto out;
        }
        else
        {
            dbgisp(NORMAL, "done\n");
        }

        verify_state = ISP_STATE_DONE;
        
        //dbgisp(NORMAL, "isp download success!");
        dbgisp(NORMAL, "\n");
    }
    #endif
    else if(sizeof("dbgon") == count && 0 == strncmp(buff, "dbgon", 5))
    {
        dbglevel = true;
    }
    else if(sizeof("dbgoff") == count && 0 == strncmp(buff, "dbgoff", 6))
    {
        dbglevel = false;
    }
    else if(sizeof("reset") == count && 0 == strncmp(buff, "reset", 5))
    {
        inISP_mode = false;
        gpio_set_value(RESET_AVR_GPIO, 1);
        mdelay(5);
        gpio_set_value(RESET_AVR_GPIO, 0);
        mdelay(10);
        gpio_set_value(RESET_AVR_GPIO, 1);
    }
    else if('/' == buff[0])
    {   
        //dbgisp(DEBUG, "old cfg path: %s\n", cfgfile_path);
        dbgisp(NORMAL, "\n");
        if(0 < sscanf(buff, "%s", cfgfile_path))
            dbgisp(NORMAL, "cfgfile = %s\n", cfgfile_path);
        else
        {
            dbgisp(NORMAL, "set new cfgfile failed\n");
        }
        dbgisp(NORMAL, "\n");
        
    }
    else
    {
        dbgisp(NORMAL, help_string);
    }

out:    

#else
    //unsigned long size = 0;
    //char *buff = NULL;
    //buff = kzalloc(65535, GFP_KERNEL);
    //size = read_file_to_buff("/mnt/nfs/file.hex", buff, 90000);
    //dbgisp(DEBUG, "size = 0x%04x\n", size);
#endif

    return count;
}


static const struct  file_operations ispproc_op = {
    .read = ispproc_read,
    .write = ispproc_write,
};

static ssize_t ispdev_write(struct file *filp, const char __user *usr_buf,
                            size_t count, loff_t *f_pos)
{ 
    return count;
}

static ssize_t ispdev_read(struct file *filp, char __user *buf, 
                           size_t count, loff_t *f_pos)
{
    unsigned char * buff;

    if(*f_pos > filp->f_inode->i_size)
        return 0;
    
    dbgisp(DEBUG, "%s!\n", __func__);    
    buff = kzalloc(count, GFP_KERNEL);
    snprintf(buff,20,"%s","hello world!");
    //copy_to_user(buf, buff, count);
    dbgisp(DEBUG, "%s\n", buff);
    kfree(buff);
    *f_pos += 20; 
	return count;
}

static int ispdev_open(struct inode *inode, struct file *filp)
{
    dbgisp(DEBUG, "%s!\n", __func__);
    return 0;
}

static int ispdev_release(struct inode *inode, struct file *filp)
{
    dbgisp(DEBUG, "%s!\n", __func__);
    return 0;
}


static const struct file_operations ispdev_fops = {
	.owner =	THIS_MODULE,
	.write =	ispdev_write,
	.read =		ispdev_read,
	.open =		ispdev_open,
	.release =	ispdev_release,
};


static int atmel_probe(struct spi_device *spi)
{
    if (NULL == spi)
    {
        dbgisp(DEBUG, "Err: struct spi_device *spi is null!\n");
        return 0;
    }
    else
    {
        spidev = spi;
    }
    
    dbgisp(DEBUG, "hello atmel isp modules!\n");
    
    if(0 > gpio_request(RESET_AVR_GPIO, "gpio_reset_avr\n")) // gpio2_26
    {
        dbgisp(DEBUG, "Err: request GPIO for reset avr failed!\n");
        return 0;
    }
    
    /* create proc file /proc/PROC_ISP_NAME */
    if (NULL == proc_create(ISP_PROC_NAME, 0755, NULL, &ispproc_op))    
    {
        dbgisp(DEBUG, "Err: create /proc/%s failed!", ISP_PROC_NAME);
        gpio_free(RESET_AVR_GPIO);
        return 0;
    }
    
#if ISP_CHAR_DEV

    isp_major_num = register_chrdev(0, ISP_DEV_NAME, &ispdev_fops);
    if (0 > isp_major_num)
    {
        dbgisp(DEBUG, "Err: register char dev %s failed!\n", ISP_DEV_NAME);
        remove_proc_entry(ISP_PROC_NAME, NULL);
        gpio_free(RESET_AVR_GPIO);
        return 0;
    }
    
    isp_devt = MKDEV(isp_major_num, 0);

    /* create class /sys/device/CLASS_ISP_NAME */
    atmel_class = class_create(THIS_MODULE, ISP_CLASS_NAME);
    if (NULL == atmel_class || IS_ERR(atmel_class))
    {
        dbgisp(DEBUG, "Err: create class %s failed!\n", ISP_CLASS_NAME);
        remove_proc_entry(ISP_PROC_NAME, NULL);        
        unregister_chrdev(isp_major_num, ISP_DEV_NAME);
        gpio_free(RESET_AVR_GPIO);
        return 0;
    }
    
    dev = device_create(atmel_class, NULL, isp_devt, NULL, ISP_DEV_NAME);    
    if (NULL == dev || IS_ERR(dev))
    {
        dbgisp(DEBUG, "Err: create sysfs dev failed!\n");
        remove_proc_entry(ISP_PROC_NAME, NULL);        
        unregister_chrdev(isp_major_num, ISP_DEV_NAME);
	    class_destroy(atmel_class);
        gpio_free(RESET_AVR_GPIO);
        return 0;
    }
    
#endif

	return 0;
}


static int atmel_remove(struct spi_device *spi)
{
	dbgisp(DEBUG, "bye atmel isp modules!\n");
	remove_proc_entry(ISP_PROC_NAME, NULL);
    gpio_free(RESET_AVR_GPIO);
    
#if ISP_CHAR_DEV
    device_destroy(atmel_class, isp_devt);
	class_destroy(atmel_class);
    unregister_chrdev(isp_major_num, ISP_DEV_NAME);
#endif

	return 0;
}


static struct spi_driver atmel_isp_driver = {
	.driver = {
		.name	= "atmel_isp",
		.owner	= THIS_MODULE,
	},
	.id_table	= atmel_ids,
	.probe	= atmel_probe,
	.remove	= atmel_remove,
};

module_spi_driver(atmel_isp_driver);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("hu yongfa 2015-01-19");
MODULE_DESCRIPTION("ISP driver for atmega644pa");
