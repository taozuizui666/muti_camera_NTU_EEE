PROJECT(upgrade)
CMAKE_MINIMUM_REQUIRED(VERSION 2.6)

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
include_directories(./include)
include_directories(./)


option(BUILD32 "Build x86" OFF)
if(BUILD32)
    message("building x86")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32")
else()
    message("building x64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -g -s")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -g -s")
endif()

link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../drivers)

add_executable(sample 
    ./src/sample.cpp
    )
option(USE_STATIC_LIB "use libirupgrade.a" OFF)
if (USE_STATIC_LIB)
    target_link_libraries(sample ircam ircmd iruart iri2c usb-1.0 pthread irdfu libirupgrade.a -lm)
else ()
    target_link_libraries(sample ircam ircmd iruart iri2c usb-1.0 pthread irdfu irupgrade -lm)
endif()
