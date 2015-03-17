
obj-m := isp.o  
KERNELDR := /home/hu/linux-3.10.17 
#KERNELDR := /home/hu/linux-3.14.29
PWD := $(shell pwd)  
modules:  
	$(MAKE) -C $(KERNELDR) M=$(PWD) modules  
moduels_install:  
	$(MAKE) -C $(KERNELDR) M=$(PWD) modules_install  
    
clean:
	rm -rf *.o  *.symvers *.order .depend .*.cmd *.ko *.mod.c .tmp_versions
