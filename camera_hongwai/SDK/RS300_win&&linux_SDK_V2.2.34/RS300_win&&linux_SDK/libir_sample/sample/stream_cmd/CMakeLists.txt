PROJECT(ircmd)
CMAKE_MINIMUM_REQUIRED(VERSION 2.6)

set(CMAKE_CXX_STANDARD 11)

set(EXTERN_LIB extern_lib.cmake)
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${EXTERN_LIB})
 file(WRITE ${EXTERN_LIB} "")
endif()
include(${EXTERN_LIB})

include_directories(../../interfaces)
include_directories(../../drivers)
include_directories(../../thirdparty/libdrm/include)
include_directories(../../thirdparty/cJSON/include)
include_directories(../../common)
include_directories(../../other)
include_directories(../../components)
include_directories(./)


option(BUILD32 "Build x86" OFF)
if(BUILD32)
    if(${CMAKE_SYSTEM_NAME} MATCHES "Android")
        message("building x86")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32 -Wno-incompatible-pointer-types -Wmissing-field-initializers -Wunknown-escape-sequence -Wsign-compare")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32 -Wno-incompatible-pointer-types -Wmissing-field-initializers -Wunknown-escape-sequence -Wsign-compare")
    else()
        message("building x86")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32 -Wno-incompatible-pointer-types -Wmissing-field-initializers -Wsign-compare")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32 -Wmissing-field-initializers -Wsign-compare")
    endif()
else()
    if(${CMAKE_SYSTEM_NAME} MATCHES "Android")
        message("building x64")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -Wno-incompatible-pointer-types -Wmissing-field-initializers -Wunknown-escape-sequence -Wsign-compare")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -Wno-incompatible-pointer-types -Wmissing-field-initializers -Wunknown-escape-sequence -Wsign-compare")
    else()
        message("building x64")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -s -Wno-incompatible-pointer-types -Wmissing-field-initializers -Wsign-compare")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -s -Wmissing-field-initializers -Wsign-compare")
    endif()
endif()

link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../drivers)

add_executable(sample 
    ../../common/config.cpp
    ../../common/data.cpp
    ../../common/v4l2_camera.cpp
    ../../common/drm_display.cpp
    ../../components/cmd.cpp
    ./sample.cpp
    ../../thirdparty/libdrm/xf86drm.c
    ../../thirdparty/libdrm/xf86drmHash.c
    ../../thirdparty/libdrm/xf86drmMode.c
    ../../thirdparty/libdrm/xf86drmRandom.c
    ../../thirdparty/libdrm/xf86drmSL.c
    ../../thirdparty/cJSON/src/cJSON.c
    )

if(${CMAKE_SYSTEM_NAME} MATCHES "Android")
target_link_libraries(sample ircmd.a iruart.a irv4l2.a ircam.a iri2c.a irparse.a log -lm)
else()
target_link_libraries(sample ircmd iruart irv4l2 ircam iri2c irparse pthread -lm)
endif()

install(TARGETS sample DESTINATION .)
install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/../../config DESTINATION config)
