#!/bin/sh
EXTERN_LIB_CMAKE=extern_lib.cmake

echo "set(CMAKE_SYSTEM_NAME Linux)" >> $EXTERN_LIB_CMAKE
echo "set(CMAKE_C_FLAGS "-I/include")" >> $EXTERN_LIB_CMAKE
echo "set(CMAKE_CXX_COMPILER /home/public/tool_chain/gcc-arm-8.3-2019.03-x86_64-arm-linux-gnueabihf//bin/arm-linux-gnueabihf-g++)" >> $EXTERN_LIB_CMAKE
echo "set(CMAKE_FIND_ROOT_PATH )" >> $EXTERN_LIB_CMAKE
echo "set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)" >> $EXTERN_LIB_CMAKE
echo "set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)" >> $EXTERN_LIB_CMAKE
echo "set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)" >> $EXTERN_LIB_CMAKE
echo "list(APPEND CMAKE_PREFIX_PATH )" >> $EXTERN_LIB_CMAKE

mkdir build
cd build
cmake ..
make