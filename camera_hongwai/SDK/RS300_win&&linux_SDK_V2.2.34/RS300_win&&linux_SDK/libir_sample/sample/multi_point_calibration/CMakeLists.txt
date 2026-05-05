PROJECT(ircmd)
CMAKE_MINIMUM_REQUIRED(VERSION 2.6)

set(EXTERN_LIB extern_lib.cmake)
if(NOT EXISTS ${CMAKE_CURRENT_SOURCE_DIR}/${EXTERN_LIB})
 file(WRITE ${EXTERN_LIB} "")
endif()
include(${EXTERN_LIB})

include_directories(../../interfaces)
include_directories(../../drivers)
include_directories(../../other)
include_directories(./include)


option(BUILD32 "Build x86" OFF)
if(BUILD32)   
    message("building x86")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -m32 -msse2 -mfpmath=sse -Wall -Werror")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -m32 -msse2 -mfpmath=sse -Wall -Werror")
else()
    message("building x64")
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fpic -Wall -Werror")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fpic -Wall -Werror")
endif()

link_directories(${CMAKE_CURRENT_SOURCE_DIR}/../../drivers ./)

add_executable(sample 
    ./src/sample.cpp
    )

if(${CMAKE_SYSTEM_NAME} MATCHES "Android")
target_link_libraries(sample ircmd.a iruart.a ircam.a irupgrade.a irtemp.a log -lm)
else()
target_link_libraries(sample ircmd iruart ircam  irupgrade irtemp -lm)
endif()


