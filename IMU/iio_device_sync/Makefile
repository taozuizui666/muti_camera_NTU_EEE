# SPDX-License-Identifier: BSD-3-Clause
#
# Kbuild file for the Murata SCH16T IIO kernel driver.
#
# In-tree placement:  drivers/iio/imu/sch16t/
#
# Out-of-tree build (e.g. from this directory):
#   make -C /lib/modules/$(uname -r)/build M=$(pwd) modules
#   make -C /lib/modules/$(uname -r)/build M=$(pwd) modules_install
#   depmod -a
#   modprobe sch16t

# obj-$(CONFIG_SCH16T) += sch16t.o

# 目标模块
obj-m := sch16t.o

# 内核源码路径（必须是已经编译过的内核）
KDIR = /home/computer/LubanCat_SDK/kernel-6.1

# 当前目录
PWD := $(shell pwd)

# 交叉编译工具链
ARCH = arm64
CROSS_COMPILE = /home/computer/LubanCat_SDK/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin/aarch64-none-linux-gnu-

# 默认目标
all:
	$(MAKE) -C $(KDIR) M=$(PWD) \
	ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) \
	modules

# 安装模块
modules_install:
	$(MAKE) -C $(KDIR) M=$(PWD) \
	ARCH=$(ARCH) CROSS_COMPILE=$(CROSS_COMPILE) \
	modules_install

# 清理
clean:
	$(MAKE) -C $(KDIR) M=$(PWD) clean
