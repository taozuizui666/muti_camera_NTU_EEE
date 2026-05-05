#!/bin/sh

EXTERN_LIB_CMAKE=extern_lib.cmake
rm -f ${EXTERN_LIB_CMAKE}

cat << EOF > ${EXTERN_LIB_CMAKE}
set(CMAKE_SYSTEM_NAME Linux)
set(CMAKE_SYSTEM_PROCESSOR aarch64)
set(TOOLCHAIN_PATH /home/zui/LubanCat_SDK/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu/bin)

set(CMAKE_C_COMPILER \${TOOLCHAIN_PATH}/aarch64-none-linux-gnu-gcc)
set(CMAKE_CXX_COMPILER \${TOOLCHAIN_PATH}/aarch64-none-linux-gnu-g++)

set(CMAKE_SYSROOT /home/zui/rk3588-rootfs)

set(CMAKE_FIND_ROOT_PATH \${CMAKE_SYSROOT})

set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
EOF

rm -rf build
mkdir build
cd build

cmake .. \
  -DCMAKE_TOOLCHAIN_FILE=../extern_lib.cmake \
  -DCMAKE_INSTALL_PREFIX=./stream_cmd

make -j$(nproc) && make install

# #!/bin/sh
# EXTERN_LIB_CMAKE=extern_lib.cmake
# rm ${EXTERN_LIB_CMAKE}

# echo "set(CMAKE_SYSTEM_NAME Linux)" >> $EXTERN_LIB_CMAKE
# echo "set(TOOLCHAIN_PATH /home/zui/LubanCat_SDK/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu//bin)" >> $EXTERN_LIB_CMAKE
# echo "set(CMAKE_C_COMPILER /home/zui/LubanCat_SDK/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu//bin/aarch64-none-linux-gnu-gcc)" >> $EXTERN_LIB_CMAKE
# echo "set(CMAKE_C_FLAGS "-I/include")" >> $EXTERN_LIB_CMAKE
# echo "set(CMAKE_CXX_COMPILER /home/zui/LubanCat_SDK/prebuilts/gcc/linux-x86/aarch64/gcc-arm-10.3-2021.07-x86_64-aarch64-none-linux-gnu//bin/aarch64-none-linux-gnu-g++)" >> $EXTERN_LIB_CMAKE
# echo "set(CMAKE_FIND_ROOT_PATH )" >> $EXTERN_LIB_CMAKE
# echo "set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)" >> $EXTERN_LIB_CMAKE
# echo "set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)" >> $EXTERN_LIB_CMAKE
# echo "set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)" >> $EXTERN_LIB_CMAKE
# echo "list(APPEND CMAKE_PREFIX_PATH )" >> $EXTERN_LIB_CMAKE

# mkdir build
# cd build
# cmake .. \
#   -DCMAKE_TOOLCHAIN_FILE=../extern_lib.cmake \
#   -DCMAKE_INSTALL_PREFIX=./stream_cmd
# make && make install

