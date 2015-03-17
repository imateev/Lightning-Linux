xczxz

----------- 2014-08-27 -------------
proc file read ok
char dev ok
read file ok


----------- 2014-08-28 -------------
print buff ok
avr read/write ok


----------- 2014-08-28 -------------
gpio for reset avr is ok


----------- 2014-09-02 -------------
erase flash and eeprom ok
read memory page ok
write memory page ok
verify ok
read fuse ok
write fuse ok
read write eeprom ok
eeprom verify ok


----------- 2014-09-03 -------------
cfg file ok
proc control ok
hex to bin ok
all ok


----------- 2014-09-04 -------------
use alloc_pages for hexbuff ok
use dbg_isp for printk ok
set a switch for char dev ok
change eeprom file from hex to bin file ok


----------- 2014-09-05 -------------
use dbgisp replace of dbg_isp ok
fix first spi_write length from 2 to 1 ok
user # or empty for cfg file  ok
skip eeprom ok
cat /proc/atmel   ok


----------- 2014-09-025 -------------
add isp_state, can be check by cat /proc/atmel ok


----------- 2014-12-18 -------------
once enter isp_mode, allow take many time doisp 




