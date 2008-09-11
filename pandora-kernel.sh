#!/bin/sh
make CROSS_COMPILE=arm-none-linux-gnueabi- distclean 
make CROSS_COMPILE=arm-none-linux-gnueabi- omap3_pandora_defconfig
#make CROSS_COMPILE=arm-none-linux-gnueabi- menuconfig
make CROSS_COMPILE=arm-none-linux-gnueabi- uImage
cp arch/arm/boot/uImage ../uImage

