对于rk3588开发板：

1. 完整的工程设备树插件我已经提供，在主目录的DTS目录下。如果要拓展或者移除功能，在对应的不同设备目录下，我只提供了dts文件，需要编译成dtbo文件才能变成设备树补丁，具体编译步骤参考《[野火]Linux驱动开发指南》的设备树插件章节，我连同开发指南一起上传github了
2. dtbo文件要放在开发板的/boot/dtb/overlay 文件夹下面，并且要修改/boot/uEnv/uEnv.txt 文件，把对应的dtbo文件路径加进去： dtoverlay=/dtb/overlay/”filename“.dtbo

# note
. 通常datasheet文件夹下是该设备的产品手册
. 这里设备树补丁和设备树插件都指代——dtbo
 
## IMU
目前IMU文件夹下有3个子文件夹： datasheet、iio_device、spi_device。 拿到IMU设备需要先测试功能是否正常，建议按照下面两个文件夹的顺序测试，先测试SPI通信是否成功，再测试IIO的方式
. spi_device文件夹对应使用kernel默认spidev的驱动，主要用于测试IMU设备是否能正常工作，以及精度不高的ROS2节点用作采集IMU的数据
. iio_device文件夹对应使用我编写的iio_imu_drv.ko驱动，设备树插件的compatible做出了对应的修改。iio（Industrial I/O）是kernel提供的专门用于处理各类传感器和数据采集设备的子系统，主要处理ADC、IMU等，选择iio是因为他提供了一套标准化接口 + 数据模型，具体核心原理还没有时间仔细研究。通过iio方式创建的设备在/sys/bus/iio/devices/iio:device0/ ..iio:device1/等，正常情况iio:device0是ADC,iio:device1是我们的IMU。我也提供了imu_read.c程序，编译好了之后可以在开发板上运行，测试设备节点地址是否正确
