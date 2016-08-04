# DM8148  
all resources for TMS320DM8148 development  
check links below for EZSDK,toolchain etc  
http://processors.wiki.ti.com/index.php/Category:EZSDK  
http://software-dl.ti.com/dsps/dsps_public_sw/ezsdk/latest/index_FDS.html   
porting guide:  
http://processors.wiki.ti.com/index.php/TI81xx_PSP_Porting_Guide#Updating_U-Boot_to_use_different_UART_as_console  
make kernel:  
make ARCH=arm CROSS_COMPILE=/home/user/CodeSourcery/Sourcery_G++_Lite/bin/arm-none-linux-gnueabi-  tisdk_dm814x-evm_defconfig  
make ARCH=arm CROSS_COMPILE=/home/user/CodeSourcery/Sourcery_G++_Lite/bin/arm-none-linux-gnueabi-  uImage 


