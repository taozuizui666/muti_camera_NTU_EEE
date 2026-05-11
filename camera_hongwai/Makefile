ARCH = arm64
CROSS_COMPILE = aarch64-none-linux-gnu-
PATH := $(PATH):/home/zui/LubanCat_SDK/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin
KERNEL_DIR = /home/zui/LubanCat_SDK/kernel

export PATH
export CROSS_COMPILE
export ARCH
export KERNEL_DIR

Target_drv = rs300_hongwai_drv
# Target_test = test

$(Target_drv)-y := rs300-mipi.o 
obj-m += $(Target_drv).o

all: 
	make -C $(KERNEL_DIR) M=`pwd` modules 
#	$(CROSS_COMPILE)gcc -o $(Target_test) $(Target_test).c

clean:
	make -C $(KERNEL_DIR) M=`pwd` modules clean
	rm -rf modules.order
#	rm -f $(Target_test)

.PHONY: clean