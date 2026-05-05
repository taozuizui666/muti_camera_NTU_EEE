对于rk3588开发板：
dtbo文件要放在开发板的/boot/dtb/overlay 文件夹下面，并且要修改/boot/uEnv/uEnv.txt 文件，把对应的dtbo文件路径加进去： dtoverlay=/dtb/overlay/”filename“.dtbo
