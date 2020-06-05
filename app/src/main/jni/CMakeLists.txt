cmake_minimum_required(VERSION 3.6)

set(CMAKE_CXX_STANDARD 14)
add_compile_options("-Wextra")

message(STATUS "ANDROID_ABI=${ANDROID_ABI}")

# hacky
include_directories(".")
add_definitions(-include always_assert.h)

set(target vio_main)
project(${target} CXX)

message(STATUS "Custom VIO build ${target}")

set(BUILD_CLI_TOOLS OFF CACHE STRING "CLI tools" FORCE)
set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")
set(mobile-cv-suite_DIR "${ROOT_DIR}/mobile-cv-suite")
add_subdirectory("${ROOT_DIR}/custom-vio/viotester-integration/android" build-custom-vio)

add_library(${target} SHARED
        algorithm_worker.cpp
        algorithm_module.cpp
        modules/camera_calibrator.cpp
        modules/recorder.cpp
        opengl/algorithm_renderer.cpp
        opengl/ar_renderer.cpp
        opengl/camera_renderer.cpp
        opengl/ext_ar_renderer.cpp
        opengl/util.cpp)

set(VIO_ANDROID_LIBS
        mobile-cv-suite
        "GLESv2"
        ${CUSTOM_VIO_LIBS}
        mediandk
        log)

target_link_libraries(${target} ${VIO_ANDROID_LIBS})
