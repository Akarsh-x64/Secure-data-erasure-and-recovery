cmake_minimum_required(VERSION 3.15)

set(TSK_VERSION 4.15.0 CACHE INTERNAL "Pinned The Sleuth Kit version")

if(WIN32)
    set(TSK_HAVE_SYS_STAT_H "")
    set(TSK_HAVE_SYS_TYPES_H "")
    set(TSK_HAVE_UNISTD_H "")
else()
    set(TSK_HAVE_SYS_STAT_H "#define HAVE_SYS_STAT_H 1")
    set(TSK_HAVE_SYS_TYPES_H "#define HAVE_SYS_TYPES_H 1")
    set(TSK_HAVE_UNISTD_H "#define HAVE_UNISTD_H 1")
endif()

configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tsk_config.h.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/tsk/tsk_config.h"
    @ONLY)
configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/tsk_incs.h.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/tsk/tsk_incs.h"
    COPYONLY)

set(TSK_SOURCES)
foreach(tsk_component IN ITEMS base img vs fs pool util)
    file(GLOB component_sources CONFIGURE_DEPENDS
        "${CMAKE_CURRENT_SOURCE_DIR}/tsk/${tsk_component}/*.c"
        "${CMAKE_CURRENT_SOURCE_DIR}/tsk/${tsk_component}/*.cpp")
    list(APPEND TSK_SOURCES ${component_sources})
endforeach()
list(APPEND TSK_SOURCES
    "${CMAKE_CURRENT_SOURCE_DIR}/tsk/auto/guid.cpp")

add_library(tsk STATIC ${TSK_SOURCES})
add_library(TSK::tsk ALIAS tsk)

set_target_properties(tsk PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_features(tsk PUBLIC cxx_std_17)
target_include_directories(tsk PUBLIC
    "${CMAKE_CURRENT_BINARY_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}"
    "${CMAKE_CURRENT_SOURCE_DIR}/tsk")

target_compile_definitions(tsk PRIVATE
    HAVE_CXX14=1
    HAVE_INTTYPES_H=1
    HAVE_STDINT_H=1
    HAVE_STDIO_H=1
    HAVE_STDLIB_H=1
    HAVE_STRING_H=1
    HAVE_LIST=1
    HAVE_MAP=1
    HAVE_QUEUE=1
    HAVE_SET=1
    HAVE_STACK=1
    HAVE_STREAMBUF=1
    HAVE_STRING=1
    HAVE_VECTOR=1
    HAVE__BOOL=1)

if(MSVC)
    target_compile_definitions(tsk PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()