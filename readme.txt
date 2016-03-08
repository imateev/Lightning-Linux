
1. device tree is ok
2. disk name bind ok
3. eth0 reset     ok
4. usb config0    ok
5. usb audio card reset ok
6. hub enable     ok
7. hdd enalbe     ok
8. panic log      ok


========== 2016-02-29 ==========
9. filelist       ok
10. hfsplus       ok
11. mmc static id ok
12. dac read file ok

========== 2016-03-01 ==========
 driver/dma/imx-sdma.c  raw_spinlock_t channel_0_lock  ok
 drivers/net/ethernet/freescale/fec_main.c  fec_timeout improve  not needed
 driver/pci/host/pci-imx6.c  pcie gpio reset ok
 net/mac80211/Makefile  KBUILD_EXTRA_SYMBOLS=/Linux/net/wireless/Module.symvers not needed
 sound/usb/quirks.c snd_usb_interface_dsd_format_quirks  for dsd256 ok
 

========== 2016-03-04 ==========
 driver/auralic/auralic_panic.c write is ok   writed=fp->f_op->write_iter(&kiocb, &iter);
 imx6 pcie reboot hang fixed             ok   aura_imx6_pcie_shutdown
 exfat build into kernel                 ok
 arch/arm/Kconfig for snd-audio-mytek    ok   #select HAVE_ARCH_BITREVERSE if (CPU_32v7M || CPU_32v7) && !CPU_32v6
 add pcie shutdown in kernel_restart     ok   



================================
rootfs port list
================================
1. usb interrupt change from 72 to 280 in /etc/rc5/S99z.sh  
2. add ll vim tailf iperf3 ... 
