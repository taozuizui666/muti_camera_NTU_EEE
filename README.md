## author: TAO RAN

1. 完整的工程设备树插件我已经提供，在主目录的DTS目录下。如果要拓展或者移除功能，在对应的不同设备目录下，我只提供了dts文件，需要编译成dtbo文件才能变成设备树补丁，具体编译步骤参考《[野火]Linux驱动开发指南》的设备树插件章节，我连同开发指南一起上传github了
2. dtbo文件要放在开发板的/boot/dtb/overlay 文件夹下面，并且要修改/boot/uEnv/uEnv.txt 文件，把对应的dtbo文件路径加进去： dtoverlay=/dtb/overlay/”filename“.dtbo

### note
* 通常datasheet/doc文件夹下是该设备的产品手册
* 这里设备树补丁和设备树插件都指代——dtbo

## WIFI_module
此文件夹下有4个子文件夹： BL616_HostDriver、FW、doc、usb。对于rk3588开发板我已经交叉编译好了WIFI的usb驱动，参考doc里面的Linux驱动移植手册，将FW里面的对应bin文件放在开发板的/lib/firmware目录下面，再挂载ko驱动即可连上wifi；对于其他型号的CPU请自行交叉编译，注意官方提供的SDK里的驱动程序用的函数是基于linux kernel 6 版本的，如果用的kernel是5 版本会出现内核函数不适配的情况。
 
## IMU
目前IMU文件夹下有3个子文件夹： datasheet、iio_device、spi_device。 拿到IMU设备需要先测试功能是否正常，建议按照下面两个文件夹的顺序测试，先测试SPI通信是否成功，再测试IIO的方式
* spi_device文件夹对应使用kernel默认spidev的驱动，主要用于测试IMU设备是否能正常工作，以及精度不高的ROS2节点用作采集IMU的数据
* iio_device文件夹对应使用我编写的iio_imu_drv.ko驱动，设备树插件的compatible做出了对应的修改。iio（Industrial I/O）是kernel提供的专门用于处理各类传感器和数据采集设备的子系统，主要处理ADC、IMU等，选择iio是因为他提供了一套标准化接口 + 数据模型，具体核心原理还没有时间仔细研究。通过iio方式创建的设备在/sys/bus/iio/devices/iio:device0/ ..iio:device1/等，正常情况iio:device0是ADC,iio:device1是我们的IMU。我也提供了imu_read.c程序，编译好了之后可以在开发板上运行，测试设备节点地址是否正确

## camera_hongwai

## DTS

## SD卡复制
考虑到后期的多款产品用相似的系统，需要复制同一张SD卡，这里单独用一个文件夹来提供相关手段。SD卡容量如果过大需要压缩之后做成镜像拷贝，否则很容易超出电脑容量。
1. SD卡压缩。可以使用gparted,如果用parted,可以参考我的“压缩_扩容”文件
2. 镜像烧录到新的SD卡，下载balenaEtcher或者其他软件，URL=https://etcher.balena.io/#download-etcher

## scripts
此文件夹下是常用的脚本指令集合
1. Linux紧急恢复： 如果设备树配置错误（包括uEnv中的dtbo文件名写错，还有dtb复制进/boot/dtb/中需要等待5秒以上，否则刚用完cp指令就拔电dtb文件也是损坏的），会导致开发板进不去系统，我整理了有3种方式进行拯救：
   * 如果系统是在SD卡上，可以把SD卡用读卡器接入Linux系统的电脑，找到/boot分区对/dtb中的设备树进行修改
   * 如果系统是在SD卡上并且eMMC中有备用系统，可以先拔掉SD卡用eMMC进入系统，再插入SD卡挂载到/mnt目录下进行修改
   * 如果系统是在SD卡上并且eMMC中有备用系统，此时在紧急系统中按这个文件里的指令正常进入系统
   * 如果系统在eMMC中，可以插入带系统的SD卡，操作原理和上面类似
2. 网卡配置： 用网线连接开发板和电脑时可以用此方式固定静态IP, 有wifi模块之后就可以不用这种方式了
3. camera_hongwai_i2c指令： camera_hongwai摄像头用i2c和MPU进行通信配置，camera_hongwai的datasheet里面有对应的i2c指令，我这里整理出linux自带的i2ctransfer方法完成一些简单的配置和读取信息操作
