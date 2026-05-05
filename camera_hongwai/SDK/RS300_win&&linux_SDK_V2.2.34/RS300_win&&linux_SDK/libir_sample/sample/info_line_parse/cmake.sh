
#CMakeLists.txt
CLS=$1
PROJECT=.
prefab=prefab
OUT=out
#sdk NDK

NDK=//home/jenkins/Android/sdk/ndk/21.1.6352462
AndroidVer=24
#API平台
TARGET='arm64-v8a'

#ִcmake
for I in $TARGET 
    do
BUILD=$I
ABI=$I
cmake   \
-H$PROJECT \
-DCMAKE_FIND_ROOT_PATH=$prefab \
-DCMAKE_BUILD_TYPE=Release \
-DCMAKE_TOOLCHAIN_FILE=$NDK/build/cmake/android.toolchain.cmake \
-DANDROID_ABI=$ABI \
-DANDROID_NDK=$NDK \
-DANDROID_PLATFORM=android-$AndroidVer \
-DCMAKE_ANDROID_ARCH_ABI=$ABI \
-DCMAKE_ANDROID_NDK=$NDK \
-DCMAKE_EXPORT_COMPILE_COMMANDS=ON \
-DCMAKE_LIBRARY_OUTPUT_DIRECTORY=$OUT   \
-DCMAKE_RUNTIME_OUTPUT_DIRECTORY=$OUT   \
-DCMAKE_SYSTEM_NAME=Android \
-DCMAKE_SYSTEM_VERSION=$AndroidVer  \
-B$BUILD   
make -C $BUILD $CLS || exit "$?" 
done
