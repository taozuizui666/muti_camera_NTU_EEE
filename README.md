对于rk3588开发板：

完整的工程设备树插件我已经提供，在主目录的DTS目录下。如果要拓展或者移除功能，在对应的不同设备目录下，我只提供了dts文件，需要编译成dtbo文件才能变成设备树补丁，具体编译步骤参考《[野火]Linux驱动开发指南》的设备树插件章节，我连同开发指南一起上传github了
dtbo文件要放在开发板的/boot/dtb/overlay 文件夹下面，并且要修改/boot/uEnv/uEnv.txt 文件，把对应的dtbo文件路径加进去： dtoverlay=/dtb/overlay/”filename“.dtbo
