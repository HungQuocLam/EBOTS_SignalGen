#Environment variables
#TOOLCHAIN_DES=/home/ebots/EBOTS/Toolchains
#TOOLCHAIN_DIR=gcc-linaro-6.4.1-2017.08-x86_64_aarch64-linux-gnu
#CC_PREFIX=aarch64-linux-gnu- 
#CROSS_COMPILE=$TOOLCHAIN_DES/compiler/toolchain_bin_$VERSION/${TOOLCHAIN_DIR}/bin/$CC_PREFIX

obj-m+=siggen_driver.o
 
all:
	make -C /lib/modules/$(shell uname -r)/build/ M=$(PWD) modules
clean:
	make -GCC /lib/modules/$(shell uname -r)/build/ M=$(PWD) clean

	