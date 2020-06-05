cmake_minimum_required(VERSION 3.6)

set(CMAKE_CXX_STANDARD 14)
add_compile_options("-Wextra")

message(STATUS "ANDROID_ABI=${ANDROID_ABI}")

# hacky
include_directories(".")
add_definitions(-include always_assert.h)

set(target vio_main)
project(${target} CXX)

set(ROOT_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../../..")
set(mobile-cv-suite_DIR "${ROOT_DIR}/mobile-cv-suite")

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
        "GLESv2"
        mobile-cv-suite
        mediandk
        log)

option(USE_CUSTOM_VIO "Compile with a custom VIO module" OFF)
if (USE_CUSTOM_VIO)
    # assumes the VIO module also uses mobile-cv-suite
    message(STATUS "Custom VIO build ${target}")
    add_subdirectory("${ROOT_DIR}/custom-vio/viotester-integration/android" build-custom-vio)
    list(APPEND VIO_ANDROID_LIBS ${CUSTOM_VIO_LIBS})
    target_compile_definitions(${target} PRIVATE "-DUSE_CUSTOM_VIO")
else()
    find_package(mobile-cv-suite REQUIRED)
endif()

target_link_libraries(${target} ${VIO_ANDROID_LIBS})
